The application sends camera and IMU packets through one USB CDC connection. Run:

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

Both packet headers use `<4sIIHH` (little-endian): magic, sequence number, payload
length, and two type-specific fields. Sequence numbers are independent per sensor.

| Magic | Header fields 12/14 | Payload |
| --- | --- | --- |
| `VL9F` | width / height | width × height RAW8 bytes, including sensor status row |
| `IMUF` | 10 float channels / format version 1 | `<I10f`: MCU timestamp in milliseconds, acc xyz in mg, gyro xyz in mdps, SFLP quaternion xyzw |

Camera capture uses DCMIPP PIPE0's dump path to preserve RAW8 bytes without ISP
processing or padded row pitch. Watch `hdcmipp.PipeState[0]` for this capture.
PIPE0 setup lives in `DCMIPP_Init 2`'s CubeMX USER CODE block in `dcmipp.c`,
so it survives regeneration even while CubeMX also configures PIPE1.
`vl53_capture_counter` preserves the hardware counter, `vl53_capture_bytes`
reports that count without multiplying by four, and `vl53_frame_nonzero_bytes` counts nonzero
bytes in the last payload.

IMU packets are 60 bytes total. Acc, gyro, and SFLP run at 120 Hz; the thread
publishes the latest readings at 100 Hz after all three have produced data. The
timestamp records host acquisition time, not an IMU hardware timestamp. SFLP is
the game rotation quaternion, so it does not provide a compass heading. USB uses
a bounded 16-packet queue: sampling continues during USB backpressure and full
queues drop samples. Sequence gaps expose lost samples at the host.

Watch `imu_stream_status` (0 = OK), `imu_samples_published`,
`imu_packets_dropped`, and `imu_deadlines_missed`. When stationary, acceleration
magnitude should be about 1000 mg, gyro near zero, and quaternion magnitude near
one. The host should report about 100 IMU packets/s when USB keeps up. IMU
streaming continues after a VL53L9 sensor fault.
IMU sampling has priority over the USB application thread. FIFO overruns increment
`imu_fifo_overruns`, flush the FIFO, and resume after fresh acc/gyro/SFLP readings;
they no longer terminate streaming. A sequence gap flags the discontinuity.

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
faulted emission. The firmware preserves sensor fault status and stops capture.
ST's error handling and bit definitions are in
[UM3683, sections 2.4–2.4.2](https://www.st.com/resource/en/user_manual/um3683-programing-guide-stmicroelectronics.pdf#page=16).
