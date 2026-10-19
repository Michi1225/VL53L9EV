# Streaming and sensor diagnostics

[Wiki home](../README.md) · [Full contents](../SUMMARY.md)

## Contents

- [Ethernet streaming](#ethernet-streaming)
- [USB streaming](#usb-streaming)
- [Packet framing and profiling](#packet-framing-and-profiling)
- [Capture and IMU timing](#capture-and-imu-timing)
- [Backpressure and link recovery](#backpressure-and-link-recovery)
- [Isolating sensor faults](#isolating-sensor-faults)
- [Gaming mode, frame rate and temperature](#gaming-mode-frame-rate-and-temperature)

For exact byte offsets, command/response formats and examples, see the
[wire protocol README](../protocols/wire-protocol.md).

## Ethernet streaming

The application sends identical camera and IMU packets over USB CDC and Ethernet
TCP. To view depth, acceleration, gyro and SFLP over Ethernet, run:

```bash
python3 test_scripts/Ethernet_Debug.py --host 192.168.8.20
```

TCP port defaults to 50002 (`--tcp-port` changes the client port). The server
forwards telemetry when one client connects; no handshake is required. ToF starts
at boot, and can be stopped/restarted from the Device Monitor on USB or TCP 50002.
The viewer reconnects automatically and resets partial packets on reconnect.
Ethernet can be plugged in after boot or unplugged/replugged while running.
The network thread polls the PHY every 200 ms, retries MAC/DMA enable when link
appears, and publishes IP readiness only after enable succeeds. Link removal
withdraws readiness and stops MAC/DMA. Negotiation time is additional to polling.
TCP listeners discard old sessions and incomplete handshakes on a cable cycle;
the Device Monitor reconnects automatically. ToF/IMU operation and USB continue.
The IPv4 address remains static: configure the PC's Ethernet adapter on
`192.168.8.0/24` with a different host address (for example `192.168.8.10/24`).
The legacy echo server remains on port 50000; the throughput server and its
benchmark scripts have been removed. The normal full application remains Release.

## USB streaming

For USB, run:

```bash
python3 test_scripts/USB_Debug.py --port /dev/ttyACM1
```

Omit `--port` to discover devices and follow changed ttyACM numbers on reconnect.
The plot window shows a ToF depth heatmap plus acceleration xyz (mg), gyro xyz
(deg/s), and SFLP quaternion xyzw over the most recent 10 seconds. IMU plots use
the MCU timestamps and clear when the device reconnects.
Plots target 30 FPS (`--fps 60` requests 60). The depth grid is decoded from the
latest complete frame using status-row binning and crop metadata, rather than
CSI transport dimensions (e.g. 100×149 carries a 54×42 zone grid). Little-endian
depth words contain radial distance in millimetres in bits 0–14; bit 15 selects
the ranging step and is not an invalid-data flag. Zero returns appear gray.
The colour scale defaults to 0–4000 mm (`--depth-max 9000` changes its maximum);
hover to see a zone's depth. This displays raw radial distances without ST's
calibration, confidence filtering, or radial-to-perpendicular postprocessing.
See [TN1641, section 2.3](https://www.st.com/resource/en/technical_note/tn1641-vl53l9cx-postprocessing-pipeline-stmicroelectronics.pdf#page=5).
The IMU time axis is relative
to the latest sample, and acceleration/gyro ranges expand when needed.

## Packet framing and profiling

Both packet headers use `<4sIIHH` (little-endian): magic, sequence number, payload
length, and two type-specific fields. Sequence numbers are independent per sensor.

| Magic | Header fields 12/14 | Payload |
| --- | --- | --- |
| `VL9F` | width / height | width × height RAW8 bytes, including sensor status row |
| `IMUF` | 10 float channels / format version 1 | `<I10f`: MCU timestamp in milliseconds, acc xyz in mg, gyro xyz in mdps, SFLP quaternion xyzw |
| `PARM` | format version 1 / reserved 0 | UTF-8 device/firmware parameter JSON, repeated every two seconds |
| `PROF` | format version 1 / thread count | CPU profiling interval, sent about once per second |
| `TOFS` | format version 2 / acknowledged operation (0 for periodic) | 36-byte sensor configuration/state, DSS flags and command result; PC also accepts v1 (32 bytes) |
| `CTRL` (host → board) | format version 1 or 2 / operation | Empty for Get/Start/Stop/Reset/Power down; 12-byte settings for Set (v2 adds DSS flags) |

`PROF` has its own sequence counter. Its payload starts with `<IIQQQQHH`:
MCU timestamp (ms), core clock (Hz), interval cycles, idle cycles, interrupt
cycles, scheduler cycles, thread count, flags. Each thread record is `<IIQ32s`:
thread ID (control-block address), priority, interval CPU cycles and a
NUL-terminated UTF-8 name (up to 31 bytes). All numbers are little-endian.
Up to 64 threads are reported; flags bit 0 indicates a truncated thread list.
Unreported/removed-thread cycles appear as `Other / removed threads` on the PC.
CPU time in ms is `cycles * 1000 / core_clock`; CPU percentage is
`cycles * 100 / interval_cycles`. Report windows continue across connections,
so receiving one complete report is enough to display usage. See the
[desktop viewer](device-monitor.md) for the CPU profiler tab.

## Capture and IMU timing

Camera capture uses DCMIPP PIPE0's dump path to preserve RAW8 bytes without ISP
processing or padded row pitch. Watch `hdcmipp.PipeState[0]` for this capture.
PIPE0 setup lives in `DCMIPP_Init 2`'s CubeMX USER CODE block in `dcmipp.c`,
so it survives regeneration even while CubeMX also configures PIPE1.

IMU packets are 60 bytes total. Acc, gyro, and SFLP run at 120 Hz; the thread
publishes the latest readings at 100 Hz after all three have produced data. The
timestamp records host acquisition time, not an IMU hardware timestamp. SFLP is
the game rotation quaternion, so it does not provide a compass heading. Transports use
a bounded 16-packet queue: sampling continues during backpressure and full
queues drop samples. Sequence gaps expose lost samples at the host.

When stationary, acceleration
magnitude should be about 1000 mg, gyro near zero, and quaternion magnitude near
one. The host should report about 100 IMU packets/s when the transport keeps up. IMU
streaming continues after a VL53L9 sensor fault.
IMU sampling has priority over the USB application thread. FIFO overruns
flush the FIFO and resume after fresh acc/gyro/SFLP readings;
they no longer terminate streaming. A sequence gap flags the discontinuity.

## Backpressure and link recovery

A single transport thread owns packet consumption and sends complete packets to
USB (if CDC/DTR is active) and the TCP client (if connected). With neither active,
frames are discarded and capture is rearmed. This avoids two threads competing
for the same camera buffer or splitting IMU packets between clients. Both
connections can be used together; slow readers can reduce camera capture rate.
TCP allocation/send waits are bounded to 250 ms per segment; a failed/partial
write closes that session so a future connection starts at a fresh packet.

To check cable recovery on hardware, boot without Ethernet, wait more than five
seconds, then insert the cable. Once link negotiates, verify `ping 192.168.8.20`
and connect the monitor to TCP 50002. Unplug during streaming, reinsert, and
confirm that the monitor reconnects without resetting the board. Also exercise
unplugging while the PC is attempting a connection. Native regressions cover
late insertion, failed enable/disable retries, an already enabled link at boot,
half-open TCP cleanup and a full cable cycle during a blocked transport write.
USB writes retain their existing one-second timeout. Sampling is not blocked by
the queue, so backpressure can drop IMU samples. One TCP client is supported.

## Isolating sensor faults

The following comparisons can isolate an intermittent VL53L9 fault using one
board. Reset/reboot the sensor between attempts and record its firmware code and
all five laser-driver bytes. The VL9F sequence counts frames captured for USB;
it is not the sensor's own frame counter or a fault code. Repeated runs may fail
at different sequence numbers without changing the underlying fault.

1. Run without the Python receiver (or USB host). Camera packets are discarded
   while disconnected, so this checks whether host traffic changes the fault.
2. Keep exposure, binning, power mode and CSI bitrate unchanged, but change the
   frame period in the selected profile in
   `Appli/Libs/53L9/interface/vl53l9/vl53l9_utils.c`, then rebuild.

   Flash the resulting ELF and compare repeated runs by elapsed time as well as
   frame count. Improvement suggests a duty-cycle/timing dependency; it does not
   establish that the laser-driver path is healthy.
3. Run the selected profile with DCMIPP capture disabled:

   ```bash
   cmake -S Appli -B Appli/build -DVL53L9_SENSOR_ONLY=ON
   cmake --build Appli/build -j 4
   ```

   The sensor still emits CSI frames, but no VL9F packets are produced. IMU
   packets continue. The stream thread polls sensor status once a second. A
   recurring `0x0F00` / `laser_driver[3] = 0x80` in this mode establishes that
   STM32 frame capture and camera USB transfers are not required to trigger it.
   This test leaves the sensor's CSI transmitter enabled and does not isolate
   every possible electrical interaction with the board.

Restore normal settings afterward:

```bash
cmake -S Appli -B Appli/build -DVL53L9_SENSOR_ONLY=OFF
cmake --build Appli/build -j 4
```

The diagnostic options do not change laser safety checks or automatically retry
faulted emission. The firmware preserves sensor fault status, stops capture and latches a fault.
The command service stays alive to report the fault; Start is rejected until an
explicit Reset ToF (or board reset). Reset ToF restores compiled defaults and
leaves ranging stopped; Start must be requested separately. IMU and profiler
reports continue during sensor reset.
ST's error handling and bit definitions are in
[UM3683, sections 2.4–2.4.2](https://www.st.com/resource/en/user_manual/um3683-programing-guide-stmicroelectronics.pdf#page=16).


## Gaming mode, frame rate and temperature

The monitor's **Sensor configuration** tab exposes Precision (no DSS), Precision
and Ambient. These map to short/DSS off, short/DSS short and long/DSS long.
Previously Gaming selected long context and `vl53l9_set_binning` enabled DSS;
it therefore used Ambient rather than the datasheet's Gaming mode. Gaming now
selects Precision (no DSS), 54×42, regular power and a 10 ms requested period,
keeping the local 1 ms exposure. Other presets retain DSS.

ST's [VL53L9CX datasheet, Table 9](https://www.st.com/resource/en/datasheet/vl53l9cx.pdf)
uses **4 ms** exposure for its 100 fps Gaming example. A 1 ms setting is a deliberate
shorter exposure. Low/ultralow modes turn blocks off between frames; regular mode
keeps them powered. Exposure alone therefore does not determine power or temperature.
The [programming guide, Tables 22–23](https://www.st.com/resource/en/user_manual/um3683-programing-guide-stmicroelectronics.pdf)
describes context/DSS mapping and processing time: full-resolution Precision DSS
computation is listed as 13.5 ms, running in parallel with exposure. A readable
10 ms period register alone cannot establish actual frame rate.

CSI snapshots previously waited for the transport to finish before rearming.
The next capture is now armed immediately after copying into the separate
transport packet. This removes a source of alternate-frame skipping; transport
backpressure can still limit received FPS. The monitor displays both received
Lidar FPS and **estimated sensor FPS** from the sensor frame counter. A sensor
estimate near 100 with received FPS near 50 points to capture/transport loss;
if both are near 50, investigate sensor timing/mode and measurement jitter.
The GUI redraw rate is separate from packet reception.

The monitor also displays die temperature in °C, the mode and period reported
in the frame, the frame counter, firmware error and the age of the last frame.
Temperature is retained as a last-frame reading after stopping/shutdown; it is
not a live reading while off. Confirm temperature/error details on hardware
before concluding that heating caused a fault. Protection remains enabled.

**Power down** holds XSHUT low and keeps IMU/network/profiler operation running.
Supply rails remain connected. To wake: **Reset ToF → Start ToF**. Reset restores
compiled defaults and leaves emission stopped. Stop pauses ranging without
asserting XSHUT. The full wire format is in [protocol/README.md](../protocols/wire-protocol.md).

---

[Wiki home](../README.md) · [Full contents](../SUMMARY.md)
