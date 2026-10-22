# Boot and application status LED

[Wiki home](../README.md) · [Full contents](../SUMMARY.md)

The same RGB LED indicates progress through ROM-loaded FSBL and application
startup, then follows the acknowledged ToF sensor state. Both firmware images
need the updated LED controller to show the complete sequence.

| State | LED pattern | Trigger |
| --- | --- | --- |
| FSBL started | Amber blink, about 2 Hz | FSBL has configured GPIO/I2C, before flash initialization. |
| FSBL loading | Amber blink, about 5 Hz | Reading A/B metadata, validating a slot and loading it into SRAM. |
| FSBL handoff | Solid magenta | Copied application passed SHA-256 verification; the branch is being prepared. |
| FSBL error | Counted amber burst, then 2 s dark | FSBL error handler; count identifies the boot stage below. |
| Application started | Blue blink, about 2 Hz | Application has configured GPIO/I2C, before the remaining peripheral initialization. |
| Application core init finished | Cyan double pulse, then a pause | Application entry thread starts after RTOS and middleware initialization completes. Sensor initialization can still be in progress. |
| Ranging | Green breathing, about 1.2 s per cycle | ToF successfully enters streaming/ranging state. |
| Idle | Dim blue breathing, about 2.4 s per cycle | ToF successfully stops, or a sensor reset finishes with ranging stopped. |
| ToF powered down | Dim steady amber | Sensor XSHUT shutdown completed. |
| ToF resetting | Cyan blink, about 5 Hz | Reset command is reinitializing the sensor. |
| ToF fault | Red double pulse, then a pause | Latched initialization, capture or ranging fault; explicit Reset ToF is required. |
| Fatal application initialization error | Counted red, blue, or magenta burst, then 2 s dark | Application error handler; color and count identify the startup stage below. |

Early boot transitions can be brief; no artificial boot delays are introduced.
Green means ToF ranging was started, independently of a USB/Ethernet client.
Idle and power-down apply to ToF only: IMU, networking and USB can keep running.
Stop/Start/Reset/Power down update the LED after the corresponding state change;
rejected commands leave the current state indication intact.

Patterns run autonomously in the LED chip. They continue without CPU animation
timers, and can remain visible if the CPU later hangs. A surviving pattern is
the last reported state, not proof that the processor is still making progress.
No indication can mean failure before GPIO/I2C initialization or an unavailable
LED device; magenta only confirms preparation for handoff, not successful
application execution. See [standalone boot diagnostics](firmware-updates.md#diagnosing-standalone-boot).

## Counted startup failure codes

Each failure burst has **N flashes, 200 ms on / 200 ms off, followed by 2 s
dark**, repeating autonomously. Count a complete burst after the long gap.
Normal continuous amber/blue startup blinks have no long gap. These codes require
the new FSBL and application images; an old image can still show an old pattern.

**Amber: FSBL failure before application entry**

| Count | Stage active when the FSBL error handler was reached |
| --- | --- |
| 1 | LED setup / DMA setup before flash initialization |
| 2 | XSPI controller or external flash initialization |
| 3 | Reading/opening the two A/B metadata records |
| 4 | Validating/selecting a boot slot (including initial image adoption and metadata writes), or starting its trial watchdog |
| 5 | Copying the selected image from flash into SRAM |
| 6 | Verifying the SRAM copy's SHA-256 |
| 7 | Preparing application handoff (reserved if this stage reports an error) |

For amber 3 or 4 after direct SWD replacement of an application, check the
[A/B metadata reprovisioning notes](firmware-updates.md#flash-allocation-and-limits).
Existing descriptors can still refer to the old image hash; replacing a binary
alone does not update those descriptors. Use **Flash: Reprovision Release
Baseline**, or `scripts/flash.sh --target all --config Release --build
--reset-boot-metadata`, to back up and reset only those boot records after
verifying the baseline. Do not erase unrelated device data.

**Red: application peripheral initialization**

| Count | Stage |
| --- | --- |
| 1 | HAL / tick initialization |
| 2 | System or common peripheral clocks |
| 3 | GPIO |
| 4 | I2C2 |
| 5 | GPDMA1 |
| 6 | DCMIPP / CSI capture setup |
| 7 | Ethernet MAC / PHY setup |
| 8 | UCPD1 |
| 9 | USB OTG HS peripheral |
| 10 | SPI4 |
| 11 | I3C2 |
| 12 | XSPI1 |
| 13 | System isolation / RIF setup |
| 14 | USB-PD pre-OS setup |

Errors before application I2C/LED initialization cannot program a new code; the
previous FSBL pattern can remain. Codes identify the active initialization call,
including nested MSP/clock errors; they do not identify its underlying cause.

**Blue: application startup services**

| Count | Stage |
| --- | --- |
| 1 | Reading device parameters |
| 2 | Entering ThreadX before middleware initialization |
| 3 | Runtime: application entry thread has started |

**Magenta: RTOS pools / middleware setup**

| Count | Stage |
| --- | --- |
| 1 | ThreadX application memory pool |
| 2 | Application thread / service creation |
| 3 | NetX Duo memory pool |
| 4 | NetX Duo initialization |
| 5 | USBX memory pool |
| 6 | USBX initialization |
| 7 | USB-PD memory pool |
| 8 | USB-PD initialization |

When debug access is available, `app_boot_stage`, `app_boot_failed_stage`, and
`app_boot_error_return_address` retain the current checkpoint, failing checkpoint,
and return address of the caller of `Error_Handler()`. These are live RAM symbols,
not a persistent reset log. FSBL retains `fw_boot_stage` / `fw_boot_result`.

### Earlier continuous-red ambiguity

The earlier double-pulse preset used infinite **pattern** repetition, preventing
its final pause from being reached. An FSBL error or ToF fault could therefore
look like the application's continuous red 5 Hz fatal indication. Continuous red
on those images does not prove that the jump reached the application. The driver
now runs the double-pulse pattern once and repeats the complete **engine**,
including its pause. Counted codes also use finite pattern repetition followed
by the engine's complete-cycle repeat, as described in
[TI LP5815 sections 7.3.4.1–7.3.4.2](https://www.ti.com/lit/gpn/lp5815).

## Driver behavior

The register map matches [TI LP5815](https://www.ti.com/lit/gpn/lp5815).
Timing codes, infinite repetition, engine routing and commands follow its
autonomous animation registers. The existing channel current settings and
red/green/blue output mapping are retained. Legacy animation names remain
available; lifecycle code uses the explicit FSBL/application names.

`LEDController_Init()` runs once after I2C setup in each firmware image. The
header declares one shared controller instance. Every animation is constructed
from cleared configuration, with explicitly encoded register bytes, so earlier
patterns and compiler bitfield layout cannot alter later states.

`LEDController_SetAnimation()` returns the HAL transfer status. Repeating a
successful request generates no I2C traffic and does not restart the animation.
Failed repeated requests retry at most every 500 ms; the sensor owner thread
services retries even while idle or waiting for a frame. A changed state can be
attempted immediately. A concurrent writer receives `HAL_BUSY`; the driver masks
interrupts only while guarding its ownership flag and restores the caller's
interrupt state before I2C transfers. Calls belong in startup or thread context.
`led_controller.last_status` and `animation_valid` expose unsuccessful writes
when debugger access is available.

## Validation

```sh
python3 -m unittest discover -s test_scripts -p test_led_controller.py -v
python3 -m unittest discover -s test_scripts -p test_sensor_control.py -v
```

Native tests check actual configuration/engine/pattern register bytes, all
preset transitions, infinite playback, duplicate suppression, failed transfer
steps, retry timing and wraparound, concurrent calls, and ToF state mapping.
An independent waveform model checks that double-pulse pauses actually execute,
that each diagnostic burst contains the requested number of flashes, and that
the full 2 s gap is dark. Tests cover every application startup-code mapping.
The sensor harness verifies LED state follows Stop/Start/Reset/Power down/fault
acknowledgements. Cross-build FSBL and Appli before a board check; confirm colors
and cadence on the physical LED after flashing the images.
