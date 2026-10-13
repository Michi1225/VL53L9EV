#!/usr/bin/env python3
"""Live Ethernet lidar-depth and IMU viewer, sharing the USB packet parser/plots."""
from USB_Debug import main

if __name__ == '__main__':
    main(default_host='192.168.8.20')
