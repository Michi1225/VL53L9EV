# STM32N6 Sensor Board – Bring-Up & System Test Protocol

> **Purpose**  
> Structured bring-up checklist for the STM32N657 sensor/communication board.  
> The sequence is intentionally conservative: each stage should be completed before moving to the next one.

---

# 0. Test Record

- **Board serial / ID:** ______________________
- **PCB revision:** ___________________________
- **Assembly revision:** ______________________
- **Firmware commit / Git hash:** ______________________
- **Date:** ______________________
- **Tester:** ______________________

### Test equipment

- [ ] Bench supply with adjustable current limit
- [ ] DMM
- [ ] Oscilloscope
- [ ] Logic analyzer
- [ ] ST-Link / debugger
- [ ] Ethernet-capable PC
- [ ] Known-good Ethernet cable
- [ ] USB-C cable / PD source
- [ ] Optional thermal camera / IR thermometer
- [ ] Optional differential probe / high-bandwidth probes

### Global rule

If a test fails:

- [ ] Stop before progressing to dependent tests
- [ ] Record the failure
- [ ] Record measured voltages/currents/signals
- [ ] Save relevant scope captures / logs
- [ ] Fix or understand the failure before continuing

---

# 1. Unpowered Inspection

## 1.1 Visual inspection

- [ ] Correct PCB revision received
- [ ] Correct component population
- [ ] No visibly rotated ICs
- [ ] No solder bridges
- [ ] No tombstoned passives
- [ ] No damaged connectors
- [ ] SEMPER flash orientation correct
- [ ] STM32N657 orientation correct
- [ ] ADIN1300 orientation correct
- [ ] LSM6DSV16X orientation correct
- [ ] VL53L9 orientation correct
- [ ] USB-C connector mechanically sound
- [ ] Ethernet connector / magnetics mechanically sound
- [ ] Debug connector accessible
- [ ] BOOT / reset controls accessible

**Notes:**  
______________________________________________________________________

---

## 1.2 Unpowered short-circuit checks

Measure resistance from each major rail to GND.

| Rail | Expected | Measured | Pass |
|---|---:|---:|:---:|
| Main input / VBUS | No hard short | ______ Ω | [ ] |
| 5 V | No hard short | ______ Ω | [ ] |
| 3.3 V | No hard short | ______ Ω | [ ] |
| 1.8 V | No hard short | ______ Ω | [ ] |
| MCU core rail(s) | No hard short | ______ Ω | [ ] |
| Analog rail(s) | No hard short | ______ Ω | [ ] |
| Other: __________ | No hard short | ______ Ω | [ ] |

> Low resistance is not automatically a fault on core rails. Compare against the schematic/BOM and expected load.

- [ ] No unexpected short between adjacent power rails
- [ ] No short from Ethernet shield/chassis domain to digital ground unless intentionally designed
- [ ] USB VBUS not unintentionally shorted to internal rails

---

# 2. First Power-Up

## 2.1 Bench-supply preparation

Before connecting the board:

- [ ] Set supply voltage to expected board input
- [ ] Set conservative current limit
- [ ] Verify polarity
- [ ] Disable output
- [ ] Connect DMM / scope to main rail(s)

Initial current limit: __________ A  
Input voltage: __________ V

---

## 2.2 First power-on

- [ ] Power enabled
- [ ] No immediate current-limit hit
- [ ] No audible/visible abnormality
- [ ] No component heating rapidly
- [ ] Input current appears reasonable

Measured input current: __________ mA

### Stop immediately if

- [ ] Current limit is reached unexpectedly
- [ ] Any regulator output is grossly incorrect
- [ ] Any component heats rapidly
- [ ] Supply oscillates / hiccups unexpectedly

---

# 3. Power Rails

Measure every rail before connecting the debugger.

| Rail | Nominal | Measured | Ripple | Pass |
|---|---:|---:|---:|:---:|
| Main input | ______ V | ______ V | ______ mVpp | [ ] |
| 5 V | ______ V | ______ V | ______ mVpp | [ ] |
| 3.3 V | ______ V | ______ V | ______ mVpp | [ ] |
| 1.8 V | ______ V | ______ V | ______ mVpp | [ ] |
| MCU core | ______ V | ______ V | ______ mVpp | [ ] |
| PHY supply | ______ V | ______ V | ______ mVpp | [ ] |
| Flash supply | ______ V | ______ V | ______ mVpp | [ ] |
| Sensor supply | ______ V | ______ V | ______ mVpp | [ ] |

- [ ] Rail sequencing is plausible
- [ ] No regulator remains in hiccup mode
- [ ] Power-good signals behave as expected
- [ ] Reset supervisors / enables behave correctly
- [ ] Rail ripple is acceptable

**Scope captures saved:** [ ] Yes [ ] No

---

# 4. Clocks, Reset, Boot Pins

## 4.1 Clocks

- [ ] STM32 clock source present
- [ ] ADIN1300 reference clock present
- [ ] XSPI clock only active when expected
- [ ] USB reference clock configuration valid
- [ ] Other required oscillators present

| Clock | Expected | Measured | Pass |
|---|---:|---:|:---:|
| MCU ref | ______ MHz | ______ MHz | [ ] |
| PHY ref | ______ MHz | ______ MHz | [ ] |
| Other | ______ MHz | ______ MHz | [ ] |

## 4.2 Reset and boot configuration

- [ ] NRST idle level correct
- [ ] NRST toggles correctly from debugger
- [ ] BOOT0 level correct
- [ ] BOOT1 level correct
- [ ] Development boot can be selected
- [ ] Normal flash boot can be selected

---

# 5. Debug Access / MCU Bring-Up

- [ ] ST-Link detects target
- [ ] Correct STM32N657 device identified
- [ ] Core can be halted
- [ ] Registers can be read
- [ ] SRAM can be written/read
- [ ] Reset + reconnect works reliably
- [ ] Minimal firmware reaches `main()`
- [ ] System clock configured
- [ ] ThreadX tick works
- [ ] GPIO test works
- [ ] Status LED toggles
- [ ] SWO/UART logging works

Suggested first log:

```text
Boot
Clock OK
ThreadX OK
```

---

# 6. ThreadX / Software Foundation

- [ ] `tx_app_thread` starts
- [ ] NetX application thread starts
- [ ] Control thread starts
- [ ] Thread stacks are valid
- [ ] No immediate stack overflow
- [ ] Event flags work
- [ ] Threads sleep/wake correctly
- [ ] System remains stable for at least 10 minutes

| Thread | Stack size | Max used | Margin | Pass |
|---|---:|---:|---:|:---:|
| App | ______ | ______ | ______ | [ ] |
| NetX | ______ | ______ | ______ | [ ] |
| Control | ______ | ______ | ______ | [ ] |
| Stream | ______ | ______ | ______ | [ ] |

---

# 7. SEMPER Flash

## 7.1 Physical interface

- [ ] XSPI1 connected through XSPIM Port 2
- [ ] NCS activity observed
- [ ] CLK activity observed
- [ ] IO0/IO1 activity observed
- [ ] DQS activity later in OPI/DTR mode

## 7.2 Legacy SPI access

- [ ] Reset sequence succeeds
- [ ] Flash becomes ready
- [ ] JEDEC/device ID correct
- [ ] CFR registers readable
- [ ] Factory/hybrid geometry matches expectation

JEDEC/device ID: ______________________

## 7.3 Mode switching

- [ ] SPI STR works
- [ ] CFR2 latency configuration verified
- [ ] CFR3 latency configuration verified
- [ ] CFR5 reserved bits preserved correctly
- [ ] Enter OPI STR works, if used
- [ ] Enter OPI DTR works
- [ ] CFR5 readback matches requested mode
- [ ] Exit OPI returns reliably to SPI STR
- [ ] Repeated mode transitions work

## 7.4 Basic flash operations

Use a sacrificial test region only.

- [ ] Erased read returns `0xFF`
- [ ] 256-byte page program works
- [ ] Readback matches
- [ ] Cross-page write splitting works
- [ ] 4 KiB erase works in parameter region
- [ ] 256 KiB erase works in main region
- [ ] Busy polling works
- [ ] Program-error handling works
- [ ] Erase-error handling works
- [ ] Timeout handling works

---

# 8. ExtMemLoader

- [ ] `.stldr` generated
- [ ] STM32CubeProgrammer loads loader
- [ ] `StorageInfo` recognized
- [ ] Base address = `0x90000000`
- [ ] Device size = 64 MiB
- [ ] Geometry reported correctly

Expected geometry:

```text
32 × 4 KiB
1 × 128 KiB
255 × 256 KiB
```

- [ ] Read external flash
- [ ] Program test pattern
- [ ] Verify test pattern
- [ ] Sector erase works
- [ ] Full address range accessible
- [ ] Invalid addresses rejected, not wrapped

---

# 9. Boot Chain

## 9.1 Guard / parameter region

- [ ] `0x00000000–0x00000FFF` remains invalid as FSBL1
- [ ] Parameter sectors begin at `0x00001000`
- [ ] BootROM rejects offset `0x00000000` as FSBL1

## 9.2 FSBL2 boot

- [ ] Signed FSBL programmed at flash offset `0x00040000`
- [ ] CPU address corresponds to `0x90040000`
- [ ] BootROM finds FSBL2
- [ ] FSBL executes correctly
- [ ] FSBL configures SEMPER runtime mode

## 9.3 Application A

- [ ] Application A programmed at `0x00100000`
- [ ] FSBL loads Application A into SRAM
- [ ] Vector table correct
- [ ] MSP/VTOR transition correct
- [ ] Cold boot reliable
- [ ] Warm reset reliable

---

# 10. Ethernet PHY Bring-Up

## 10.1 MDIO

- [ ] PHY reset works
- [ ] PHY ID1 readable
- [ ] PHY ID2 readable
- [ ] BMCR readable
- [ ] BMSR readable
- [ ] Extended registers readable/writable
- [ ] ADIN1300 configuration verified

## 10.2 Physical link

With cable disconnected:

- [ ] Link reports DOWN

Plug cable:

- [ ] Link reports UP
- [ ] 1 Gbit/s reported when expected
- [ ] Full duplex reported
- [ ] No repeated renegotiation
- [ ] LEDs behave as expected

Remove cable:

- [ ] Link reports DOWN
- [ ] Application keeps running

Reconnect:

- [ ] Link returns UP without reset

---

# 11. NetX Duo Basic Networking

Use static IP first.

Board IP: ______________________  
Host IP: ______________________  
Netmask: ______________________

- [ ] `NX_IP_LINK_ENABLED` follows cable state
- [ ] `APP_EVT_ETH_LINK_UP` follows cable state
- [ ] `APP_EVT_ETH_IP_READY` follows usable network state
- [ ] ARP works
- [ ] ICMP works
- [ ] Board responds to ping
- [ ] Replugging cable restores ping automatically
- [ ] 100 cable unplug/replug cycles without reset
- [ ] 30 min continuous ping without failure

---

# 12. TCP Control Channel

## 12.1 TCP echo

- [ ] TCP server listens
- [ ] PC connects
- [ ] STM32 receives data
- [ ] Echo returned
- [ ] Client disconnect handled
- [ ] Reconnect handled
- [ ] Cable disconnect during active session handled
- [ ] Cable reconnect allows new session

## 12.2 Control protocol

- [ ] `PING`
- [ ] `GET_DEVICE_INFO`
- [ ] `GET_STATUS`

`GET_DEVICE_INFO` should eventually expose:

- firmware version
- Git hash
- board serial
- hardware revision
- MAC
- protocol version

`GET_STATUS` should expose:

- uptime
- Ethernet state
- SEMPER state
- IMU state
- ToF state
- USB state
- error flags
- stream counters

---

# 13. USB-C / USB-PD

## 13.1 PD

- [ ] Attach detected
- [ ] Default 5 V safe
- [ ] PD negotiation starts
- [ ] Requested PDO/RDO correct
- [ ] Target voltage achieved
- [ ] Detach detected
- [ ] Reattach works

Negotiated voltage: ______ V  
Negotiated current: ______ A

## 13.2 USB data

- [ ] USBX initializes
- [ ] PCD starts appropriately
- [ ] Device enumerates
- [ ] CDC ACM works
- [ ] USB unplug/replug works
- [ ] Ethernet unaffected by USB state
- [ ] USB works with Ethernet disconnected

---

# 14. Shared Control Layer

- [ ] Same command decoder used by TCP and USB
- [ ] `PING` works via Ethernet
- [ ] `PING` works via USB
- [ ] `GET_DEVICE_INFO` identical on both
- [ ] `GET_STATUS` identical on both
- [ ] Concurrent Ethernet + USB behavior defined

---

# 15. IMU – LSM6DSV16X

- [ ] WHO_AM_I correct
- [ ] Register read/write works
- [ ] Reset works
- [ ] Accelerometer starts
- [ ] Gyroscope starts
- [ ] FIFO works if used
- [ ] Interrupt works if used
- [ ] Static acceleration ≈ 1 g
- [ ] Axis orientation matches board coordinates
- [ ] Gyro near zero when stationary
- [ ] Sample rate correct
- [ ] Sensor fusion/game rotation works
- [ ] Timestamping consistent

---

# 16. VL53L9 / MIPI CSI-2

- [ ] Sensor detected/configured
- [ ] Correct operating mode selected
- [ ] CSI-2 clock activity present
- [ ] Lane activity present
- [ ] PHY locks
- [ ] Correct virtual channel
- [ ] Correct data types
- [ ] Correct frame geometry
- [ ] First complete frame received
- [ ] Frame size correct
- [ ] Frame counter increments
- [ ] No buffer corruption
- [ ] Double buffering works
- [ ] Continuous operation stable
- [ ] Frame timestamp captured

---

# 17. UDP Stream Layer

## 17.1 Synthetic stream

- [ ] UDP socket created
- [ ] Synthetic frames generated
- [ ] Packet sequence correct
- [ ] Frame IDs correct
- [ ] Timestamps correct
- [ ] Host reconstructs frame
- [ ] Lost packet only invalidates affected frame
- [ ] Link loss stops stream cleanly
- [ ] Reconnect allows new stream

## 17.2 Real depth stream

- [ ] VL53L9 frame sent over UDP
- [ ] Host reconstructs frame
- [ ] Frame rate correct
- [ ] End-to-end latency measured
- [ ] Packet-loss counters correct
- [ ] Frame-drop counters correct
- [ ] Acquisition never blocks indefinitely on TX

---

# 18. Configuration Storage

## 18.1 Factory information

- [ ] Serial number writable
- [ ] HW revision writable
- [ ] MAC writable
- [ ] Calibration writable
- [ ] Factory CRC validated
- [ ] Normal application does not overwrite factory data

## 18.2 Device configuration

- [ ] Config A valid
- [ ] Config B valid
- [ ] Generation counter works
- [ ] CRC works
- [ ] Latest valid copy selected
- [ ] Power-loss-safe update verified
- [ ] Static IP persists
- [ ] DHCP flag persists
- [ ] Hostname persists

---

# 19. Future A/B Update / FoE

- [ ] Application A and B regions independently erasable
- [ ] Parameter region excluded from normal firmware update
- [ ] FSBL excluded from normal application update
- [ ] Full-chip erase never used by normal updater
- [ ] Update metadata region defined

Later:

- [ ] FoE downloads to inactive slot
- [ ] Image verification
- [ ] Pending slot flag
- [ ] Pending image boot
- [ ] Successful image confirms itself
- [ ] Failed image rolls back
- [ ] Power loss during download preserves current firmware

---

# 20. Fault / Recovery

## Power

- [ ] Power cycle during idle
- [ ] Power cycle during Ethernet traffic
- [ ] Controlled power loss during config write
- [ ] Brownout behavior understood
- [ ] No unrelated flash corruption

## Reset

- [ ] NRST during normal operation
- [ ] Software reset during normal operation
- [ ] Reset while SEMPER is in OPI DTR
- [ ] Flash recovery works after MCU-only reset

## Communication

- [ ] Ethernet removed during TCP
- [ ] Ethernet removed during UDP
- [ ] USB removed during control
- [ ] Ethernet + USB simultaneously
- [ ] Host process crash during TCP
- [ ] Device accepts new host afterward

---

# 21. Performance

## Ethernet

TCP throughput: ______ Mbit/s  
UDP throughput: ______ Mbit/s

- [ ] Sustained TCP throughput measured
- [ ] Sustained UDP throughput measured
- [ ] Packet loss measured
- [ ] CPU load measured
- [ ] Memory usage measured

## Sensor streaming

Frame rate: ______ Hz  
Mean latency: ______ ms  
Worst-case latency: ______ ms  
Dropped frames: ______ %

- [ ] Maximum sustainable frame rate measured
- [ ] Latency measured
- [ ] Jitter measured
- [ ] Drop rate measured

## Flash

- [ ] Read throughput measured
- [ ] Program throughput measured
- [ ] Memory-mapped read throughput measured
- [ ] OPI DTR stable at target clock

---

# 22. Thermal / Full-Load Test

Run:

```text
Ethernet active
+ camera streaming
+ IMU active
+ USB active
+ flash traffic
```

Duration: ______ min

| Component | Temperature | Limit/target | Pass |
|---|---:|---:|:---:|
| STM32N657 | ______ °C | ______ °C | [ ] |
| ADIN1300 | ______ °C | ______ °C | [ ] |
| SEMPER | ______ °C | ______ °C | [ ] |
| Main regulator | ______ °C | ______ °C | [ ] |
| USB/PD power path | ______ °C | ______ °C | [ ] |

- [ ] No reset
- [ ] No thermal throttling
- [ ] Ethernet stable
- [ ] Sensor data stable

---

# 23. Long-Duration Stability

Suggested first target:

```text
24 h continuous operation
```

Run with:

- camera streaming
- IMU active
- Ethernet connected
- periodic control requests
- periodic configuration reads

Track:

Runtime: __________________  
Resets: ___________________  
Dropped frames: ___________  
Network errors: ___________  
Sensor errors: ____________  
Flash errors: _____________  

- [ ] No unexpected reset
- [ ] No packet-pool exhaustion
- [ ] No thread errors
- [ ] No sensor lockups
- [ ] No flash errors

---

# 24. Final Acceptance

- [ ] Power rails stable
- [ ] Debug/recovery works
- [ ] Autonomous SEMPER boot works
- [ ] Parameter storage works
- [ ] Ethernet may be connected after boot
- [ ] Ethernet reconnect works
- [ ] TCP control works
- [ ] USB control works
- [ ] IMU works
- [ ] VL53L9 acquisition works
- [ ] UDP depth stream works
- [ ] Configuration survives reset
- [ ] Communication interruptions recover cleanly
- [ ] Thermal test passes
- [ ] Long-duration test passes

---

# 25. Issue Log

| ID | Stage | Description | Severity | Resolution | Retested |
|---|---|---|---|---|:---:|
| 001 | | | | | [ ] |
| 002 | | | | | [ ] |
| 003 | | | | | [ ] |
| 004 | | | | | [ ] |

---

# 26. Short-Form Bring-Up Checklist

- [ ] **1. Visual inspection**
- [ ] **2. Resistance / short checks**
- [ ] **3. Current-limited first power-up**
- [ ] **4. Verify all rails**
- [ ] **5. Verify clocks / reset / BOOT**
- [ ] **6. Establish SWD/SWO**
- [ ] **7. Minimal ThreadX firmware**
- [ ] **8. SEMPER legacy SPI**
- [ ] **9. SEMPER read/write/erase**
- [ ] **10. ExtMemLoader**
- [ ] **11. BootROM → FSBL2**
- [ ] **12. FSBL → Application A**
- [ ] **13. ADIN1300 MDIO**
- [ ] **14. Ethernet link**
- [ ] **15. Ping**
- [ ] **16. TCP echo/control**
- [ ] **17. Ethernet unplug/replug**
- [ ] **18. USB-PD**
- [ ] **19. USB CDC**
- [ ] **20. IMU**
- [ ] **21. VL53L9 / CSI-2**
- [ ] **22. Synthetic UDP stream**
- [ ] **23. Real depth stream**
- [ ] **24. Persistent configuration**
- [ ] **25. Fault/recovery**
- [ ] **26. Thermal/load**
- [ ] **27. 24-hour soak**
- [ ] **28. Full-system acceptance**
