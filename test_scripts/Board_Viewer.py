#!/usr/bin/env python3
"""Desktop viewer for VL53L9EV USB CDC / Ethernet sensor and identity packets."""
import argparse
from collections import deque
import json
import socket
import threading
import time
try:
    import tkinter as tk
    from tkinter import ttk, filedialog
except ImportError as error:
    raise SystemExit("Tk is required. On Ubuntu/Debian install python3-tk, then rerun this viewer.") from error

import numpy as np
import serial
from serial.tools import list_ports
from matplotlib.figure import Figure
try:
    from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
except ImportError as error:
    raise SystemExit(
        "The Tk plotting backend needs Pillow ImageTk. On Ubuntu/Debian run:\n"
        "  sudo apt install python3-tk python3-pil.imagetk\n"
        "Then run this viewer without sudo, using the Python environment where\n"
        "numpy, matplotlib, pyserial and Pillow are installed.\n"
        f"Import failed: {error}"
    ) from error

from USB_Debug import PacketParser, MAGIC, IMU_MAGIC, PARAM_MAGIC, IMU_PAYLOAD, decode_depth


class Receiver:
    def __init__(self, mode, endpoint, port):
        self.mode, self.endpoint, self.port = mode, endpoint, port
        self.stop = threading.Event()
        self.lock = threading.Lock()
        self.status = 'Connecting'
        self.total = self.lidar_count = self.imu_count = self.gaps = 0
        self.discarded = self.invalid = 0
        self.metadata = None
        self.depth = None
        self.samples = deque(maxlen=1000)
        self.last_packet = None
        self.session = 0
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def run(self):
        while not self.stop.is_set():
            try:
                if self.mode == 'Ethernet':
                    connection = socket.create_connection((self.endpoint, self.port), timeout=2)
                    connection.settimeout(.2)
                else:
                    connection = serial.Serial(self.endpoint, 115200, timeout=.2)
                with connection:
                    parser = PacketParser()
                    previous = {}
                    with self.lock:
                        self.status = 'Connected'
                        self.session += 1
                        self.metadata = self.depth = None
                        self.samples.clear()
                        self.last_packet = None
                    while not self.stop.is_set():
                        try:
                            data = (connection.recv(65536) if self.mode == 'Ethernet'
                                    else connection.read(min(65536, max(1, connection.in_waiting))))
                        except socket.timeout:
                            continue
                        if not data:
                            if self.mode == 'Ethernet':
                                raise ConnectionError('Device closed connection')
                            continue
                        packets = parser.feed(data)
                        with self.lock:
                            self.total += len(data)
                            self.discarded, self.invalid = parser.discarded, parser.invalid_headers
                            for packet in packets:
                                self.last_packet = time.monotonic()
                                if packet.kind == PARAM_MAGIC:
                                    try:
                                        metadata = json.loads(packet.payload)
                                        if not isinstance(metadata, dict):
                                            raise ValueError('Expected JSON object')
                                        self.metadata = metadata
                                    except (ValueError, UnicodeError):
                                        self.invalid += 1
                                    continue
                                old = previous.get(packet.kind)
                                if old is not None:
                                    delta = (packet.sequence - old - 1) & 0xffffffff
                                    if delta < 0x80000000:
                                        self.gaps += delta
                                previous[packet.kind] = packet.sequence
                                if packet.kind == MAGIC:
                                    self.depth = packet
                                    self.lidar_count += 1
                                elif packet.kind == IMU_MAGIC:
                                    self.samples.append((time.monotonic(), IMU_PAYLOAD.unpack(packet.payload)))
                                    self.imu_count += 1
            except (OSError, serial.SerialException) as error:
                with self.lock:
                    self.status = f'Reconnecting: {error}'
                    self.metadata = self.depth = None
                    self.samples.clear()
                self.stop.wait(1)
        with self.lock:
            self.status = 'Disconnected'


class Viewer:
    def __init__(self, root, args):
        self.root = root
        root.title('VL53L9EV · Device Monitor')
        root.geometry('1200x850')
        self.receiver = None
        self.metadata = None
        self.last_metadata = None
        self.last_depth = None
        self.last_stats = (time.monotonic(), 0, 0, 0)
        self.last_session = -1
        bar = ttk.Frame(root, padding=12)
        bar.pack(fill='x')
        self.mode = tk.StringVar(value='USB' if args.port else 'Ethernet')
        self.endpoint = tk.StringVar(value=args.port or args.host)
        self.port = tk.StringVar(value=str(args.tcp_port))
        ttk.Label(bar, text='Transport').pack(side='left')
        ttk.Combobox(bar, textvariable=self.mode, values=['Ethernet', 'USB'], state='readonly', width=10).pack(side='left', padx=8)
        ttk.Label(bar, text='Host / serial port').pack(side='left')
        self.endpoint_box = ttk.Combobox(bar, textvariable=self.endpoint, values=[p.device for p in list_ports.comports()], width=24)
        self.endpoint_box.pack(side='left', padx=8)
        ttk.Label(bar, text='TCP port').pack(side='left')
        ttk.Entry(bar, textvariable=self.port, width=7).pack(side='left', padx=8)
        ttk.Button(bar, text='Connect', command=self.connect).pack(side='left', padx=4)
        ttk.Button(bar, text='Disconnect', command=self.disconnect).pack(side='left', padx=4)
        self.status = tk.StringVar(value='Disconnected')
        ttk.Label(root, textvariable=self.status, padding=(12, 4)).pack(fill='x')
        self.stats = tk.StringVar(value='No data')
        ttk.Label(root, textvariable=self.stats, padding=(12, 8), font=('TkDefaultFont', 11)).pack(fill='x')
        tabs = ttk.Notebook(root)
        tabs.pack(fill='both', expand=True, padx=12, pady=8)
        sensor = ttk.Frame(tabs)
        identity = ttk.Frame(tabs)
        tabs.add(sensor, text='Live sensors')
        tabs.add(identity, text='Device & firmware parameters')
        fig = Figure(figsize=(11, 6), dpi=100)
        grid = fig.add_gridspec(3, 2)
        self.depth_ax = fig.add_subplot(grid[:, 0])
        self.depth_ax.set_title('Lidar radial depth (mm)')
        self.depth_image = self.depth_ax.imshow(np.full((42, 54), np.nan), vmin=0, vmax=args.depth_max, cmap='turbo', interpolation='nearest')
        fig.colorbar(self.depth_image, ax=self.depth_ax, label='mm', shrink=.7)
        self.axes = [fig.add_subplot(grid[i, 1]) for i in range(3)]
        self.lines = []
        for ax, title, labels in zip(self.axes, ['Acceleration (mg)', 'Gyroscope (°/s)', 'SFLP quaternion'], [('X','Y','Z'), ('X','Y','Z'), ('X','Y','Z','W')]):
            ax.set_title(title)
            ax.set_xlim(-10, 0)
            ax.grid(alpha=.2)
            self.lines.append([ax.plot([], [], label=label)[0] for label in labels])
            ax.legend(loc='upper left', ncol=len(labels), fontsize=8)
        self.axes[-1].set_xlabel('Seconds relative to latest sample')
        fig.tight_layout()
        self.canvas = FigureCanvasTkAgg(fig, sensor)
        self.canvas.get_tk_widget().pack(fill='both', expand=True)
        ttk.Button(identity, text='Save received JSON…', command=self.save_metadata).pack(anchor='w', padx=8, pady=8)
        self.tree = ttk.Treeview(identity, columns=('value',), show='tree headings')
        self.tree.heading('#0', text='Parameter')
        self.tree.heading('value', text='Value')
        self.tree.column('#0', width=280)
        self.tree.column('value', width=750)
        scroll = ttk.Scrollbar(identity, orient='vertical', command=self.tree.yview)
        self.tree.configure(yscrollcommand=scroll.set)
        scroll.pack(side='right', fill='y')
        self.tree.pack(fill='both', expand=True)
        root.protocol('WM_DELETE_WINDOW', self.close)
        root.after(100, self.update)
        if args.connect:
            self.connect()

    def connect(self):
        if self.receiver and self.receiver.thread.is_alive():
            self.receiver.stop.set()
            self.status.set('Disconnecting previous session; press Connect again shortly')
            return
        try:
            port = int(self.port.get())
            if not 1 <= port <= 65535 or not self.endpoint.get().strip():
                raise ValueError()
        except ValueError:
            self.status.set('Enter a host/serial port and a valid TCP port')
            return
        self.receiver = Receiver(self.mode.get(), self.endpoint.get().strip(), port)
        self.last_stats = (time.monotonic(), 0, 0, 0)
        self.last_session = -1
        self.last_depth = self.last_metadata = self.metadata = None
        self.tree.delete(*self.tree.get_children())

    def disconnect(self):
        if self.receiver:
            self.receiver.stop.set()

    def save_metadata(self):
        if self.metadata is None:
            self.status.set('No parameter packet received yet')
            return
        path = filedialog.asksaveasfilename(defaultextension='.json', filetypes=[('JSON', '*.json')])
        if path:
            with open(path, 'w', encoding='utf-8') as file:
                json.dump(self.metadata, file, indent=2, ensure_ascii=False)

    def populate(self, parent, data):
        for key, value in data.items():
            if isinstance(value, dict):
                item = self.tree.insert(parent, 'end', text=key, open=True)
                self.populate(item, value)
            else:
                self.tree.insert(parent, 'end', text=key, values=(json.dumps(value, ensure_ascii=False),))

    def update(self):
        r = self.receiver
        if r:
            with r.lock:
                status, total, lidar, imu, gaps = r.status, r.total, r.lidar_count, r.imu_count, r.gaps
                depth, samples, metadata = r.depth, list(r.samples), r.metadata
                discarded, invalid, last_packet, session = r.discarded, r.invalid, r.last_packet, r.session
            age = '' if last_packet is None else f' · last packet {time.monotonic()-last_packet:.1f}s ago'
            self.status.set(f'{r.mode}: {r.endpoint} · {status}{age}')
            now = time.monotonic()
            then, old_total, old_lidar, old_imu = self.last_stats
            dt = now - then
            if dt >= .5:
                self.stats.set(f'RX {(total-old_total)*8/dt/1e6:.2f} Mbit/s · {(total-old_total)/dt/1024:.1f} KiB/s   |   Lidar {(lidar-old_lidar)/dt:.1f} fps   |   IMU {(imu-old_imu)/dt:.1f} Hz   |   Total {total/1048576:.2f} MiB   |   Missing seq {gaps}   |   Discarded {discarded} B / malformed {invalid}')
                self.last_stats = (now, total, lidar, imu)
            if session != self.last_session:
                self.last_depth = self.last_metadata = None
                self.last_session = session
            if metadata != self.last_metadata:
                self.tree.delete(*self.tree.get_children())
                if metadata is not None:
                    self.populate('', metadata)
                self.last_metadata = metadata
            self.metadata = metadata
            if depth is not None and depth is not self.last_depth:
                try:
                    decoded = decode_depth(depth)
                    # Shared decoder returns depth array plus metadata.
                    values = decoded[0] if isinstance(decoded, tuple) else decoded
                    self.depth_image.set_data(np.ma.masked_equal(values, 0))
                    h, w = values.shape
                    self.depth_image.set_extent((-.5, w-.5, h-.5, -.5))
                    self.depth_ax.set_xlim(-.5, w-.5)
                    self.depth_ax.set_ylim(h-.5, -.5)
                    self.depth_ax.set_title(f'Lidar {w} × {h} · depth (mm) · seq {depth.sequence}')
                except (ValueError, TypeError) as error:
                    self.depth_ax.set_title(f'Invalid depth: {error}')
                self.last_depth = depth
            if depth is None:
                self.depth_image.set_data(np.full((42,54), np.nan))
            if samples:
                times = np.array([s[0] for s in samples])
                data = np.array([s[1][1:] for s in samples])
                x = times - times[-1]
                for ax, lines, offset, scale in zip(self.axes, self.lines, [0,3,6], [1,.001,1]):
                    for j, line in enumerate(lines):
                        line.set_data(x, data[:,offset+j]*scale)
                    ax.relim()
                    ax.autoscale_view(scalex=False)
            else:
                for lines in self.lines:
                    for line in lines:
                        line.set_data([], [])
            self.canvas.draw_idle()
        self.root.after(100, self.update)

    def close(self):
        self.disconnect()
        self.root.destroy()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    transport = parser.add_mutually_exclusive_group()
    transport.add_argument('--host', default='192.168.8.20')
    transport.add_argument('--port', help='USB CDC serial device, e.g. /dev/ttyACM0')
    parser.add_argument('--tcp-port', type=int, default=50002)
    parser.add_argument('--depth-max', type=float, default=4000)
    parser.add_argument('--connect', action='store_true', help='Connect immediately on launch')
    args = parser.parse_args()
    root = tk.Tk()
    ttk.Style(root).theme_use('clam')
    Viewer(root, args)
    root.mainloop()


if __name__ == '__main__':
    main()
