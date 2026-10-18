# Board desktop viewer

Ubuntu/Debian dependencies (use Python with Tk support):

```sh
sudo apt install python3-tk python3-pil.imagetk
python3 -m pip install numpy matplotlib pyserial Pillow
```

Run the viewer without sudo so it uses your user's Python packages and display
session. Ethernet connections do not need root privileges.

Launch from VS Code task **Monitor: Board Viewer**, or:

```sh
python3 test_scripts/Board_Viewer.py --host 192.168.8.20 --connect
python3 test_scripts/Board_Viewer.py --port /dev/ttyACM0 --connect
```

Select USB/Ethernet, enter the serial device or host and press Connect. The viewer
reconnects automatically after a link interruption. To switch endpoints, press
Disconnect and wait briefly before Connect. Close other programs using that USB
serial port or TCP streaming connection; the board supports one TCP stream client.

The top bar shows measured receive throughput (Mbit/s and KiB/s), lidar FPS, IMU
Hz, received bytes, missing sequence numbers and parser errors. These are host
measurements, not the PHY's negotiated link speed. Live sensors shows the decoded
lidar zone grid and radial depths in mm, accelerometer in mg, gyro in degrees/s,
and SFLP quaternion XYZW. `--depth-max` controls the heatmap range in mm.

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
cannot acknowledge these commands. See the [wire protocol README](../protocol/README.md)
for exact headers, payload offsets, result codes and byte examples.

Device & firmware parameters shows the complete flash JSON in an expandable tree,
including serial/revisions, compiler and CMake versions, build options, compile
flags/definitions, Git/image hashes and per-slot flash records. Save received
JSON exports it. Every persistent flash refreshes these records automatically;
see [parameter management](../config/README.md) for identity preservation and
explicit settings updates.
An unprogrammed/unreadable parameter block is reported as `parameter_error`.

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

Firmware uses the Cortex-M55 DWT cycle counter and ThreadX execution hooks,
with a small idle-entry hook in `tx_thread_schedule.S`. Peripheral ISRs and the
ThreadX tick bracket interrupt processing, including nested interrupts. The
1 kHz tick extends the 32-bit cycle counter into 64-bit interval totals; counters
are snapped atomically, then serialized by the existing transport thread. This
measures the application's current running-core/busy-idle configuration.

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


The Sensor configuration tab also offers **Power down** (XSHUT shutdown) and a
**Ranging mode** selector: Precision (no DSS), Precision, Ambient. Stop before
applying settings. To wake a powered-down sensor, use Reset ToF, then Start ToF;
reset restores compiled defaults. These controls need the matching firmware.

The live and configuration tabs show die temperature, estimated sensor FPS,
frame-reported mode/period, frame counter and error. Compare the sensor estimate
with received Lidar FPS in the status bar. Last-frame age makes stale readings
visible after Stop/Power down. See [timing diagnostics](STREAMING.md#gaming-mode-frame-rate-and-temperature).
