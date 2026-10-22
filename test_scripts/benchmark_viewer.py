#!/usr/bin/env python3
"""Compare the former full-redraw viewer with cached rendering, without a device/display."""
import argparse
import gc
import struct
import time

import numpy as np
from matplotlib.backends.backend_agg import FigureCanvasAgg

from USB_Debug import Packet, decode_depth
from Viewer_Plots import SensorPlots


def depth_frame(sequence=0, binning=2):
    from USB_Debug import RAW_GRID_SIZES
    width, height = RAW_GRID_SIZES[binning]
    zones = width * height
    distances = (np.arange(zones, dtype='<u2') * 17 + sequence) % 4000
    row = bytearray(100)
    struct.pack_into('<H', row, 58, binning << 6)
    payload = distances.tobytes() + bytes(14800 - zones * 2) + row
    return Packet(sequence, 100, 149, bytes(payload))


def imu_samples(frame=0, count=1000):
    times = np.linspace(0, 10, count) + frame * .05
    data = np.sin(times[:, None] * np.arange(1, 11)[None, :])
    data[:, :3] *= 1000
    data[:, 3:6] *= 100000
    return [(float(t), (i, *row)) for i, (t, row) in enumerate(zip(times, data))]


def full_redraw(plots, canvas, depth, samples):
    """Original Board_Viewer update path, including per-frame autoscaling."""
    values, _ = decode_depth(depth)
    plots.depth_image.set_data(np.ma.masked_equal(values, 0))
    h, w = values.shape
    plots.depth_image.set_extent((-.5, w - .5, h - .5, -.5))
    plots.depth_ax.set_xlim(-.5, w - .5)
    plots.depth_ax.set_ylim(h - .5, -.5)
    plots.depth_ax.set_title(f'Lidar {w} × {h} · depth (mm) · seq {depth.sequence}')
    times = np.array([s[0] for s in samples])
    data = np.array([s[1][1:] for s in samples])
    for ax, lines, offset, scale in zip(plots.axes, plots.lines, (0, 3, 6), (1, .001, 1)):
        for j, line in enumerate(lines):
            line.set_data(times - times[-1], data[:, offset + j] * scale)
        ax.relim()
        ax.autoscale_view(scalex=False)
    canvas.draw()


def benchmark(frames=100, width=1100, height=600):
    # Build fixtures before timing; compare identical frames at identical sizes.
    fixtures = [(depth_frame(i), imu_samples(i)) for i in range(frames + 5)]
    result = {}
    for mode in ('full redraw', 'cached rendering'):
        plots = SensorPlots()
        plots.figure.set_size_inches(width / 100, height / 100)
        canvas = FigureCanvasAgg(plots.figure)
        if mode == 'cached rendering':
            plots.attach(canvas)
            render = lambda depth, samples: plots.update(depth, samples)
        else:
            render = lambda depth, samples: full_redraw(plots, canvas, depth, samples)
        wall_times, cpu_times = [], []
        for i, (depth, samples) in enumerate(fixtures):
            wall, cpu = time.perf_counter(), time.process_time()
            render(depth, samples)
            elapsed_cpu = time.process_time() - cpu
            elapsed_wall = time.perf_counter() - wall
            if i >= 5:
                wall_times.append(elapsed_wall * 1000)
                cpu_times.append(elapsed_cpu * 1000)
        result[mode] = (float(np.median(wall_times)), float(np.percentile(wall_times, 95)),
                        float(np.median(cpu_times)))
        del plots, canvas, render
        gc.collect()
    print(f'{width}×{height}, {frames} frames, 54×42 depth, 1000 IMU samples, Agg (excludes Tk display transfer)')
    for mode, (median, p95, cpu) in result.items():
        print(f'{mode:18s}: median {median:7.2f} ms, p95 {p95:7.2f} ms, CPU {cpu:7.2f} ms/frame')
    print(f'Median rendering speedup: {result["full redraw"][0] / result["cached rendering"][0]:.2f}×')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--frames', type=int, default=100)
    parser.add_argument('--width', type=int, default=1100)
    parser.add_argument('--height', type=int, default=600)
    args = parser.parse_args()
    if min(args.frames, args.width, args.height) <= 0:
        parser.error('frames, width and height must be positive')
    benchmark(args.frames, args.width, args.height)
