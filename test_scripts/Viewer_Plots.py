"""Retained live plots; usable with TkAgg or Agg for headless benchmarks."""
import time

import numpy as np
from matplotlib import colormaps
from matplotlib.figure import Figure

from USB_Debug import decode_depth


class BlitRenderer:
    """Cache static decorations and redraw only the changing artists."""

    def __init__(self, canvas, artists):
        self.canvas = canvas
        self.artists = artists
        self.background = None
        self.enabled = canvas.supports_blit
        for artist in artists:
            artist.set_animated(self.enabled)
        canvas.mpl_connect('draw_event', self._on_draw)
        canvas.mpl_connect('resize_event', self.invalidate)

    def invalidate(self, event=None):
        self.background = None

    def _draw_artists(self):
        for artist in self.artists:
            if artist.get_visible():
                artist.axes.draw_artist(artist)

    def _on_draw(self, event):
        if self.enabled:
            self.background = self.canvas.copy_from_bbox(self.canvas.figure.bbox)
            self._draw_artists()

    def draw(self):
        if not self.enabled or self.background is None:
            # Synchronous drawing avoids accumulating idle redraws when the
            # receiver is faster than the display. draw_event captures the cache.
            self.canvas.draw()
        else:
            self.canvas.restore_region(self.background)
            self._draw_artists()
            self.canvas.blit(self.canvas.figure.bbox)


class SensorPlots:
    """Latest depth frame and bounded IMU history, with stable axis limits."""

    def __init__(self, depth_max=4000):
        self.figure = Figure(figsize=(11, 6), dpi=100)
        grid = self.figure.add_gridspec(3, 2)
        self.depth_ax = self.figure.add_subplot(grid[:, 0])
        self.depth_ax.set_title('Lidar radial depth (mm)')
        cmap = colormaps['turbo'].with_extremes(bad='#555555')
        self.depth_image = self.depth_ax.imshow(
            np.full((42, 54), np.nan), vmin=0, vmax=depth_max,
            cmap=cmap, interpolation='nearest')
        self.depth_values = None
        self.hover_cell = None
        self.depth_summary = 'No depth frame'
        self.depth_note = self.depth_ax.text(
            0, -.13, '', transform=self.depth_ax.transAxes, va='top', fontsize=8)
        self._update_depth_note()
        self.figure.colorbar(self.depth_image, ax=self.depth_ax, label='mm', shrink=.7)
        self.axes = [self.figure.add_subplot(grid[i, 1]) for i in range(3)]
        self.lines = []
        for ax, title, labels in zip(
                self.axes, ['Acceleration (mg)', 'Gyroscope (°/s)', 'SFLP quaternion'],
                [('X', 'Y', 'Z'), ('X', 'Y', 'Z'), ('X', 'Y', 'Z', 'W')]):
            ax.set_title(title, loc='left')
            ax.set_xlim(-10, 0)
            ax.grid(alpha=.2)
            self.lines.append([ax.plot([], [], label=label)[0] for label in labels])
            # Keep legends outside the data rectangle so they can be cached
            # without traces painting over them (or redrawing text each frame).
            ax.legend(loc='lower right', bbox_to_anchor=(1, 1.04), borderaxespad=0,
                      ncol=len(labels), fontsize=8)
        self.axes[-1].set_xlabel('Seconds relative to latest sample')
        self.figure.tight_layout()
        self.renderer = None
        self.last_depth = None
        self.have_samples = False
        self.limits_initialized = [False] * 3
        self.last_scale = [0.] * 3
        self.dirty = True

    def attach(self, canvas):
        artists = [self.depth_image, self.depth_ax.title, self.depth_note]
        artists.extend(self.depth_ax.spines.values())
        for ax, lines in zip(self.axes, self.lines):
            artists.extend(lines)
            artists.extend(ax.spines.values())
        self.renderer = BlitRenderer(canvas, artists)
        canvas.mpl_connect('motion_notify_event', self.hover)

    def _update_depth_note(self):
        pixel = 'Point to a pixel to inspect its received distance'
        if self.hover_cell is not None and self.depth_values is not None:
            col, row = self.hover_cell
            value = int(np.ma.getdata(self.depth_values)[row, col])
            suffix = ' (masked / no return)' if value == 0 else ''
            pixel = f'Column {col}, row {row}: received {value} mm{suffix}'
        self.depth_note.set_text(
            f'{self.depth_summary}\nGray = received distance 0 mm (masked)\n{pixel}')

    def hover(self, event):
        cell = None
        if (event.inaxes is self.depth_ax and self.depth_values is not None and
                event.xdata is not None and event.ydata is not None):
            col, row = int(np.floor(event.xdata + .5)), int(np.floor(event.ydata + .5))
            h, w = self.depth_values.shape
            if 0 <= col < w and 0 <= row < h:
                cell = (col, row)
        if cell != self.hover_cell:
            self.hover_cell = cell
            self._update_depth_note()
            # The regular display tick presents hover changes at the selected
            # FPS; pointer motion never queues extra full-figure redraws.
            self.dirty = True

    def invalidate(self):
        if self.renderer:
            self.renderer.invalidate()
        self.dirty = True

    def _limits(self, index, values, now):
        finite = values[np.isfinite(values)]
        if not finite.size:
            return
        low, high = float(finite.min()), float(finite.max())
        ax = self.axes[index]
        bottom, top = ax.get_ylim()
        span = high - low
        # Expand immediately; shrink at most every two seconds. Small sensor
        # noise no longer rebuilds ticks and invalidates the cache each frame.
        shrink = span < (top - bottom) * .45 and now - self.last_scale[index] >= 2
        if not self.limits_initialized[index] or low < bottom or high > top or shrink:
            pad = max(span * .15, max(abs(low), abs(high)) * .02, .001)
            new_limits = (low - pad, high + pad)
            if not np.allclose((bottom, top), new_limits):
                ax.set_ylim(*new_limits)
                self.invalidate()
            self.limits_initialized[index] = True
            self.last_scale[index] = now

    def update(self, depth, samples=None, now=None):
        """None samples means unchanged; an empty list clears the IMU traces."""
        now = time.monotonic() if now is None else now
        if depth is not self.last_depth:
            if depth is None:
                self.depth_values = None
                self.hover_cell = None
                self.depth_summary = 'No depth frame'
                self.depth_image.set_data(np.full((42, 54), np.nan))
                self.depth_ax.set_title('Lidar radial depth (mm)')
            else:
                try:
                    values, _ = decode_depth(depth)
                    if self.depth_values is None or self.depth_values.shape != values.shape:
                        self.hover_cell = None
                    self.depth_values = values
                    valid = values.compressed()
                    zeros = values.size - valid.size
                    limits = f'{valid.min():.0f}–{valid.max():.0f} mm' if valid.size else 'none'
                    self.depth_summary = f'Zero returns: {zeros}/{values.size} · valid range: {limits}'
                    self.depth_image.set_data(values)
                    h, w = values.shape
                    extent = (-.5, w - .5, h - .5, -.5)
                    if tuple(self.depth_image.get_extent()) != extent:
                        self.depth_image.set_extent(extent)
                        self.depth_ax.set_xlim(-.5, w - .5)
                        self.depth_ax.set_ylim(h - .5, -.5)
                        self.invalidate()
                    self.depth_ax.set_title(f'Lidar {w} × {h} · depth (mm) · seq {depth.sequence}')
                except (ValueError, TypeError) as error:
                    self.depth_values = None
                    self.hover_cell = None
                    self.depth_summary = 'Invalid depth frame; no distances available'
                    self.depth_image.set_data(np.full((42, 54), np.nan))
                    self.depth_ax.set_title(f'Invalid depth: {error}')
            self._update_depth_note()
            self.last_depth = depth
            self.dirty = True
        if samples:
            times = np.fromiter((s[0] for s in samples), dtype=float, count=len(samples))
            data = np.asarray([s[1][1:] for s in samples])
            x = times - times[-1]
            visible = x >= -10
            x, data = x[visible], data[visible]
            for i, (lines, offset, scale) in enumerate(zip(self.lines, (0, 3, 6), (1, .001, 1))):
                values = data[:, offset:offset + len(lines)] * scale
                for j, line in enumerate(lines):
                    line.set_data(x, values[:, j])
                self._limits(i, values, now)
            self.have_samples = self.dirty = True
        elif samples is not None and self.have_samples:
            for lines in self.lines:
                for line in lines:
                    line.set_data([], [])
            self.have_samples = False
            self.limits_initialized = [False] * 3
            self.dirty = True
        if self.dirty and self.renderer:
            self.renderer.draw()
            self.dirty = False
