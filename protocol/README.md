# VL53L9EV wire protocol

This is the byte-level reference for the application and Device Monitor.
Implementation: `Appli/Libs/control/{ethernet_stream,tof_control,stream_manager}.c`,
`test_scripts/USB_Debug.py` and `test_scripts/Sensor_Control.py`.

## Connections and lifecycle

| Transport | Endpoint | Direction |
| --- | --- | --- |
| Ethernet | TCP port **50002** (default board IP `192.168.8.20`) | Framed commands to board; interleaved telemetry and replies from board |
| USB | CDC ACM, typically `/dev/ttyACM0` | Identical framing in each direction; assert DTR |
| Legacy Ethernet | TCP port 50000 | Echo service; does **not** accept the commands below |

USB baud rate is ignored (host uses 115200). One TCP stream client and one USB
serial owner are supported; both transports can operate simultaneously. Open a
connection and read packets; no handshake is required. Ranging starts at boot
with the compiled `CONF_USECASE` profile. `GET_CONFIG` or periodic `TOFS` reports
reveal the actual running configuration; do not assume particular defaults.

Ethernet supports cable insertion after boot and unplug/replug during operation.
The network thread checks the PHY every 200 ms and explicitly enables/disables
the MAC/DMA driver; PHY link alone does not mean the interface is ready. A cable
cycle invalidates old TCP sessions, including incomplete handshakes, so clients
must reconnect. The static IPv4 configuration is retained. USB and sensor
acquisition continue during Ethernet outages.

`STOP` stops sensor ranging and the CSI receiver, affecting depth output on
**both** transports. IMU, parameters, CPU profiling and sensor status continue.
`START` restarts stopped ranging. Disconnecting does not change ranging state.
Settings and start/stop state are volatile. A board reset restores compiled
defaults and starts ranging. `RESET` (operation 5) resets only the ToF sensor,
restores compiled defaults and leaves ranging **stopped**; press Start afterward.
IMU, USB/Ethernet connections and CPU profiling continue during sensor reset.
`POWER_DOWN` (operation 6) stops capture and holds XSHUT low. State becomes
**powered down**; this is sensor shutdown with supply rails still connected.
Use `RESET`, then `START`, to range again. Start and Set while powered down
return result 7. Stop and Power down are idempotent while off. Shutdown is
also accepted when a fault is latched; it preserves the error until Reset.
The flash parameter JSON is identity/build metadata, separate from these runtime
sensor settings.

All I3C sensor operations run in the ToF thread. A complete depth packet already
owned by the transport is finished before a Stop acknowledgement, sensor reset,
power-down or configuration change. Successful Stop acknowledgement therefore
follows the last depth packet
on that connection, until a later Start (possibly from the other transport).
Faults remain latched until an explicit `RESET` or board reset; there is no
automatic reset or retry of faulted emission. `RESET` stops CSI capture, clears
its pending frame event, toggles the sensor's XSHUT pin, reassigns its I3C address,
reloads the sensor firmware patch and applies the compiled profile. Success
clears the fault and acknowledges stopped state with default settings. Failure
returns Sensor operation failed and keeps a fault latched. Driver validation
and sensor protection remain active.

## Common 16-byte header

All integers are **little-endian**. Floats are IEEE-754 binary32, little-endian.
There is no structure padding, CRC, terminator or alignment between packets.
Python header format: `struct.Struct('<4sIIHH')`.

| Byte offset | Size | Type | Meaning |
| --- | --- | --- | --- |
| 0 | 4 | ASCII bytes | Magic identifying packet type |
| 4 | 4 | uint32 | Sequence number, or command request ID |
| 8 | 4 | uint32 | Payload byte length, excluding the header |
| 12 | 2 | uint16 | Type-specific field A |
| 14 | 2 | uint16 | Type-specific field B |
| 16 | payload length | bytes | Payload |

TCP reads and USB transfers may split or combine packets arbitrarily. Accumulate
bytes, validate the magic/header/length, then consume exactly `16 + length` bytes.
Never search for magic inside a valid packet's payload. Preserve up to three
trailing bytes while searching for a new magic after an invalid header. Clear
partial packets at every reconnect. Sequence arithmetic wraps modulo 2³².

| Magic | Direction | Header A | Header B | Payload bytes |
| --- | --- | --- | --- | --- |
| `CTRL` | Host → board | Format version **1 or 2** | Operation | 0 or 12 |
| `TOFS` | Board → host | Format version **2** (legacy 1 also decoded by PC) | Replied-to operation; 0 for periodic status | 36 (legacy 32) |
| `VL9F` | Board → host | CSI width | CSI height | width × height, at most 14900 |
| `IMUF` | Board → host | **10** float channels | Format version **1** | 44 |
| `PARM` | Board → host | Format version **1** | Reserved **0** | 1–16384 UTF-8 bytes |
| `PROF` | Board → host | Format version **1** | Thread count, 0–64 | 44 + 48 × thread count |

Camera, IMU and profiler sequence counters are independent, start at zero and
continue across connection and ToF start/stop cycles. `PARM` uses sequence zero.
`TOFS` replies use the request's ID; periodic status uses ID zero. Do not include
`TOFS`/`PARM` in sensor sequence-gap calculations.

## Commands: `CTRL`

Use a **nonzero uint32 request ID**, field A = 1 or 2, field B = operation.
The monitor sends Set as v2 and empty commands as v1. Firmware accepts both.
Update firmware and monitor together to use the new modes/shutdown; old firmware
does not recognize v2 Set commands. Allow one
outstanding command per connection. Match replies by request ID **and** operation.
The Device Monitor waits up to eight seconds; a timeout/disconnect leaves command
outcome uncertain. Read current state before retrying a mutating command. Do not
blindly resend commands on reconnect.

| Operation | Name | Payload | Behavior |
| --- | --- | --- | --- |
| 1 | `GET_CONFIG` | Empty | Return current settings/state |
| 2 | `START` | Empty | Start stopped ranging; already streaming succeeds without restarting |
| 3 | `STOP` | Empty | Stop ranging/capture; already stopped succeeds |
| 4 | `SET_CONFIG` | 12 bytes below | Apply settings while stopped; remain stopped |
| 5 | `RESET` | Empty | Reset/reinitialize the sensor from streaming, stopped, powered-down or faulted state; restore compiled defaults and remain stopped |
| 6 | `POWER_DOWN` | Empty | Stop CSI and assert XSHUT; leave sensor off until Reset |

`SET_CONFIG` payload format: `<IHHBBH`.

| Payload offset (absolute offset) | Size | Field | Valid values |
| --- | --- | --- | --- |
| 0 (16) | 4 | Frame period, µs | 10000–1000000, inclusive |
| 4 (20) | 2 | Exposure, ms | Integer 1–30; exposure × 1000 ≤ frame period |
| 6 (22) | 2 | Binning | 2, 4, 6, 8, 12, 24 |
| 8 (24) | 1 | Power mode | 0 regular, 1 low, 2 ultra low |
| 9 (25) | 1 | Range context | 0 short, 1 long |
| 10 (26) | 2 | Flags | v1: **0** (DSS enabled); v2 bit 0: **disable DSS**, all other bits zero |

The datasheet's ranging modes correspond to these settings:

| Ranging mode | Context | DSS enabled | v2 flags |
| --- | --- | --- | --- |
| Precision (no DSS) | 0 short | false | 1 |
| Precision | 0 short | true | 0 |
| Ambient | 1 long | true | 0 |

Long context with DSS disabled is rejected by the control protocol. Gaming now
uses Precision (no DSS), regular power, binning 2, 10000 µs period and the user's
1 ms exposure. Applying binning first restores driver DSS defaults; the explicit
DSS selection is written and read back afterwards.

Sync mode stays autonomous; CSI stays RAW8, VC0, one lane, 1 Gbit/s. The firmware
updates both the sensor's ranging settings and its CSI frame geometry when
binning changes. Passing these host-side bounds does not guarantee that the
sensor accepts every combination; check the acknowledgement and state.

Command parser storage is bounded to 28 bytes, with eight queued commands and
eight queued replies. Invalid magic, version, or payload length above 12 is
resynchronized without an acknowledgement. Recognized frames with an invalid
operation, payload layout, reserved value or configuration receive an error.
Queue saturation produces a best-effort Busy reply; a flooding client can also
lose replies. A transport session change discards partial commands and stale
replies; already accepted commands may still execute. Do not pipeline requests.

### Exact command examples

Stop, request ID `0x12345678` (16 bytes):

```text
43 54 52 4c  78 56 34 12  00 00 00 00  01 00 03 00
C  T  R  L  request ID    payload = 0  v1    STOP
```

Reset ToF, request ID 1 (16 bytes, no payload):

```text
43 54 52 4c  01 00 00 00  00 00 00 00  01 00 05 00
```

Wait for a `TOFS` response with request ID 1, operation 5, result 0 and state 1
(stopped). Reset discards runtime settings; the reply contains the restored
configuration. Send Start separately to resume ranging.

Set 20 ms frame period, 2 ms exposure, binning 4, low power, short context,
request ID 7 (28 bytes, v2, DSS enabled):

```text
43 54 52 4c  07 00 00 00  0c 00 00 00  02 00 04 00
20 4e 00 00  02 00 04 00  01 00 00 00
```

Python (on an already-open USB serial connection or TCP stream socket):

```python
from Sensor_Control import build_command, STOP, SET_CONFIG, START

stop = build_command(1, STOP)
settings = build_command(2, SET_CONFIG, dict(
    period_us=20000, exposure_ms=2, binning=4, power=1, context=0, dss_enabled=True))
start = build_command(3, START)
# Send stop; wait for matching successful TOFS.
# Send settings; wait for matching successful TOFS.
# Send start; wait for matching successful TOFS.
# TCP: connection.sendall(command); USB: connection.write(command).
```

## Sensor status and acknowledgements: `TOFS`

Every command reply contains a complete state snapshot. Replies go only to the
requesting transport/session, interleaved with telemetry by the single transport
writer. Unsolicited snapshots are sent to both links about once per second,
including while stopped, powered down or faulted. Current v2 payload format:
`<IIiIHHBBBBHHIHH` (36 bytes). The first 32 bytes retain the v1 layout.
The PC treats v1 status as DSS enabled.

| Payload offset | Size | Type | Field |
| --- | --- | --- | --- |
| 0 | 4 | uint32 | MCU uptime timestamp, ms |
| 4 | 4 | uint32 | Configuration/state revision; increments on state or configuration changes |
| 8 | 4 | int32 | Latched error: 0 healthy; sensor firmware code or negative application/driver error (`-1` for generic failures) |
| 12 | 4 | uint32 | Frame period, µs |
| 16 | 2 | uint16 | Exposure, ms |
| 18 | 2 | uint16 | Binning |
| 20 | 1 | uint8 | Power mode, as above |
| 21 | 1 | uint8 | Range context, as above |
| 22 | 1 | uint8 | State: 0 initializing, 1 stopped, 2 streaming, 3 fault, 4 powered down |
| 23 | 1 | uint8 | Command result, below; periodic status uses 0 |
| 24 | 2 | uint16 | CSI width in bytes |
| 26 | 2 | uint16 | CSI height in rows, including status row |
| 28 | 4 | uint32 | Frames captured since STM32 boot, modulo 2³²; preserved across sensor resets |
| 32 | 2 | uint16 | v2 flags: bit 0 = DSS disabled; all other bits zero |
| 34 | 2 | uint16 | v2 reserved: zero |

Settings/dimensions can be zero during initialization or after initialization
failure. A configuration failure latches a fault; do not assume partially written
hardware settings were rolled back. The last successfully applied settings remain
in the snapshot. Result 0 in a periodic report is not a health indication; inspect
state/error. While stopped, faulted or powered down, the monitor clears the depth display.

| Result | Meaning |
| --- | --- |
| 0 | Success |
| 1 | Unknown command |
| 2 | Invalid request/configuration |
| 3 | Busy / command queue full |
| 4 | Sensor/capture operation failed; inspect latched state/error |
| 5 | Configuration requires stopped ranging |
| 6 | Sensor fault is latched; explicit `RESET` or board reset required |
| 7 | Sensor is powered down; use `RESET` before Start/Set |

## Depth frames: `VL9F`

The payload is a tightly packed CSI RAW8 dump, including sensor padding and the
final **100-byte metadata row**. Header width/height describe transport bytes,
not the number of depth zones. Sequence is the board's captured-frame counter,
independent of the sensor metadata's own frame counter, which can restart on sensor reset.

| Binning | Raw zone grid | Displayed grid after default crop | CSI bytes × rows | Payload bytes |
| --- | --- | --- | --- | --- |
| 2 | 54×42 | 54×42 | 100×149 | 14900 |
| 4 | 24×24 | 24×20 | 100×39 | 3900 |
| 6 | 18×14 | 18×14 | 100×18 | 1800 |
| 8 | 12×10 | 12×10 | 100×9 | 900 |
| 12 | 8×8 | 8×6 | 100×6 | 600 |
| 24 | 4×4 | 4×4 | 100×3 | 300 |

For `N = raw_width × raw_height`, payload planes are:

| Payload offset | Length | Content |
| --- | --- | --- |
| 0 | 2N | Little-endian uint16 radial distances: bits 0–14 = mm, bit 15 = ranging-step selector |
| 2N | 2N | uint16 amplitude plane |
| 4N | 2N | uint16 ambient plane |
| 6N | D = N/2 if DSS enabled; otherwise 0 | Packed DSS lookup data (absent with DSS disabled) |
| 6N + D | Remaining bytes before final row | CSI padding; fixed geometry above supports either DSS setting |
| payload_length − 100 | 100 | Sensor metadata |

Zero distance means a missing return in the monitor. Bit 15 is not an invalidity
flag. Decode/crop from metadata rather than guessing from the CSI row count.
Within the final metadata row (offsets relative to that row):

| Offset | Type | Meaning |
| --- | --- | --- |
| 0 | uint32 | Sensor frame counter |
| 4 | int16 | Sensor die temperature, °C |
| 6 | uint16 | Laser-driver temperature register (raw; no host conversion) |
| 58 | uint16 | Dynamic flags; DSS = `(word >> 4) & 3` (0 disabled, 1 long, 2 short), binning = `(word >> 6) & 0x1f`, context = `(word >> 11) & 1` |
| 60 | uint16 | Sensor error code |
| 62 | uint8 | Sensor error status |
| 68 | uint32 | Reported sensor frame period, µs (does not prove delivered FPS) |
| 72 | uint32 | Crop: width bits 0–5, height 6–11, x offset 12–17, y offset 18–23, enabled bit 24 |

`vl53l9_utils.h` defines the remaining sensor-specific metadata fields. Raw radial
ranges are not ST's calibrated/confidence-filtered or perpendicular-depth output.

## IMU: `IMUF`

Payload `<I10f>` is 44 bytes; the complete packet is 60 bytes.

| Payload offset | Type | Meaning |
| --- | --- | --- |
| 0 | uint32 | MCU acquisition timestamp, ms |
| 4 | 3 × float32 | Acceleration X/Y/Z, mg |
| 16 | 3 × float32 | Gyroscope X/Y/Z, mdps (divide by 1000 for °/s) |
| 28 | 4 × float32 | SFLP game quaternion X/Y/Z/W |

Sampling is published at 100 Hz, backed by 120 Hz sensor/SFLP updates. A bounded
16-packet queue drops samples when full; sequence gaps expose losses. Timestamps
wrap modulo 2³². SFLP has no compass-heading reference.

## Device/build parameters: `PARM`

Payload is UTF-8 JSON, excluding the flash's NUL terminator/padding. Repeated
about every two seconds for late opens/reconnects. Parameter `schema_version`
is independent of wire format version 1. The board forwards stored metadata;
it does not apply this JSON to the runtime sensor controls. Unreadable or missing
parameters use a JSON object containing `parameter_error`.

`parameter_build.built_at_utc` records when the parameter artifact was generated.
Each Appli/FSBL flash also refreshes the selected slot's `build.<slot>.built_at_utc`
and `programmed_at_utc`; `image_built_at_utc` retains the manifest's original
image build date. Unflashed slots and all firmware dates during a parameter-only
update stay unchanged. The application reads the JSON
once at startup, so a new parameter image appears in `PARM` after a board restart.

See [parameter management](../config/README.md) for fields, flash layout and the
metadata update workflow.

## CPU profiling: `PROF`

Reports arrive about once per second, with actual intervals lengthened by
transport backpressure. Payload header `<IIQQQQHH>` is 44 bytes:

| Payload offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | MCU timestamp, ms |
| 4 | 4 | CPU clock frequency, Hz |
| 8 | 8 | Interval cycles |
| 16 | 8 | Idle cycles |
| 24 | 8 | Interrupt cycles (nested interrupts counted once) |
| 32 | 8 | Scheduler cycles |
| 40 | 2 | Thread count (must match common header field B) |
| 42 | 2 | Flags: bit 0 = thread list truncated; other bits reserved zero |

Each following thread record `<IIQ32s>` is 48 bytes:

| Record offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | Thread ID (control-block address) |
| 4 | 4 | ThreadX priority |
| 8 | 8 | Thread CPU cycles in this interval |
| 16 | 32 | NUL-terminated name, up to 31 UTF-8 bytes |

`milliseconds = cycles × 1000 / clock_hz`,
`CPU share = cycles × 100 / interval_cycles`,
`CPU busy = 100 − idle share`. Blocked/sleeping time is not charged to threads.
Any residual interval cycles represent omitted/deleted threads. Up to 64 threads
are reported. Reports are independent intervals; a new connection needs no
previous report to compute percentages.

## Backpressure and recovery

The single transport writer sends complete packets to each active link. TCP
allocation/send waits are bounded to 250 ms per 1460-byte segment; USB writes
retain a one-second timeout. A slow link can delay camera, replies and reports
on both links. A partial/failed TCP send closes that session. ToF commands wait
for the current camera packet to be consumed; USB command writes use a 0.5-second
host timeout. The PC receiver's eight-second acknowledgement timeout is separate
from these transport timeouts.

After copying a completed frame into the transport packet, the firmware immediately
rearms the separate DMA capture buffer. Capture overlaps transmission; the transport
packet is never overwritten while in use. The next completed snapshot waits for
consumption of the previous packet. Slow readers can still cause skipped sensor frames.
The monitor estimates sensor FPS from changes in the sensor counter divided by
host arrival time (averaged over at least 0.5 s). Buffering/jitter affects this
estimate; compare it with received Lidar FPS and reported period. Sensor reset,
stop, shutdown and reconnect restart the timing window. Temperature and mode
readouts show the most recent frame and its age, including after shutdown.

With no readers, camera packets are discarded and capture is rearmed while
ranging remains enabled. Stopping ToF prevents new capture. The IMU producer is
independent and can drop queued samples under backpressure. Neither a reconnect
nor command timeout resets the sensor or changes a latched fault.
