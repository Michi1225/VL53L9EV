"""Rendering correctness and cache invalidation without a display or board."""
import unittest
import struct
from types import SimpleNamespace
from unittest.mock import patch

import numpy as np
from matplotlib.backends.backend_agg import FigureCanvasAgg

from benchmark_viewer import depth_frame, imu_samples
from USB_Debug import Packet
from Viewer_Plots import SensorPlots


class SensorPlotTests(unittest.TestCase):
    def make_plots(self, canvas_type=FigureCanvasAgg):
        plots = SensorPlots()
        canvas = canvas_type(plots.figure)
        plots.attach(canvas)
        self.addCleanup(plots.figure.clear)
        return plots, canvas

    def assert_matches_full_draw(self, plots, canvas):
        actual = np.asarray(canvas.buffer_rgba()).copy()
        plots.renderer.enabled = False
        for artist in plots.renderer.artists:
            artist.set_animated(False)
        canvas.draw()
        np.testing.assert_array_equal(actual, np.asarray(canvas.buffer_rgba()))

    def test_zero_returns_are_gray_and_small_positive_distances_keep_their_colors(self):
        plots, canvas = self.make_plots()
        frame = depth_frame()
        payload = bytearray(frame.payload)
        # Bit 15 selects a ranging step; it must not hide positive distances.
        struct.pack_into('<5H', payload, 0, 0, 1, 0x8000, 0x8005, 32767)
        frame = Packet(frame.sequence, frame.width, frame.height, bytes(payload))
        plots.update(frame)
        colors = plots.depth_image.to_rgba(plots.depth_image.get_array())
        gray = np.array([1 / 3, 1 / 3, 1 / 3, 1.])
        for col in (0, 2):
            np.testing.assert_allclose(colors[0, col], gray)
        for col in (1, 3, 4):
            self.assertFalse(np.ma.is_masked(plots.depth_values[0, col]))
            self.assertFalse(np.allclose(colors[0, col], gray))
            self.assertFalse(np.allclose(colors[0, col], np.ones(4)))
        self.assertIn('Zero returns: 2/2268', plots.depth_note.get_text())
        self.assertIn('valid range: 1–32767 mm', plots.depth_note.get_text())
        self.assert_matches_full_draw(plots, canvas)

    def test_hover_reports_received_zero_and_updates_without_extra_draws(self):
        plots, canvas = self.make_plots()
        frame = depth_frame()
        plots.update(frame)
        event = SimpleNamespace(inaxes=plots.depth_ax, xdata=0, ydata=0)
        with patch.object(plots.renderer, 'draw', wraps=plots.renderer.draw) as draw:
            plots.hover(event)
            self.assertIn('received 0 mm (masked / no return)', plots.depth_note.get_text())
            self.assertEqual(draw.call_count, 0, 'hover must use the regular display tick')
            plots.update(frame)
            self.assertEqual(draw.call_count, 1)
        plots.update(depth_frame(1))
        self.assertIn('Column 0, row 0: received 1 mm', plots.depth_note.get_text())
        plots.hover(SimpleNamespace(inaxes=None, xdata=None, ydata=None))
        self.assertIn('Point to a pixel', plots.depth_note.get_text())
        plots.update(None)
        self.assertIsNone(plots.depth_values)
        self.assertIn('No depth frame', plots.depth_note.get_text())
        self.assert_matches_full_draw(plots, canvas)

    def test_cached_updates_match_full_render_without_ghost_traces(self):
        plots, canvas = self.make_plots()
        plots.update(depth_frame(), imu_samples())
        with patch.object(canvas, 'draw', wraps=canvas.draw) as draw:
            for i in range(1, 4):
                plots.update(depth_frame(i), imu_samples(i))
            self.assertEqual(draw.call_count, 0, 'stable data must reuse cached axes')
        self.assert_matches_full_draw(plots, canvas)

    def test_unchanged_data_does_not_render_and_stop_clears_once(self):
        plots, canvas = self.make_plots()
        depth = depth_frame()
        plots.update(depth, imu_samples())
        with patch.object(plots.renderer, 'draw', wraps=plots.renderer.draw) as draw:
            plots.update(depth)
            self.assertEqual(draw.call_count, 0)
            plots.update(None, [])
            self.assertEqual(draw.call_count, 1)
            plots.update(None, [])
            self.assertEqual(draw.call_count, 1)
        self.assertTrue(np.all(np.ma.getmaskarray(plots.depth_image.get_array())))
        self.assertTrue(all(len(line.get_xdata()) == 0 for lines in plots.lines for line in lines))
        self.assert_matches_full_draw(plots, canvas)

    def test_binning_resize_and_expose_rebuild_background(self):
        plots, canvas = self.make_plots()
        plots.update(depth_frame(), imu_samples())
        for binning, shape in ((4, (24, 24)), (8, (10, 12)), (2, (42, 54))):
            plots.update(depth_frame(binning=binning))
            self.assertEqual(plots.depth_image.get_array().shape, shape)
            self.assertEqual(plots.depth_ax.get_xlim(), (-.5, shape[1] - .5))
        canvas.figure.set_size_inches(9, 5)
        plots.invalidate()
        plots.update(plots.last_depth)
        # A native expose/resize draw must also include animated data.
        canvas.draw()
        self.assert_matches_full_draw(plots, canvas)

    def test_axis_expands_for_new_peak_and_shrinks_after_quiet_period(self):
        plots, _ = self.make_plots()
        samples = imu_samples()
        plots.update(None, samples, now=10)
        before = plots.axes[0].get_ylim()
        spike = list(samples)
        stamp, row = spike[-1]
        spike[-1] = (stamp, (row[0], 12000, *row[2:]))
        plots.update(None, spike, now=10.05)
        self.assertGreater(plots.axes[0].get_ylim()[1], 12000)
        wide = plots.axes[0].get_ylim()
        plots.update(None, samples, now=10.1)
        self.assertEqual(plots.axes[0].get_ylim(), wide)
        plots.update(None, samples, now=12.1)
        np.testing.assert_allclose(plots.axes[0].get_ylim(), before)

    def test_invalid_depth_clears_old_image_and_recovers(self):
        plots, canvas = self.make_plots()
        plots.update(depth_frame())
        plots.update(Packet(2, 1, 1, b'x'))
        self.assertTrue(np.all(np.ma.getmaskarray(plots.depth_image.get_array())))
        self.assertIn('Invalid depth', plots.depth_ax.get_title())
        plots.update(depth_frame(3))
        self.assertIn('seq 3', plots.depth_ax.get_title())
        self.assert_matches_full_draw(plots, canvas)

    def test_non_blitting_backend_falls_back_to_full_render(self):
        class FullDrawCanvas(FigureCanvasAgg):
            supports_blit = False

        plots, canvas = self.make_plots(FullDrawCanvas)
        with patch.object(canvas, 'draw', wraps=canvas.draw) as draw:
            plots.update(depth_frame(), imu_samples())
            plots.update(depth_frame(1), imu_samples(1))
            self.assertEqual(draw.call_count, 2)
        self.assert_matches_full_draw(plots, canvas)


if __name__ == '__main__':
    unittest.main()
