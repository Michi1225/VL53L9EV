#!/usr/bin/env python3
"""Desktop viewer for Magnecko Sense USB CDC / Ethernet sensor and identity packets."""
import argparse
from decimal import Decimal, InvalidOperation
from collections import deque
import json
from pathlib import Path
import socket
import threading
import time
try:
    import tkinter as tk
    from tkinter import ttk, filedialog, font as tkfont
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

from USB_Debug import (PacketParser, MAGIC, IMU_MAGIC, PARAM_MAGIC, IMU_PAYLOAD,
                       decode_depth, decode_depth_metadata, SensorFrameTiming, PROFILE_MAGIC, decode_profile, profile_rows, TOF_MAGIC)
from Sensor_Control import (GET_CONFIG, START, STOP, SET_CONFIG, RESET, POWER_DOWN, STATE_NAMES, POWER_NAMES,
                            RANGING_MODES, ranging_mode, RESULT_NAMES, OPERATION_NAMES, build_command, decode_status)
from Firmware_Update import UpdateWorker, fw, slot_name


class CpuShareChart:
    """Live CPU donut with stable colors and details on hover."""

    PALETTE = ('#2563eb', '#10b981', '#8b5cf6', '#f43f5e', '#06b6d4',
               '#eab308', '#6366f1', '#14b8a6')

    ## @brief Create the CPU share chart, stable color assignments, and hover handling.
    def __init__(self, parent):
        self.colors = {'idle': '#cbd5e1', 'isr': '#f59e0b',
                       'scheduler': '#a855f7', 'other': '#64748b', 'small': '#94a3b8'}
        self.thread_colors = {}
        self.figure = Figure(figsize=(5, 5), dpi=100, facecolor='#f8fafc')
        self.ax = self.figure.add_axes((.08, .24, .84, .70))
        self.canvas = FigureCanvasTkAgg(self.figure, parent)
        self.canvas.get_tk_widget().pack(fill='both', expand=True)
        self.canvas.mpl_connect('motion_notify_event', self.hover)
        self.slices = []
        self.rows = []
        self.tooltip = None
        self.show(None)

    ## @brief Return a stable chart color for a thread or accounting category.
    def color(self, key):
        if key in self.colors:
            return self.colors[key]
        if key not in self.thread_colors:
            self.thread_colors[key] = self.PALETTE[len(self.thread_colors) % len(self.PALETTE)]
        return self.thread_colors[key]

    ## @brief Render the current CPU usage profile and retain hover details for each slice.
    def show(self, profile):
        self.ax.clear()
        self.ax.set_aspect('equal')
        self.ax.set_axis_off()
        self.ax.set_title('CPU time distribution', color='#334155', fontsize=13, pad=14)
        for legend in list(self.figure.legends):
            legend.remove()
        self.slices = []
        self.rows = []
        self.tooltip = None
        if profile is None:
            self.ax.pie([1], colors=['#e2e8f0'], radius=1,
                        wedgeprops=dict(width=.28, edgecolor='#f8fafc', linewidth=2))
            self.ax.text(0, .04, 'No data', ha='center', va='center',
                         fontsize=19, color='#64748b', weight='bold')
            self.ax.text(0, -.20, 'Waiting for a report', ha='center', fontsize=9, color='#64748b')
        else:
            rows = profile_rows(profile)
            # Assign colors before sorting so changing loads do not swap colors.
            for key, *_ in rows:
                self.color(key)
            active = sorted((row for row in rows if row[4] > 0), key=lambda row: -row[4])
            self.rows = [row for row in active[:7] if row[4] >= 1]
            small = [row for row in active if row not in self.rows]
            if small:
                self.rows.append(('small', 'Other activities', '',
                                  sum(row[3] for row in small), sum(row[4] for row in small)))
            self.slices, _, _ = self.ax.pie(
                [row[4] for row in self.rows], colors=[self.color(row[0]) for row in self.rows],
                startangle=90, counterclock=False, radius=1,
                wedgeprops=dict(width=.28, edgecolor='#f8fafc', linewidth=2),
                autopct=lambda percent: f'{percent:.0f}%' if percent >= 5 else '',
                pctdistance=.86, textprops=dict(color='#0f172a', fontsize=9, weight='bold'))
            busy = 100 * (1 - profile['idle_cycles'] / profile['interval_cycles'])
            self.ax.text(0, .06, f'{busy:.1f}%', ha='center', va='center',
                         fontsize=28, color='#0f172a', weight='bold')
            self.ax.text(0, -.19, 'CPU busy', ha='center', fontsize=11, color='#64748b')
            labels = [f'{name[:25]}  {percent:.1f}%' for _, name, _, _, percent in self.rows]
            self.figure.legend(self.slices, labels, loc='lower center', bbox_to_anchor=(.5, .02),
                               ncol=2, frameon=False, fontsize=8, labelcolor='#334155',
                               handlelength=1, columnspacing=1.2)
            self.tooltip = self.ax.annotate(
                '', xy=(0, 0), xytext=(0, 16), textcoords='offset points',
                ha='center', va='bottom', fontsize=9, color='#f8fafc', zorder=10,
                bbox=dict(boxstyle='round,pad=.6', fc='#0f172a', ec='none', alpha=.95))
            self.tooltip.set_visible(False)
        self.canvas.draw_idle()

    ## @brief Show CPU usage details for the chart slice under the pointer.
    def hover(self, event):
        if self.tooltip is None:
            return
        selected = next((i for i, wedge in enumerate(self.slices)
                         if event.inaxes is self.ax and wedge.contains(event)[0]), None)
        if selected is None:
            if self.tooltip.get_visible():
                self.tooltip.set_visible(False)
                self.canvas.draw_idle()
            return
        _, name, _, ms, percent = self.rows[selected]
        self.tooltip.xy = (event.xdata, event.ydata)
        self.tooltip.set_text(f'{name}\n{ms:.3f} ms · {percent:.2f}%')
        self.tooltip.set_visible(True)
        self.canvas.draw_idle()


class Receiver:
    ## @brief Initialize a transport worker, telemetry state, and sensor command tracking.
    def __init__(self, mode, endpoint, port):
        self.mode, self.endpoint, self.port = mode, endpoint, port
        self.stop = threading.Event()
        self.lock = threading.Lock()
        self.status = 'Connecting'
        self.tof_status = self.tof_settings = None
        self.tof_received = None
        self.sensor_timing = SensorFrameTiming()
        self.sensor_metadata = self.sensor_metadata_received = None
        self.pending_command = self.outgoing = None
        self.command_message = 'Waiting for sensor status…'
        self.next_request_id = 1
        self.total = self.lidar_count = self.imu_count = self.gaps = 0
        self.discarded = self.invalid = 0
        self.metadata = None
        self.profile = None
        self.profile_received = None
        self.depth = None
        self.samples = deque(maxlen=1000)
        self.last_packet = None
        self.session = 0
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    ## @brief Queue one sensor command for the connected worker and track its acknowledgement.
    def send_command(self, operation, config=None):
        with self.lock:
            if self.status != 'Connected':
                raise ValueError('Connect to a device first')
            if self.pending_command:
                raise ValueError('Wait for the current command acknowledgement')
            request_id = self.next_request_id
            wire = build_command(request_id, operation, config)
            self.next_request_id = (request_id + 1) & 0xffffffff or 1
            self.pending_command = (request_id, operation, time.monotonic())
            self.outgoing = wire
            self.command_message = f'{OPERATION_NAMES[operation]}: waiting for device…'

    ## @brief Reconnect and decode telemetry while sending queued commands and matching replies.
    def run(self):
        while not self.stop.is_set():
            try:
                if self.mode == 'Ethernet':
                    connection = socket.create_connection((self.endpoint, self.port), timeout=2)
                    connection.settimeout(.2)
                else:
                    connection = serial.Serial(self.endpoint, 115200, timeout=.2, write_timeout=.5)
                with connection:
                    parser = PacketParser()
                    previous = {}
                    with self.lock:
                        self.status = 'Connected'
                        self.session += 1
                        self.metadata = self.depth = self.profile = None
                        self.samples.clear()
                        self.last_packet = None
                        self.tof_status = self.tof_settings = None
                        self.sensor_timing.reset()
                        self.sensor_metadata = self.sensor_metadata_received = None
                        self.pending_command = self.outgoing = None
                        self.command_message = 'Waiting for sensor status…'
                    while not self.stop.is_set():
                        with self.lock:
                            if self.pending_command and time.monotonic() - self.pending_command[2] > 8:
                                self.pending_command = self.outgoing = None
                                self.command_message = 'No acknowledgement; state may have changed. Refresh before retrying.'
                            outgoing, self.outgoing = self.outgoing, None
                        if outgoing:
                            if self.mode == 'Ethernet':
                                connection.sendall(outgoing)
                            elif connection.write(outgoing) != len(outgoing):
                                raise OSError('Incomplete USB command write')
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
                                if packet.kind == TOF_MAGIC:
                                    try:
                                        report = decode_status(packet)
                                        if self.tof_status is None and self.pending_command is None:
                                            self.command_message = 'Sensor status received.'
                                        self.tof_status = report
                                        self.tof_received = time.monotonic()
                                        pending = self.pending_command
                                        matched = pending and (report['request_id'], report['operation']) == pending[:2]
                                        if (report['state'] in (1, 2) and self.tof_settings is None) or (
                                                matched and report['result'] == 0 and report['operation'] in (GET_CONFIG, SET_CONFIG, RESET)):
                                            self.tof_settings = report['config']
                                        if matched:
                                            self.command_message = f"{OPERATION_NAMES[report['operation']]}: {RESULT_NAMES[report['result']]}"
                                            self.pending_command = None
                                        if report['state'] in (1, 3, 4) or (matched and report['operation'] == RESET):
                                            self.depth = None
                                            self.sensor_timing.reset()
                                    except ValueError:
                                        parser.invalid_headers += 1
                                        self.invalid = parser.invalid_headers
                                    continue
                                if packet.kind == PARAM_MAGIC:
                                    try:
                                        metadata = json.loads(packet.payload)
                                        if not isinstance(metadata, dict):
                                            raise ValueError('Expected JSON object')
                                        self.metadata = metadata
                                    except (ValueError, UnicodeError):
                                        parser.invalid_headers += 1
                                        self.invalid = parser.invalid_headers
                                    continue
                                if packet.kind == PROFILE_MAGIC:
                                    try:
                                        self.profile = decode_profile(packet)
                                        self.profile_received = time.monotonic()
                                    except ValueError:
                                        parser.invalid_headers += 1
                                        self.invalid = parser.invalid_headers
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
                                    try:
                                        self.sensor_metadata = decode_depth_metadata(packet)
                                        self.sensor_metadata_received = time.monotonic()
                                        self.sensor_timing.add(self.sensor_metadata['counter'], self.sensor_metadata_received)
                                    except ValueError:
                                        self.sensor_metadata = None
                                elif packet.kind == IMU_MAGIC:
                                    self.samples.append((time.monotonic(), IMU_PAYLOAD.unpack(packet.payload)))
                                    self.imu_count += 1
            except (OSError, serial.SerialException) as error:
                with self.lock:
                    self.sensor_metadata = self.sensor_metadata_received = None
                    self.sensor_timing.reset()
                    self.status = f'Reconnecting: {error}'
                    self.tof_status = self.tof_settings = None
                    self.command_message = 'Connection lost; refresh sensor state after reconnecting.'
                    self.pending_command = self.outgoing = None
                    self.metadata = self.depth = self.profile = None
                    self.samples.clear()
                self.stop.wait(1)
        with self.lock:
            self.sensor_metadata = self.sensor_metadata_received = None
            self.sensor_timing.reset()
            self.status = 'Disconnected'
            self.profile = None
            self.tof_status = self.tof_settings = None
            self.pending_command = self.outgoing = None


class Viewer:
    ## @brief Build the device monitor tabs, sensor controls, plots, and periodic refresh loop.
    def __init__(self, root, args):
        self.root = root
        root.title('Magnecko Sense · Device Monitor')
        root.geometry('1200x850')
        # Treeview's fixed row height does not follow the DPI-scaled font.
        # Share font-based spacing between the profiler and parameter tables.
        style = ttk.Style(root)
        table_font = tkfont.Font(root=root, font=style.lookup('Treeview', 'font') or 'TkDefaultFont')
        style.configure('Treeview', rowheight=table_font.metrics('linespace') + round(root.winfo_fpixels('4p')))
        self.receiver = None
        self.fw_worker = None
        self.fw_resume = False
        self.fw_report = self.fw_report_target = None
        self.closing = False
        self.last_tof_settings = None
        self.metadata = None
        self.last_metadata = None
        self.last_depth = None
        self.last_profile = None
        self.last_stats = (time.monotonic(), 0, 0, 0)
        self.last_session = -1
        bar = ttk.Frame(root, padding=12)
        bar.pack(fill='x')
        self.mode = tk.StringVar(value='USB' if args.port else 'Ethernet')
        self.endpoint = tk.StringVar(value=args.port or args.host)
        self.port = tk.StringVar(value=str(args.tcp_port))
        ttk.Label(bar, text='Transport').pack(side='left')
        mode_box = ttk.Combobox(bar, textvariable=self.mode, values=['Ethernet', 'USB'], state='readonly', width=10)
        mode_box.pack(side='left', padx=8)
        ttk.Label(bar, text='Host / serial port').pack(side='left')
        self.endpoint_box = ttk.Combobox(bar, textvariable=self.endpoint, values=[p.device for p in list_ports.comports()], width=24)
        self.endpoint_box.pack(side='left', padx=8)
        ttk.Label(bar, text='TCP port').pack(side='left')
        port_box = ttk.Entry(bar, textvariable=self.port, width=7)
        port_box.pack(side='left', padx=8)
        connect_button = ttk.Button(bar, text='Connect', command=self.connect)
        connect_button.pack(side='left', padx=4)
        self.connection_widgets = [(mode_box, 'readonly'), (self.endpoint_box, 'normal'),
                                   (port_box, 'normal'), (connect_button, 'normal')]
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
        profiler = ttk.Frame(tabs, padding=12)
        tabs.add(profiler, text='CPU profiler')
        self.profile_status = tk.StringVar(value='Waiting for profiler data…')
        ttk.Label(profiler, textvariable=self.profile_status).pack(anchor='w', pady=(0, 8))
        self.cpu_busy = ttk.Progressbar(profiler, maximum=100)
        self.cpu_busy.pack(fill='x', pady=(0, 12))
        ttk.Label(profiler, text='CPU time per reporting interval; blocked/sleeping time is excluded from threads.').pack(anchor='w', pady=(0, 8))
        profile_panes = ttk.Panedwindow(profiler, orient='horizontal')
        profile_panes.pack(fill='both', expand=True)
        table = ttk.Frame(profile_panes)
        chart = ttk.Frame(profile_panes)
        profile_panes.add(table, weight=3)
        profile_panes.add(chart, weight=2)
        self.profile_tree = ttk.Treeview(table, columns=('name', 'priority', 'ms', 'percent'), show='headings')
        for column, title, width in [('name', 'Thread / activity', 240), ('priority', 'Priority', 65),
                                      ('ms', 'CPU time (ms)', 110), ('percent', 'CPU share (%)', 110)]:
            self.profile_tree.heading(column, text=title)
            self.profile_tree.column(column, width=width, anchor='w' if column == 'name' else 'e')
        profile_scroll = ttk.Scrollbar(table, orient='vertical', command=self.profile_tree.yview)
        self.profile_tree.configure(yscrollcommand=profile_scroll.set)
        profile_scroll.pack(side='right', fill='y')
        self.profile_tree.pack(fill='both', expand=True)
        self.profile_chart = CpuShareChart(chart)
        configuration = ttk.Frame(tabs, padding=20)
        tabs.add(configuration, text='Sensor configuration')
        self.tof_state = tk.StringVar(value='ToF: waiting for device status')
        self.tof_command_status = tk.StringVar(value='Connect to read sensor settings.')
        self.tof_buttons = []
        live_controls = ttk.Frame(sensor, padding=(8, 6))
        live_controls.pack(fill='x')
        self.add_tof_button(live_controls, 'Start ToF', START)
        self.add_tof_button(live_controls, 'Stop ToF', STOP)
        self.add_tof_button(live_controls, 'Reset ToF', RESET)
        self.add_tof_button(live_controls, 'Power down', POWER_DOWN)
        ttk.Label(live_controls, textvariable=self.tof_state).pack(side='left', padx=12)
        ttk.Label(configuration, text='Time-of-flight sensor', font=('TkDefaultFont', 16, 'bold')).pack(anchor='w')
        ttk.Label(configuration, textvariable=self.tof_state).pack(anchor='w', pady=(8, 12))
        controls = ttk.Frame(configuration)
        controls.pack(anchor='w', pady=(0, 16))
        self.add_tof_button(controls, 'Start ToF', START)
        self.add_tof_button(controls, 'Stop ToF', STOP)
        self.add_tof_button(controls, 'Reset ToF', RESET)
        self.add_tof_button(controls, 'Power down', POWER_DOWN)
        self.add_tof_button(controls, 'Read from device', GET_CONFIG)
        form = ttk.LabelFrame(configuration, text='Ranging settings', padding=16)
        form.pack(fill='x')
        self.tof_period = tk.StringVar(value='10')
        self.tof_exposure = tk.StringVar(value='1')
        self.tof_binning = tk.StringVar(value='2')
        self.tof_power = tk.StringVar(value='Regular')
        self.tof_mode = tk.StringVar(value='Precision (no DSS)')
        self.tof_diagnostics = tk.StringVar(value='Waiting for sensor frame diagnostics…')
        ttk.Label(sensor, textvariable=self.tof_diagnostics, padding=(8, 0)).pack(fill='x')
        ttk.Label(configuration, textvariable=self.tof_diagnostics).pack(anchor='w', pady=8)
        for row, (label, variable, values, hint) in enumerate([
                ('Frame period (ms)', self.tof_period, None, '10–1000 ms (1–100 Hz)'),
                ('Exposure (ms)', self.tof_exposure, None, '1–30 ms; must fit within the frame period'),
                ('Binning', self.tof_binning, ['2', '4', '6', '8', '12', '24'], '2: 54×42 · 4: 24×20 · 6: 18×14 · 8: 12×10 · 12: 8×6 · 24: 4×4'),
                ('Power mode', self.tof_power, POWER_NAMES, ''),
                ('Ranging mode', self.tof_mode, RANGING_MODES, 'Precision: short context · Ambient: long context')]):
            ttk.Label(form, text=label).grid(row=row, column=0, sticky='w', pady=8, padx=(0, 24))
            widget = (ttk.Combobox(form, textvariable=variable, values=values, state='readonly', width=22)
                      if values else ttk.Entry(form, textvariable=variable, width=18))
            widget.grid(row=row, column=1, sticky='w', pady=8)
            ttk.Label(form, text=hint).grid(row=row, column=2, sticky='w', padx=18)
        actions = ttk.Frame(configuration)
        actions.pack(anchor='w', pady=16)
        self.add_tof_button(actions, 'Apply settings', SET_CONFIG)
        ttk.Label(configuration, text='Stop ToF before applying changes. Settings last until reboot. IMU and CPU reports continue.').pack(anchor='w')
        ttk.Label(configuration, text='Reset ToF restores compiled defaults and leaves ranging stopped; it also clears recoverable sensor faults.').pack(anchor='w', pady=(6, 0))
        ttk.Label(configuration, text='Power down holds XSHUT low. Use Reset ToF to wake the sensor, then Start ToF to range.').pack(anchor='w', pady=(6, 0))
        ttk.Label(configuration, textvariable=self.tof_command_status, wraplength=1000).pack(anchor='w', pady=16)
        self.build_firmware_tab(tabs, getattr(args, 'dfu_port', 50003))
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

    ## @brief Build the firmware update tab with image selection, progress, and persistent status.
    def build_firmware_tab(self, tabs, port):
        self.fw_tab = ttk.Frame(tabs, padding=20)
        tabs.add(self.fw_tab, text='Firmware update')
        ttk.Label(self.fw_tab, text='Application firmware', font=('TkDefaultFont', 16, 'bold')).pack(anchor='w')
        ttk.Label(self.fw_tab, text=(
            'Upload a packaged application to the inactive slot. The device verifies it before activation; '
            'a failed trial boot falls back to the previous image.'), wraplength=1000).pack(anchor='w', pady=(8, 12))
        self.fw_target = tk.StringVar()
        ttk.Label(self.fw_tab, textvariable=self.fw_target).pack(anchor='w')
        form = ttk.LabelFrame(self.fw_tab, text='New image', padding=12)
        form.pack(fill='x', pady=12)
        form.columnconfigure(1, weight=1)
        self.fw_image = tk.StringVar()
        self.fw_version = tk.StringVar(value='0')
        self.fw_port = tk.StringVar(value=str(port))
        self.fw_reset = tk.BooleanVar(value=True)
        ttk.Label(form, text='Application binary').grid(row=0, column=0, sticky='w', padx=(0, 16))
        image_box = ttk.Entry(form, textvariable=self.fw_image)
        image_box.grid(row=0, column=1, sticky='ew')
        browse = ttk.Button(form, text='Browse…', command=self.choose_firmware)
        browse.grid(row=0, column=2, padx=(8, 0))
        ttk.Label(form, text='Release identifier').grid(row=1, column=0, sticky='w', pady=8)
        version_box = ttk.Entry(form, textvariable=self.fw_version, width=16)
        version_box.grid(row=1, column=1, sticky='w', pady=8)
        ttk.Label(form, text='Unsigned 32-bit number').grid(row=1, column=2, sticky='w', padx=8)
        ttk.Label(form, text='Update TCP port').grid(row=2, column=0, sticky='w')
        update_port_box = ttk.Entry(form, textvariable=self.fw_port, width=16)
        update_port_box.grid(row=2, column=1, sticky='w')
        ttk.Label(form, text='Ethernet only; default 50003').grid(row=2, column=2, sticky='w', padx=8)
        reset_box = ttk.Checkbutton(form, variable=self.fw_reset, text='Reset after upload and verify boot confirmation')
        reset_box.grid(row=3, column=0, columnspan=3, sticky='w', pady=(12, 0))
        self.fw_inputs = [image_box, browse, version_box, update_port_box, reset_box]
        actions = ttk.Frame(self.fw_tab)
        actions.pack(fill='x', pady=(0, 12))
        self.fw_buttons = {}
        for action, label in [('upload', 'Upload image'), ('status', 'Read status'),
                              ('reset', 'Reset staged image'), ('abort', 'Abort incomplete upload'),
                              ('clear', 'Clear warnings')]:
            button = ttk.Button(actions, text=label, command=lambda action=action: self.firmware_action(action))
            button.pack(side='left', padx=(0, 8))
            self.fw_buttons[action] = button
        self.fw_cancel = ttk.Button(self.fw_tab, text='Cancel operation', state='disabled', command=self.cancel_firmware)
        self.fw_cancel.pack(anchor='w')
        self.fw_message = tk.StringVar(value='Read status to inspect the running and staged images.')
        ttk.Label(self.fw_tab, textvariable=self.fw_message, wraplength=1000).pack(anchor='w', pady=12)
        self.fw_progress = ttk.Progressbar(self.fw_tab, maximum=100)
        self.fw_progress.pack(fill='x')
        self.fw_progress_text = tk.StringVar(value='No upload in progress')
        ttk.Label(self.fw_tab, textvariable=self.fw_progress_text).pack(anchor='w', pady=(4, 12))
        status = ttk.LabelFrame(self.fw_tab, text='Status read from device', padding=12)
        status.pack(fill='x')
        self.fw_slots = tk.StringVar(value='Running: —    Confirmed: —    Pending: —')
        self.fw_versions = tk.StringVar(value='Release A: —    Release B: —')
        self.fw_flags = tk.StringVar(value='Upload and verification flags: not read')
        self.fw_warning = tk.StringVar(value='Warnings: not read')
        for variable in (self.fw_slots, self.fw_versions, self.fw_flags, self.fw_warning):
            ttk.Label(status, textvariable=variable, wraplength=1000).pack(anchor='w', pady=3)
        ttk.Label(self.fw_tab, text=(
            'Choose magnecko-sense-fw_Appli-signed.bin from a normal flash build. '
            'USB streaming pauses during updates and resumes afterward. '
            'Boot confirmation can take up to 60 seconds, including watchdog recovery.'),
            wraplength=1000).pack(anchor='w', pady=12)
        self.update_firmware()

    ## @brief Select the packaged STM32 application binary to upload.
    def choose_firmware(self):
        path = filedialog.askopenfilename(title='Select packaged application image',
                                          filetypes=[('Application binary', '*.bin'), ('All files', '*')])
        if path:
            self.fw_image.set(path)

    ## @brief Validate and return the selected firmware update transport and endpoint.
    def firmware_target(self):
        mode, endpoint = self.mode.get(), self.endpoint.get().strip()
        port = int(self.fw_port.get()) if mode == 'Ethernet' else 50003
        if mode not in ('USB', 'Ethernet') or not endpoint or not 1 <= port <= 65535:
            raise ValueError('Enter a host/serial port and a valid update TCP port.')
        return mode, endpoint, port

    ## @brief Start one background update action and hand off USB ownership when necessary.
    def firmware_action(self, action):
        if self.fw_worker is not None or self.closing:
            return
        try:
            mode, endpoint, port = self.firmware_target()
            version, image = 0, None
            if action == 'upload':
                version = int(self.fw_version.get(), 0)
                if not 0 <= version <= fw.NONE:
                    raise ValueError('Release identifier must be between 0 and 4294967295.')
                image = Path(self.fw_image.get()).expanduser()
                if not image.is_file():
                    raise ValueError('Select a packaged application binary first.')
            r = self.receiver
            if r and r.thread.is_alive() and (r.mode, r.endpoint) != (mode, endpoint):
                raise ValueError('Disconnect the current device before updating a different endpoint.')
            if r and r.thread.is_alive():
                with r.lock:
                    if r.pending_command:
                        raise ValueError('Wait for the sensor command acknowledgement before updating.')
            self.fw_resume = bool(mode == 'USB' and r and r.thread.is_alive() and not r.stop.is_set())
            self.fw_report = None
            self.fw_report_target = (mode, endpoint, port)
            self.fw_worker = UpdateWorker(mode, endpoint, port, action, image=image, version=version,
                                          reset=self.fw_reset.get(), receiver=r if mode == 'USB' else None)
            self.fw_worker.thread.start()
            self.update_firmware()
        except (ValueError, OSError) as error:
            self.fw_message.set(str(error))

    ## @brief Request upload cancellation or stop waiting for boot confirmation.
    def cancel_firmware(self):
        if self.fw_worker is not None:
            self.fw_worker.cancel.set()
            self.fw_message.set('Cancelling after the current device acknowledgement…')

    ## @brief Refresh update progress and status, then restore streaming after USB handoff.
    def update_firmware(self):
        worker = self.fw_worker
        if worker is not None:
            # Sample completion before taking the snapshot so a fast worker
            # cannot finish between an old snapshot and the completion check.
            finished = not worker.thread.is_alive()
            state = worker.snapshot()
            self.fw_message.set('Cancelling after the current device acknowledgement…'
                                if worker.cancel.is_set() and not finished else state['message'])
            if state['report'] is not None:
                self.fw_report = state['report']
            total, received = state['total'], state['received']
            self.fw_progress['value'] = 100 * received / total if total else 0
            self.fw_progress_text.set(f'{received:,} / {total:,} bytes ({100*received/total:.1f}%)'
                                      if total else 'No upload in progress')
            if finished:
                self.fw_worker = None
                resume, self.fw_resume = self.fw_resume, False
                if resume and not self.closing:
                    self.connect()
        busy = self.fw_worker is not None
        try:
            target = self.firmware_target()
            self.fw_target.set(f'Update device: {target[1]}' +
                               (f' · TCP {target[2]}' if target[0] == 'Ethernet' else ' · USB'))
        except ValueError:
            target = None
            self.fw_target.set('Select the update device using the connection controls above.')
        if target != self.fw_report_target and not busy:
            self.fw_report = None
        report = self.fw_report
        if report is not None:
            self.fw_slots.set(f'Running: {slot_name(report["running"])}    Confirmed: {slot_name(report["confirmed"])}'
                              f'    Pending: {slot_name(report["pending"])}')
            self.fw_versions.set(f'Release A: {report["version_a"]}    Release B: {report["version_b"]}')
            self.fw_flags.set('Uploaded: ' + ('yes' if report['flags'] & 2 else 'no') +
                              '    Verified: ' + ('yes' if report['flags'] & 4 else 'no') +
                              '    Boot confirmed: ' + ('yes' if report['flags'] & 8 else 'no'))
            warnings = [label.replace('_', ' ') for bit, label in fw.FLAGS.items()
                        if bit >= 16 and report['flags'] & bit]
            error = report['last_error']
            self.fw_warning.set('Warnings: ' + (', '.join(warnings) or 'none') +
                                (f' · Last error: {fw.RESULTS[error] if error < len(fw.RESULTS) else error}' if error else ''))
        else:
            self.fw_slots.set('Running: —    Confirmed: —    Pending: —')
            self.fw_versions.set('Release A: —    Release B: —')
            self.fw_flags.set('Upload and verification flags: not read')
            self.fw_warning.set('Warnings: not read')
        for widget, normal in self.connection_widgets:
            widget.configure(state='disabled' if busy or self.closing else normal)
        for widget in self.fw_inputs:
            widget.configure(state='disabled' if busy or self.closing else 'normal')
        for action, button in self.fw_buttons.items():
            enabled = not busy and not self.closing and target is not None
            if action == 'upload':
                enabled = enabled and bool(self.fw_image.get().strip())
            elif action == 'reset':
                enabled = enabled and report is not None and report['pending'] in (0, 1)
            elif action == 'abort':
                enabled = enabled and report is not None and bool(report['flags'] & 1)
            elif action == 'clear':
                enabled = enabled and report is not None and bool(report['flags'] & 0x1f0)
            button.configure(state='normal' if enabled else 'disabled')
        self.fw_cancel.configure(state='normal' if busy and not self.closing and
                                 not self.fw_worker.cancel.is_set() else 'disabled')

    ## @brief Add an initially disabled sensor command button to the selected parent.
    def add_tof_button(self, parent, label, operation):
        button = ttk.Button(parent, text=label, state='disabled', command=lambda: self.sensor_command(operation))
        button.pack(side='left', padx=(0, 8))
        self.tof_buttons.append((button, operation))

    ## @brief Validate the displayed settings and queue the requested sensor operation.
    def sensor_command(self, operation):
        try:
            if self.fw_worker is not None:
                raise ValueError('Wait for the firmware update operation to finish.')
            config = None
            if operation == SET_CONFIG:
                microseconds = Decimal(self.tof_period.get()) * 1000
                if not microseconds.is_finite() or microseconds != microseconds.to_integral_value():
                    raise ValueError('Frame period must be a whole number of microseconds')
                config = dict(period_us=int(microseconds), exposure_ms=int(self.tof_exposure.get()),
                              binning=int(self.tof_binning.get()), power=POWER_NAMES.index(self.tof_power.get()),
                              context=int(self.tof_mode.get() == 'Ambient'),
                              dss_enabled=self.tof_mode.get() != 'Precision (no DSS)')
            if self.receiver is None:
                raise ValueError('Connect to a device first')
            self.receiver.send_command(operation, config)
        except (ValueError, InvalidOperation) as error:
            self.tof_command_status.set(str(error))
            if self.receiver:
                with self.receiver.lock:
                    self.receiver.command_message = str(error)

    ## @brief Display acknowledged sensor state and update the available controls.
    def show_sensor_status(self, report, settings, received, connected, pending, message):
        self.tof_command_status.set(message)
        state = report['state'] if report else None
        if report:
            age = time.monotonic() - received
            text = f"ToF: {STATE_NAMES[state]} · {report['width']}×{report['height']} CSI · {report['frames']} frames"
            if report['error']:
                text += f" · error {report['error']}"
            if age > 3:
                text += f' · status {age:.1f}s old'
            self.tof_state.set(text)
        else:
            self.tof_state.set('ToF: waiting for device status' if connected else 'ToF: disconnected')
        if settings is not None and settings is not self.last_tof_settings:
            self.tof_period.set(f"{settings['period_us']/1000:g}")
            self.tof_exposure.set(str(settings['exposure_ms']))
            self.tof_binning.set(str(settings['binning']))
            self.tof_power.set(POWER_NAMES[settings['power']])
            self.tof_mode.set(ranging_mode(settings['context'], settings.get('dss_enabled', True)))
        self.last_tof_settings = settings
        for button, operation in self.tof_buttons:
            enabled = connected and not pending and (operation == GET_CONFIG or
                      (operation == RESET and state in (1, 2, 3, 4)) or
                      (operation == POWER_DOWN and state in (1, 2, 3)) or
                      (operation in (START, SET_CONFIG) and state == 1) or (operation == STOP and state == 2))
            button.configure(state='normal' if enabled else 'disabled')

    ## @brief Start a receiver for the selected endpoint after validating connection settings.
    def connect(self):
        if self.fw_worker is not None or self.closing:
            return
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
        self.show_profile(None, None)
        self.show_sensor_status(None, None, None, False, False, 'Waiting for sensor status…')

    ## @brief Request that the current receiver stop and close its connection.
    def disconnect(self):
        self.fw_resume = False
        self.cancel_firmware()
        if self.receiver:
            self.receiver.stop.set()

    ## @brief Save the latest device parameter packet as a JSON file.
    def save_metadata(self):
        if self.metadata is None:
            self.status.set('No parameter packet received yet')
            return
        path = filedialog.asksaveasfilename(defaultextension='.json', filetypes=[('JSON', '*.json')])
        if path:
            with open(path, 'w', encoding='utf-8') as file:
                json.dump(self.metadata, file, indent=2, ensure_ascii=False)

    ## @brief Recursively insert parameter objects and values into the parameter tree.
    def populate(self, parent, data):
        for key, value in data.items():
            if isinstance(value, dict):
                item = self.tree.insert(parent, 'end', text=key, open=True)
                self.populate(item, value)
            else:
                self.tree.insert(parent, 'end', text=key, values=(json.dumps(value, ensure_ascii=False),))

    ## @brief Refresh the CPU chart and table with the latest profiling interval.
    def show_profile(self, profile, received):
        if profile is not self.last_profile:
            self.profile_chart.show(profile)
        if profile is None:
            self.profile_status.set('Waiting for profiler data…')
            self.cpu_busy['value'] = 0
            if self.profile_tree.get_children():
                self.profile_tree.delete(*self.profile_tree.get_children())
        else:
            idle = 100 * profile['idle_cycles'] / profile['interval_cycles']
            duration = 1000 * profile['interval_cycles'] / profile['clock_hz']
            age = time.monotonic() - received
            suffix = ' · STALE' if age > 3 else ''
            if profile['truncated']:
                suffix += ' · thread list truncated'
            self.profile_status.set(f'CPU busy {100-idle:.1f}% · Idle {idle:.1f}% · Interval {duration:.1f} ms · Report age {age:.1f}s{suffix}')
            self.cpu_busy['value'] = 100 - idle
            if profile is not self.last_profile:
                self.profile_tree.delete(*self.profile_tree.get_children())
                for key, name, priority, ms, percent in profile_rows(profile):
                    self.profile_tree.insert('', 'end', iid=key,
                                             values=(name, priority, f'{ms:.3f}', f'{percent:.2f}'))
        self.last_profile = profile

    ## @brief Refresh connection status, plots, parameters, and sensor controls from receiver state.
    def update(self):
        self.update_firmware()
        if self.closing and self.fw_worker is None:
            self.root.destroy()
            return
        r = self.receiver
        if r:
            with r.lock:
                status, total, lidar, imu, gaps = r.status, r.total, r.lidar_count, r.imu_count, r.gaps
                depth, samples, metadata = r.depth, list(r.samples), r.metadata
                profile, profile_received = r.profile, r.profile_received
                tof, settings, tof_received = r.tof_status, r.tof_settings, r.tof_received
                pending, command_message = r.pending_command, r.command_message
                sensor_meta, sensor_received, sensor_fps = r.sensor_metadata, r.sensor_metadata_received, r.sensor_timing.fps
                discarded, invalid, last_packet, session = r.discarded, r.invalid, r.last_packet, r.session
            if sensor_meta:
                mode = ranging_mode(sensor_meta['context'], sensor_meta['dss_mode'] != 0)
                fps = 'measuring' if sensor_fps is None else f'{sensor_fps:.1f} fps (estimated)'
                age = time.monotonic() - sensor_received
                self.tof_diagnostics.set(f"Sensor {fps} · die {sensor_meta['temperature_c']} °C · {mode} · "
                                         f"reported period {sensor_meta['period_us']/1000:g} ms · "
                                         f"sensor frame {sensor_meta['counter']} · error 0x{sensor_meta['error']:04x} · last frame {age:.1f}s ago")
            else:
                self.tof_diagnostics.set('Waiting for sensor frame diagnostics…')
            self.show_profile(profile, profile_received)
            self.show_sensor_status(tof, settings, tof_received, status == 'Connected' and self.fw_worker is None,
                                    pending, command_message)
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

    ## @brief Stop the receiver and destroy the viewer window.
    def close(self):
        self.closing = True
        self.disconnect()
        if self.fw_worker is None:
            self.root.destroy()


## @brief Parse viewer options and start the Tk device monitor.
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    transport = parser.add_mutually_exclusive_group()
    transport.add_argument('--host', default='192.168.8.20')
    transport.add_argument('--port', help='USB CDC serial device, e.g. /dev/ttyACM0')
    parser.add_argument('--tcp-port', type=int, default=50002)
    parser.add_argument('--dfu-port', type=int, default=50003, help='Firmware update TCP port')
    parser.add_argument('--depth-max', type=float, default=4000)
    parser.add_argument('--connect', action='store_true', help='Connect immediately on launch')
    args = parser.parse_args()
    root = tk.Tk()
    ttk.Style(root).theme_use('clam')
    Viewer(root, args)
    root.mainloop()


if __name__ == '__main__':
    main()
