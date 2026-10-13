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

Device & firmware parameters shows the complete flash JSON in an expandable tree,
including serial/revisions and Git/build metadata. Save received JSON exports it.
An unprogrammed/unreadable parameter block is reported as `parameter_error`.

Firmware reads the first 16 KiB of the reserved parameter block once at startup,
then sends the NUL-terminated JSON contents as a `PARM` packet every two seconds on
USB and Ethernet. The header is `<4sIIHH`: magic PARM, sequence 0, JSON byte length,
format version 1, reserved 0. The NUL terminator is not transmitted. Repetition
allows late connections to receive identity without restarting the board.
JSON parsing happens on the host; firmware forwards the stored settings and does
not apply them to networking or sensors. The packer enforces the 16 KiB limit.

Rebuild/flash the application and parameters before use:

```sh
./scripts/flash.sh --target all --config Release --build
```

No GUI dependencies are needed by the existing USB_Debug/Ethernet_Debug parsers
beyond their own plotting dependencies; those viewers skip PARM packets safely.
