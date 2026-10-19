#!/usr/bin/env python3
"""Plot VL9F depth and IMUF inertial packets over USB CDC or Ethernet TCP."""

import argparse
from collections import deque
from dataclasses import dataclass
import glob
import struct
import threading

BAUDRATE = 115200  # USB CDC ignores baud rate.
READ_SIZE = 4096
PLOT_FPS = 30
IMU_PLOT_SECONDS = 10
IMU_PLOT_SAMPLES = 1000
HEADER = struct.Struct("<4sIIHH")
MAGIC = b"VL9F"
IMU_MAGIC = b"IMUF"
PARAM_MAGIC = b"PARM"
TOF_MAGIC = b"TOFS"
TOF_STATUS = struct.Struct("<IIiIHHBBBBHHI")  # legacy version 1
TOF_STATUS_V2 = struct.Struct("<IIiIHHBBBBHHIHH")
PROFILE_MAGIC = b"PROF"
PROFILE_HEADER = struct.Struct("<IIQQQQHH")
PROFILE_THREAD = struct.Struct("<IIQ32s")
PROFILE_MAX_THREADS = 64
IMU_PAYLOAD = struct.Struct("<I10f")
MAX_PAYLOAD = 100 * 149
RAW_GRID_SIZES = {2: (54, 42), 4: (24, 24), 6: (18, 14),
                  8: (12, 10), 12: (8, 8), 24: (4, 4)}


@dataclass(frozen=True)
class Packet:
    sequence: int
    width: int
    height: int
    payload: bytes
    kind: bytes = MAGIC


## @brief Decode one interval of firmware CPU cycles, including all ThreadX threads.
def decode_profile(packet):
    """Decode one interval of firmware CPU cycles, including all ThreadX threads."""
    if packet.kind != PROFILE_MAGIC or packet.width != 1 or len(packet.payload) < PROFILE_HEADER.size:
        raise ValueError("unsupported profiler packet")
    timestamp, clock, interval, idle, isr, scheduler, count, flags = PROFILE_HEADER.unpack_from(packet.payload)
    if (not clock or not interval or count != packet.height or count > PROFILE_MAX_THREADS or
            flags & ~1 or len(packet.payload) != PROFILE_HEADER.size + count * PROFILE_THREAD.size):
        raise ValueError("invalid profiler header")
    threads = []
    ids = set()
    for offset in range(PROFILE_HEADER.size, len(packet.payload), PROFILE_THREAD.size):
        ident, priority, cycles, name = PROFILE_THREAD.unpack_from(packet.payload, offset)
        if ident in ids:
            raise ValueError("duplicate profiler thread")
        ids.add(ident)
        threads.append(dict(id=ident, priority=priority, cycles=cycles,
                            name=name.split(b"\0", 1)[0].decode("utf-8", errors="replace")))
    if idle + isr + scheduler + sum(t['cycles'] for t in threads) > interval:
        raise ValueError("profiler times exceed interval")
    return dict(timestamp_ms=timestamp, clock_hz=clock, interval_cycles=interval,
                idle_cycles=idle, isr_cycles=isr, scheduler_cycles=scheduler,
                truncated=bool(flags & 1), threads=threads)


## @brief Return stable ID, label, priority, milliseconds and CPU share for display.
def profile_rows(profile):
    """Return stable ID, label, priority, milliseconds and CPU share for display."""
    rows = [(f"thread-{t['id']:08x}", t['name'] or f"Thread {t['id']:08x}",
             t['priority'], t['cycles']) for t in profile['threads']]
    rows += [('idle', 'Idle', '', profile['idle_cycles']),
             ('isr', 'Interrupts', '', profile['isr_cycles']),
             ('scheduler', 'Scheduler', '', profile['scheduler_cycles'])]
    other = profile['interval_cycles'] - sum(row[3] for row in rows)
    if other:
        rows.append(('other', 'Other / removed threads', '', other))
    return [(key, name, priority, cycles * 1000 / profile['clock_hz'],
             cycles * 100 / profile['interval_cycles']) for key, name, priority, cycles in rows]


## @brief Decode the raw radial-distance plane and the status row's crop.
def decode_depth(packet):
    """Decode the raw radial-distance plane and the status row's crop.

    Layout follows vl53l9_utils.h: 100-byte metadata at the payload end,
    binning in bits 6..10 of offset 58 and crop fields at offset 72.
    Bit 15 of each distance is the ranging-step selector, not validity.
    """
    import numpy as np

    if packet.kind != MAGIC or len(packet.payload) < 100:
        raise ValueError("missing VL53L9 status row")
    metadata = packet.payload[-100:]
    dynamic = struct.unpack_from("<H", metadata, 58)[0]
    binning = (dynamic >> 6) & 0x1F
    if binning not in RAW_GRID_SIZES:
        raise ValueError(f"unsupported metadata binning {binning}")
    width, height = RAW_GRID_SIZES[binning]
    zones = width * height
    if len(packet.payload) < zones * 6 + (zones // 2 if dynamic & 0x30 else 0) + 100:
        raise ValueError("payload is too short for depth/amplitude/ambient/DSS")
    words = np.frombuffer(packet.payload, dtype="<u2", count=zones)
    depth = (words & 0x7FFF).astype(float).reshape(height, width)
    crop = struct.unpack_from("<I", metadata, 72)[0]
    if crop & (1 << 24):
        crop_width, crop_height = crop & 0x3F, (crop >> 6) & 0x3F
        x, y = (crop >> 12) & 0x3F, (crop >> 18) & 0x3F
        if not (crop_width and crop_height and x + crop_width <= width and
                y + crop_height <= height):
            raise ValueError("invalid metadata crop bounds")
        depth = depth[y:y + crop_height, x:x + crop_width]
    # Show missing zero-distance returns in gray rather than as a near target.
    return np.ma.masked_equal(depth, 0), binning


## @brief Live sensor status from the final 100-byte CSI row (UM3683 Table 7).
def decode_depth_metadata(packet):
    """Live sensor status from the final 100-byte CSI row (UM3683 Table 7)."""
    if packet.kind != MAGIC or len(packet.payload) < 100:
        raise ValueError('Missing sensor status row')
    row = packet.payload[-100:]
    dynamic = struct.unpack_from('<H', row, 58)[0]
    return dict(counter=struct.unpack_from('<I', row)[0],
                temperature_c=struct.unpack_from('<h', row, 4)[0],
                ldd_temperature_raw=struct.unpack_from('<H', row, 6)[0],
                dss_mode=(dynamic >> 4) & 3, context=(dynamic >> 11) & 1,
                period_us=struct.unpack_from('<I', row, 68)[0],
                error=struct.unpack_from('<H', row, 60)[0])


class SensorFrameTiming:
    """Estimate sensor FPS from its counter, independently of delivered FPS.

    Host arrival times include buffering/jitter. Counter jumps count missed
    sensor frames; a reset/backward jump restarts the measurement window.
    """
    ## @brief Initialize the sensor frame counter timing window.
    def __init__(self):
        self.reset()

    ## @brief Clear the timing anchor and estimated sensor frame rate.
    def reset(self):
        self.anchor = None
        self.fps = None

    ## @brief Update the sensor frame-rate estimate while handling counter wraparound and resets.
    def add(self, counter, now):
        if self.anchor is None:
            self.anchor = (counter, now)
            return
        first, start = self.anchor
        delta = (counter - first) & 0xffffffff
        if delta >= 0x80000000:
            self.reset()
            self.anchor = (counter, now)
        elif now - start >= .5:
            self.fps = delta / (now - start)
            self.anchor = (counter, now)


class PacketParser:
    """Handle split/coalesced packets and recover after invalid headers."""

    ## @brief Initialize the stream buffer and parser recovery counters.
    def __init__(self):
        self.buffer = bytearray()
        self.discarded = 0
        self.invalid_headers = 0

    ## @brief Parse split or coalesced telemetry frames and recover from invalid headers.
    def feed(self, data):
        self.buffer.extend(data)
        packets = []
        while True:
            starts = [pos for magic in (MAGIC, IMU_MAGIC, PARAM_MAGIC, PROFILE_MAGIC, TOF_MAGIC)
                      if (pos := self.buffer.find(magic)) >= 0]
            start = min(starts) if starts else -1
            if start < 0:
                # Preserve a possible partial magic at the next read boundary.
                count = max(0, len(self.buffer) - (len(MAGIC) - 1))
                self.discarded += count
                del self.buffer[:count]
                break
            if start:
                self.discarded += start
                del self.buffer[:start]
            if len(self.buffer) < HEADER.size:
                break
            kind, sequence, length, width, height = HEADER.unpack_from(self.buffer)
            valid = ((kind == MAGIC and 0 < width <= 100 and 0 < height <= 149 and
                      length == width * height and length <= MAX_PAYLOAD) or
                     (kind == IMU_MAGIC and length == IMU_PAYLOAD.size and
                      width == 10 and height == 1) or
                     (kind == PARAM_MAGIC and 0 < length <= 16384 and
                      width == 1 and height == 0) or
                     (kind == TOF_MAGIC and ((width == 1 and length == TOF_STATUS.size) or
                                            (width == 2 and length == TOF_STATUS_V2.size))) or
                     (kind == PROFILE_MAGIC and width == 1 and height <= PROFILE_MAX_THREADS and
                      length == PROFILE_HEADER.size + height * PROFILE_THREAD.size))
            if not valid:
                self.invalid_headers += 1
                self.discarded += 1
                del self.buffer[0]
                continue
            end = HEADER.size + length
            if len(self.buffer) < end:
                break
            packets.append(Packet(sequence, width, height,
                                  bytes(self.buffer[HEADER.size:end]), kind))
            del self.buffer[:end]
        return packets


imu_plot_buffer = deque(maxlen=IMU_PLOT_SAMPLES)
rx_total = 0
packet_count = 0
sequence_gaps = 0
latest_packet = None
latest_imu_packet = None
latest_profile = None
imu_packet_count = 0
parser_discarded = 0
parser_invalid = 0
port_name = None
connected = False
lock = threading.Lock()
stop_event = threading.Event()


## @brief Read and decode a transport session while updating shared packet statistics and samples.
def stream_reader(read_chunk, label):
    global rx_total, connected, latest_packet, packet_count, sequence_gaps
    global parser_discarded, parser_invalid, latest_imu_packet, imu_packet_count
    global latest_profile
    parser = PacketParser()
    previous_sequence = {}
    previous_imu_timestamp = None
    imu_elapsed_ms = 0
    with lock:
        latest_packet = None
        latest_imu_packet = None
        latest_profile = None
        parser_discarded = parser_invalid = 0
        imu_plot_buffer.clear()
        connected = True
    print(f"[STREAM] Connected to {label}")
    try:
        while not stop_event.is_set():
            data = read_chunk()
            if not data:
                continue
            packets = parser.feed(data)
            with lock:
                rx_total += len(data)
                parser_discarded = parser.discarded
                parser_invalid = parser.invalid_headers
                for packet in packets:
                    if packet.kind in (PARAM_MAGIC, TOF_MAGIC):
                        continue
                    if packet.kind == PROFILE_MAGIC:
                        try:
                            latest_profile = decode_profile(packet)
                        except ValueError:
                            parser.invalid_headers += 1
                        continue
                    previous = previous_sequence.get(packet.kind)
                    if previous is not None and packet.sequence != ((previous + 1) & 0xFFFFFFFF):
                        sequence_gaps += 1
                    previous_sequence[packet.kind] = packet.sequence
                    packet_count += 1
                    if packet.kind == MAGIC:
                        latest_packet = packet
                    else:
                        latest_imu_packet = packet
                        imu_packet_count += 1
                        timestamp, *values = IMU_PAYLOAD.unpack(packet.payload)
                        if previous_imu_timestamp is not None:
                            delta = (timestamp - previous_imu_timestamp) & 0xFFFFFFFF
                            if delta < 0x80000000:
                                imu_elapsed_ms += delta
                            else:
                                # MCU restarted without a USB disconnect.
                                imu_plot_buffer.clear()
                                imu_elapsed_ms = 0
                        previous_imu_timestamp = timestamp
                        elapsed = imu_elapsed_ms / 1000.0
                        imu_plot_buffer.append((elapsed, values))
                        while imu_plot_buffer and elapsed - imu_plot_buffer[0][0] > IMU_PLOT_SECONDS:
                            imu_plot_buffer.popleft()
    except OSError as exc:
        print(f"[STREAM] Connection error: {exc}")
    finally:
        connected = False
        print(f"[STREAM] {label} disconnected")



## @brief Open a USB serial session and drain available input into the stream decoder.
def serial_reader(port):
    import serial
    print(f"[USB] Opening {port}")
    try:
        ser = serial.Serial(port, BAUDRATE, timeout=0.1)
    except OSError as exc:
        print(f"[USB] Failed to open {port}: {exc}")
        return
    ## @brief Read at least one serial byte and drain currently available data.
    def read_chunk():
        data = ser.read(1)
        if data and ser.in_waiting:
            data += ser.read(min(ser.in_waiting, READ_SIZE - 1))
        return data
    try:
        stream_reader(read_chunk, port)
    except serial.SerialException as exc:
        print(f"[USB] Serial error: {exc}")
    finally:
        ser.close()


## @brief Reconnect to the TCP stream and decode packets until monitoring stops.
def ethernet_monitor(host, tcp_port):
    import socket
    global port_name
    port_name = f"{host}:{tcp_port}"
    while not stop_event.is_set():
        try:
            with socket.create_connection((host, tcp_port), timeout=3) as connection:
                connection.settimeout(0.2)
                ## @brief Read TCP input, treating a timeout as no data and a closed socket as disconnection.
                def read_chunk():
                    try:
                        data = connection.recv(65536)
                    except socket.timeout:
                        return b''
                    if not data:
                        raise ConnectionError('TCP peer closed the stream')
                    return data
                stream_reader(read_chunk, port_name)
        except OSError as exc:
            print(f"[ETH] Cannot connect to {port_name}: {exc}")
        stop_event.wait(1.0)

## @brief Keep exactly one reader active, retrying after unplug or open failure.
def usb_monitor(requested_port=None):
    """Keep exactly one reader active, retrying after unplug or open failure."""
    global port_name
    print("[USB] Waiting for CDC ACM device...")
    while not stop_event.is_set():
        if requested_port:
            port_name = requested_port
            serial_reader(port_name)
        else:
            ports = sorted(glob.glob("/dev/ttyACM*"))
            # Prefer the last device if it is still present, but allow a new
            # ttyACM number after USB re-enumeration.
            if port_name in ports:
                ports.remove(port_name)
                ports.insert(0, port_name)
            for port in ports:
                if stop_event.is_set():
                    break
                port_name = port
                serial_reader(port_name)
        stop_event.wait(1.0)


## @brief Print periodic transport, sensor, IMU, and CPU usage diagnostics.
def status_thread():
    last_bytes = last_packets = last_imu = 0
    while not stop_event.wait(1.0):
        with lock:
            total, count, packet = rx_total, packet_count, latest_packet
            gaps, discarded, invalid = sequence_gaps, parser_discarded, parser_invalid
            imu_count, imu_packet = imu_packet_count, latest_imu_packet
            profile = latest_profile
        rate, fps = total - last_bytes, count - last_packets
        imu_fps = imu_count - last_imu
        last_imu = imu_count
        last_bytes, last_packets = total, count
        if not connected:
            print("[STATUS] waiting for device...")
            continue
        print(f"[STATUS] {port_name} | RX: {total} B | {rate} B/s | "
              f"packets: {count} | camera: {fps - imu_fps}/s | IMU: {imu_fps}/s | sequence gaps: {gaps} | "
              f"discarded: {discarded} B | invalid headers: {invalid}")
        if packet:
            nonzero = sum(value != 0 for value in packet.payload)
            print(f"[PACKET] seq={packet.sequence} size={packet.width}x{packet.height} "
                  f"payload={len(packet.payload)} B nonzero={nonzero} "
                  f"min={min(packet.payload)} max={max(packet.payload)} "
                  f"first16={packet.payload[:16].hex(' ')}")
        if imu_packet:
            timestamp, *values = IMU_PAYLOAD.unpack(imu_packet.payload)
            ## @brief Format a numeric vector for the IMU diagnostic output.
            def vector(items):
                return '(' + ', '.join(f'{value:.4f}' for value in items) + ')'
            print(f"[IMU] seq={imu_packet.sequence} t={timestamp} ms "
                  f"acc_mg={vector(values[:3])} gyro_mdps={vector(values[3:6])} "
                  f"quat_xyzw={vector(values[6:])}")
        if profile:
            print('[CPU] ' + ' | '.join(f'{name}: {ms:.2f} ms ({percent:.1f}%)'
                  for _, name, _, ms, percent in profile_rows(profile)))


## @brief Create the live depth and IMU plots and their animation callback.
def create_plots(refresh_hz=PLOT_FPS, depth_max=4000):
    import matplotlib.pyplot as plt
    from matplotlib.animation import FuncAnimation
    import numpy as np

    # Compute layout once; constrained layout ran again on every redraw.
    fig = plt.figure(figsize=(14, 9))
    layout = fig.add_gridspec(3, 2, width_ratios=(1.2, 1))
    ax = fig.add_subplot(layout[:, 0])
    acc_ax, gyro_ax, quat_ax = [fig.add_subplot(layout[row, 1]) for row in range(3)]
    fig.canvas.manager.set_window_title("VL53L9 and IMU streaming")
    cmap = plt.get_cmap("turbo_r").copy()
    cmap.set_bad("#555555")
    image = ax.imshow(np.ma.masked_all((42, 54)), interpolation="nearest",
                      origin="upper", cmap=cmap, vmin=0, vmax=depth_max)
    fig.colorbar(image, ax=ax, label="Raw radial depth (mm)")
    ax.set_xlabel("Zone column")
    ax.set_ylabel("Zone row")
    ax.set_title("VL53L9 depth grid")
    depth_info = ax.text(0.01, 0.98, "Waiting for a depth frame", transform=ax.transAxes,
                         va="top", color="white", fontsize=9,
                         bbox=dict(facecolor="black", alpha=0.6, edgecolor="none"))

    ## @brief Format the depth value under the plot cursor.
    def depth_coordinate(x, y):
        col, row = int(np.floor(x + 0.5)), int(np.floor(y + 0.5))
        grid = image.get_array()
        if 0 <= row < grid.shape[0] and 0 <= col < grid.shape[1]:
            value = grid[row, col]
            distance = "no return" if np.ma.is_masked(value) else f"{value:.0f} mm"
            return f"column={col}, row={row}, depth={distance}"
        return ""
    ax.format_coord = depth_coordinate

    imu_axes = (acc_ax, gyro_ax, quat_ax)
    imu_lines = []
    for plot_ax, title, ylabel, labels in (
        (acc_ax, "Acceleration", "Acceleration (mg)", ("x", "y", "z")),
        (gyro_ax, "Angular velocity", "Gyro (deg/s)", ("x", "y", "z")),
        (quat_ax, "SFLP game rotation quaternion", "Quaternion component", ("x", "y", "z", "w")),
    ):
        imu_lines.append([plot_ax.plot([], [], label=label)[0] for label in labels])
        plot_ax.set_title(title)
        plot_ax.set_ylabel(ylabel)
        plot_ax.set_xlabel("Time relative to latest sample (s)")
        plot_ax.set_xlim(-IMU_PLOT_SECONDS, 0)
        plot_ax.grid(True, alpha=0.3)
        plot_ax.legend(loc="upper right", ncol=len(labels))
    quat_ax.set_ylim(-1.1, 1.1)
    acc_ax.set_ylim(-1200, 1200)
    gyro_ax.set_ylim(-10, 10)
    fig.tight_layout()
    artists = (image, depth_info, *(trace for traces in imu_lines for trace in traces))
    use_blit = fig.canvas.supports_blit
    previous_packet = None

    ## @brief Refresh depth and inertial plot artists from the latest received samples.
    def update_plot(_):
        nonlocal previous_packet
        with lock:
            packet = latest_packet
            imu_samples = list(imu_plot_buffer)
        changed_limits = False
        if packet is not previous_packet:
            previous_packet = packet
            if packet is None:
                image.set_data(np.ma.masked_all(image.get_array().shape))
                depth_info.set_text("Waiting for a depth frame")
            else:
                try:
                    grid, binning = decode_depth(packet)
                    rows, cols = grid.shape
                    if grid.shape != image.get_array().shape:
                        image.set_extent((-0.5, cols - 0.5, rows - 0.5, -0.5))
                        ax.set_xlim(-0.5, cols - 0.5)
                        ax.set_ylim(rows - 0.5, -0.5)
                        changed_limits = True
                    image.set_data(grid)
                    depth_info.set_text(f"seq {packet.sequence} | {cols} × {rows} zones | binning {binning}")
                except ValueError as exc:
                    image.set_data(np.ma.masked_all(image.get_array().shape))
                    depth_info.set_text(f"Cannot decode depth: {exc}")
        times = np.asarray([sample[0] for sample in imu_samples])
        values = np.asarray([sample[1] for sample in imu_samples]).reshape(-1, 10)
        if len(times):
            times = times - times[-1]
        for plot_ax, traces, offset, scale in zip(
                imu_axes, imu_lines, (0, 3, 6), (1.0, 0.001, 1.0)):
            channels = values[:, offset:offset + len(traces)] * scale
            for component, trace in enumerate(traces):
                trace.set_data(times, channels[:, component])
            # Expand only when a new motion exceeds the current range, keeping
            # axes/ticks stable for blitting instead of autoscaling every frame.
            if len(times) and plot_ax is not quat_ax:
                finite = channels[np.isfinite(channels)]
                if finite.size:
                    low, high = plot_ax.get_ylim()
                    peak = float(np.max(np.abs(finite)))
                    if -peak < low or peak > high:
                        bound = max(abs(low), abs(high), peak * 1.2)
                        plot_ax.set_ylim(-bound, bound)
                        changed_limits = True
        if changed_limits and use_blit:
            # Refresh backgrounds/ticks when a range really changes. The
            # animation detects the new axis view and caches its background.
            fig.canvas.draw()
        return artists

    animation = FuncAnimation(fig, update_plot, init_func=lambda: artists,
                              interval=1000 / refresh_hz, blit=use_blit,
                              cache_frame_data=False)
    return fig, animation


## @brief Start the selected stream monitor, diagnostic output, and live plots.
def main(default_host=None):
    import matplotlib.pyplot as plt

    global port_name
    args_parser = argparse.ArgumentParser(description=__doc__)
    transport = args_parser.add_mutually_exclusive_group()
    transport.add_argument("--port", help="Serial device to retry, e.g. /dev/ttyACM1; default: rescan /dev/ttyACM*")
    transport.add_argument("--host", default=default_host, help="Stream over Ethernet from this host")
    args_parser.add_argument("--tcp-port", type=int, default=50002)
    args_parser.add_argument("--fps", type=int, default=PLOT_FPS,
                             help="Plot refresh target, 1–120 (default: 30)")
    args_parser.add_argument("--depth-max", type=float, default=4000,
                             help="Depth colour scale maximum in mm (default: 4000)")
    args = args_parser.parse_args()
    if not 1 <= args.fps <= 120:
        args_parser.error("--fps must be between 1 and 120")
    if not 0 < args.depth_max <= 32767:
        args_parser.error("--depth-max must be between 0 and 32767 mm")
    if not 1 <= args.tcp_port <= 65535:
        args_parser.error("--tcp-port must be 1..65535")
    if args.port:
        args.host = None
    port_name = args.port
    stop_event.clear()
    if args.host:
        threading.Thread(target=ethernet_monitor, args=(args.host, args.tcp_port), daemon=True).start()
    else:
        threading.Thread(target=usb_monitor, args=(args.port,), daemon=True).start()
    threading.Thread(target=status_thread, daemon=True).start()

    fig, animation = create_plots(args.fps, args.depth_max)
    try:
        plt.show()
    finally:
        stop_event.set()


if __name__ == "__main__":
    main()
