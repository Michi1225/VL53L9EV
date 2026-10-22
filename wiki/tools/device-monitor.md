# Device Monitor

[Wiki home](../README.md) · [Full contents](../SUMMARY.md)

## Contents

- [Installation](#installation)
- [Connecting](#connecting)
- [Live sensors](#live-sensors)
- [Viewer performance](#viewer-performance)
- [Sensor controls](#sensor-controls)
- [Firmware updates](#firmware-updates)
- [Device and firmware parameters](#device-and-firmware-parameters)
- [CPU profiler](#cpu-profiler)
- [Profiling implementation](#profiling-implementation)
- [Parameter packets and timestamps](#parameter-packets-and-timestamps)
- [Sensor diagnostics](#sensor-diagnostics)

## Installation

Ubuntu/Debian dependencies (use Python with Tk support):

```sh
sudo apt install python3-tk python3-pil.imagetk
python3 -m pip install numpy matplotlib pyserial Pillow
```

Run the viewer without sudo so it uses your user's Python packages and display
session. Ethernet connections do not need root privileges.

## Connecting

Launch from VS Code task **Monitor: Board Viewer**, or:

```sh
python3 test_scripts/Board_Viewer.py --host 192.168.8.20 --connect
python3 test_scripts/Board_Viewer.py --port /dev/ttyACM0 --connect
```

Select USB/Ethernet, enter the serial device or host and press Connect. The viewer
reconnects automatically after a link interruption. To switch endpoints, press
Disconnect and wait briefly before Connect. Close other programs using that USB
serial port or TCP streaming connection; the board supports one TCP stream client.

## Live sensors

The top bar shows measured receive throughput (Mbit/s and KiB/s), lidar FPS, IMU
Hz, received bytes, missing sequence numbers and parser errors. These are host
measurements, not the PHY's negotiated link speed. Live sensors shows the decoded
lidar zone grid and radial depths in mm, accelerometer in mg, gyro in degrees/s,
and SFLP quaternion XYZW. `--depth-max` controls the heatmap range in mm.

## Viewer performance

The live display defaults to **20 FPS**. Use `--fps` (1–60) to change the display
limit; 10 FPS reduces CPU use on battery, while 30 FPS provides smoother motion:

```sh
python3 test_scripts/Board_Viewer.py --host 192.168.8.20 --connect --fps 10
```

Reception runs independently of the display rate. The viewer renders the latest
depth frame and up to 1000 IMU samples within the latest ten seconds, without
queuing old frames for display. Packet counters and sensor frame-rate estimates
continue at the incoming rate. Hidden live plots and the hidden CPU donut do not
redraw; returning to a tab shows the latest data. Minimizing pauses plot updates.
Unchanged telemetry does not trigger a plot redraw.

The renderer caches axes, ticks, grids, legends and the colorbar, then updates
the heatmap and traces using Matplotlib blitting. IMU scales expand immediately
for new peaks and shrink at most every two seconds, avoiding a full redraw for
small fluctuations. Resizing and binning changes rebuild the cache automatically.
Status and control widgets refresh separately at 5 Hz. Existing Python packages
are sufficient; no additional GUI toolkit or compiler is needed.

Compare the former full-redraw path with the cached renderer on your machine:

```sh
python3 test_scripts/benchmark_viewer.py
python3 test_scripts/benchmark_viewer.py --width 1600 --height 900
```

The benchmark uses synthetic 54×42 depth frames and 1000 IMU samples, reports
median/p95 frame time and CPU time, and needs neither a board nor a display.
It measures Agg rendering and excludes Tk's transfer of pixels to the display;
actual GUI performance also depends on window size and display scaling.

## Sensor controls

Start ToF / Stop ToF / Reset ToF controls are available above Live sensors and in the
Sensor configuration tab. The tab reads the board's actual settings and lets
you edit frame period, exposure, binning, power mode and range context. Stop
ranging, wait for acknowledgement, apply settings, then Start ToF. Configuration
changes are volatile until reboot; IMU, CPU and metadata reports continue while
ToF is stopped. Read from device refreshes the edit fields; periodic status
updates do not overwrite unsaved edits. Results and latched sensor faults appear
below the controls. Reset ToF resets only the sensor, reloads its firmware and
compiled defaults, clears a recoverable fault and leaves ranging stopped; press
Start afterward. Runtime edits are discarded and the form reloads the confirmed
default settings. IMU, profiling and the connection stay active. A failed reset
reports an error and leaves the fault latched. Buttons wait for device acknowledgements, and connection loss
clears pending commands without replaying them. A timeout leaves the outcome
uncertain; read current state before retrying.

The commands use the same USB serial connection or TCP streaming socket (50002),
so no second serial owner or control connection is required. Rebuild/flash the
updated application to enable controls. Older firmware can still be viewed but
cannot acknowledge these commands. See the [wire protocol README](../protocols/wire-protocol.md)
for exact headers, payload offsets, result codes and byte examples.

## Firmware updates

Open **Firmware update** to update the application over the selected USB or
Ethernet connection. Install the [DFU-capable FSBL and application baseline](../firmware/firmware-updates.md#first-installation)
once using SWD before using this tab.

1. Select the device in the connection bar. Ethernet uses the separate update
   TCP port, **50003** by default; the normal streaming port remains **50002**.
   The update port is editable in the tab or through `--dfu-port`.
2. Choose **Read status** to inspect running, confirmed, and pending slots,
   release identifiers, upload/verification flags, and persistent warnings.
3. Browse to `Appli/build/magnecko-sense-fw_Appli-signed.bin` from a normal flash build
   and enter its unsigned 32-bit release identifier. Raw application binaries,
   corrupt images, and images marked `DEV_MODE` in their manifest are rejected.
4. Press **Upload image**. The progress bar advances after each acknowledged
   chunk. The device chooses the inactive slot and verifies the entire image.
5. With **Reset after upload and verify boot confirmation** selected, the viewer
   requests one reset and queries status for up to 60 seconds. It reports either
   confirmed activation, fallback to the previous image, or an unconfirmed
   outcome requiring another status read. Uncheck the option to leave the image
   staged; **Reset staged image** activates it later.

The window stays responsive during transfers. USB streaming pauses until the
updater releases the serial port, then reconnects automatically if it was active
before the operation. Ethernet streaming can continue on its own socket. Device
selection and sensor commands are locked during an update operation. Disconnect
cancels the operation and prevents automatic USB reconnection.

**Cancel operation** waits for the current acknowledgement, then aborts an
unfinished upload. An image already verified remains staged. During boot
monitoring, cancellation stops status polling without sending another reset.
**Abort incomplete upload** clears an unfinished transfer shown by Read status;
**Clear warnings** explicitly clears persistent diagnostics. A failed or lost
acknowledgement is never blindly replayed: read status before retrying.

If USB re-enumerates under a different device name after reset, select the new
serial port and read status. A stable `/dev/serial/by-id/` path avoids this on
systems that provide one. Upload verification and successful boot are displayed
separately. The parameter JSON tab retains the last SWD-generated provenance;
current A/B update state and release identifiers are shown in Firmware update.

## Device and firmware parameters

Device & firmware parameters shows the complete flash JSON in an expandable tree,
including serial/revisions, compiler and CMake versions, build options, compile
flags/definitions, Git/image hashes and per-slot flash records. Save received
JSON exports it. Every persistent flash refreshes these records automatically;
see [parameter management](../firmware/parameters.md) for identity preservation and
explicit settings updates.
An unprogrammed/unreadable parameter block is reported as `parameter_error`.

## CPU profiler

CPU profiler shows every ThreadX thread (including middleware threads), its
priority, CPU milliseconds and percentage of the latest reporting interval.
Idle, interrupts and scheduler overhead have separate rows. The busy meter is
100% minus idle; sleeping or blocked threads consume no CPU time. Reports arrive
about once per second over either transport. The actual interval is displayed
because slow transports can delay a report. Report age and a stale indicator
make stopped updates visible. Reconnecting clears the previous session's data.
The live donut chart beside the table shows CPU shares with consistent colors
and overall CPU usage in the center. Hover over a slice for its time and exact
percentage. Small slices (below 1%, or beyond the seven largest activities) are
combined as Other activities; the table keeps every thread's individual values.
Drag the divider to resize the chart and table.

## Profiling implementation

Firmware uses the Cortex-M55 DWT cycle counter and ThreadX execution hooks,
with a small idle-entry hook in `tx_thread_schedule.S`. Peripheral ISRs and the
ThreadX tick bracket interrupt processing, including nested interrupts. The
1 kHz tick extends the 32-bit cycle counter into 64-bit interval totals; counters
are snapped atomically, then serialized by the existing transport thread. This
measures the application's current running-core/busy-idle configuration.

## Parameter packets and timestamps

Firmware reads the first 16 KiB of the reserved parameter block once at startup,
then sends the stored JSON contents as a `PARM` packet every two seconds on
USB and Ethernet. The header is `<4sIIHH`: magic PARM, sequence 0, JSON byte length,
format version 1, reserved 0. The NUL terminator is not transmitted. Repetition
allows late connections to receive identity without restarting the board.
JSON parsing happens on the host; firmware forwards the stored settings and does
not apply them to networking or sensors. The packer enforces the 16 KiB limit.

Under **Device & firmware parameters → parameter_build → built_at_utc**, the
timestamp records the most recent parameter build. It refreshes independently
of the per-firmware dates under **build**. A parameter-only build leaves those
firmware dates unchanged. Flash the generated parameter block and restart the
board to see its new timestamp; firmware caches the parameter JSON at startup.
Flashing Appli/FSBL also refreshes the selected firmware record's `built_at_utc`
and `programmed_at_utc`. Its `image_built_at_utc` preserves the original image
build time; unflashed slots retain their previous records.

Rebuild/flash before use (firmware flashes also update the parameters):

```sh
./scripts/flash.sh --target all --config Release --build
```

No GUI dependencies are needed by the existing USB_Debug/Ethernet_Debug parsers
beyond their own plotting dependencies; those viewers skip PARM packets and
print CPU measurements as `[CPU]` status lines. Reconstruction ignores profiler
and parameter packets while preserving them in its raw stream recording.


## Sensor diagnostics

The Sensor configuration tab also offers **Power down** (XSHUT shutdown) and a
**Ranging mode** selector: Precision (no DSS), Precision, Ambient. Stop before
applying settings. To wake a powered-down sensor, use Reset ToF, then Start ToF;
reset restores compiled defaults. These controls need the matching firmware.

The live and configuration tabs show die temperature, estimated sensor FPS,
frame-reported mode/period, frame counter and error. Compare the sensor estimate
with received Lidar FPS in the status bar. Last-frame age makes stale readings
visible after Stop/Power down. See [timing diagnostics](streaming-and-diagnostics.md#gaming-mode-frame-rate-and-temperature).

---

[Wiki home](../README.md) · [Full contents](../SUMMARY.md)
