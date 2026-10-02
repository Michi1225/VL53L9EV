#!/usr/bin/env python3

import re
import subprocess
import threading
import time
from collections import deque

import serial
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation


BAUDRATE = 115200          # Irrelevant for USB CDC, but pyserial requires a value
READ_SIZE = 4096
PLOT_SAMPLES = 4096

data_buffer = deque(maxlen=PLOT_SAMPLES)

rx_total = 0
rx_last = 0
port_name = None
connected = False

lock = threading.Lock()


def serial_reader(port):
    global rx_total, connected

    print(f"[USB] Opening {port}")

    try:
        ser = serial.Serial(
            port=port,
            baudrate=BAUDRATE,
            timeout=0.1,
        )
    except Exception as e:
        print(f"[USB] Failed to open {port}: {e}")
        return

    connected = True
    print(f"[USB] Connected to {port}")

    try:
        while True:
            data = ser.read(READ_SIZE)

            if data:
                with lock:
                    data_buffer.extend(data)
                    rx_total += len(data)

    except serial.SerialException as e:
        print(f"[USB] Serial error: {e}")

    finally:
        connected = False
        ser.close()
        print(f"[USB] {port} disconnected")


def usb_monitor():
    global port_name

    # Follow kernel log continuously
    proc = subprocess.Popen(
        ["dmesg", "--follow", "--color=never"],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
    )

    # Matches e.g.
    # cdc_acm 1-3:1.0: ttyACM1: USB ACM device
    pattern = re.compile(r"(ttyACM1+): USB ACM device")

    print("[USB] Waiting for CDC ACM device...")

    for line in proc.stdout:
        print("[dmesg]", line.rstrip())

        match = pattern.search(line)

        if match:
            new_port = "/dev/" + match.group(1)

            if new_port != port_name or not connected:
                port_name = new_port

                threading.Thread(
                    target=serial_reader,
                    args=(new_port,),
                    daemon=True,
                ).start()


def status_thread():
    global rx_last

    while True:
        time.sleep(1.0)

        with lock:
            total = rx_total

        rate = total - rx_last
        rx_last = total

        if connected:
            print(
                f"[STATUS] {port_name} | "
                f"RX total: {total:10d} B | "
                f"RX rate: {rate:8d} B/s"
            )
        else:
            print("[STATUS] waiting for device...")


# ----------------------------------------------------------------------
# Start background threads
# ----------------------------------------------------------------------

threading.Thread(
    target=usb_monitor,
    daemon=True,
).start()

threading.Thread(
    target=status_thread,
    daemon=True,
).start()


# ----------------------------------------------------------------------
# Live plot
# ----------------------------------------------------------------------

fig, ax = plt.subplots()

line, = ax.plot([], [])

ax.set_ylim(0, 255)
ax.set_xlim(0, PLOT_SAMPLES)

ax.set_xlabel("Sample")
ax.set_ylabel("Received byte")
ax.set_title("USB CDC receive data")


def update_plot(_):
    with lock:
        data = list(data_buffer)

    if not data:
        return line,

    x = range(len(data))

    line.set_data(x, data)

    if len(data) > 1:
        ax.set_xlim(0, len(data))

    return line,


ani = FuncAnimation(
    fig,
    update_plot,
    interval=100,
    blit=False,
)

# plt.show()
while True:
    time.sleep(1)