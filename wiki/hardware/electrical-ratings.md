# Electrical ratings and characteristics

[Wiki home](../README.md) · [Full contents](../SUMMARY.md)

The value cells below are intentionally blank. They are unassigned board-level
ratings, not zeroes, typical component ratings, or values inferred from USB-PD
profiles. Fill them in from approved design limits and board characterization.

These tables are the shared source for the user guide and the datasheet's
electrical section. Edit this page, then run `python3 scripts/build_docs.py` to
update both LaTeX documents. The datasheet section between its generated markers
must not be edited separately.

## Absolute maximum ratings

| Parameter | Symbol | Min | Max | Unit |
| --- | --- | --- | --- | --- |
| Board input voltage | VIN | | | V |
| USB VBUS voltage | VBUS | | | V |
| Exposed signal pin voltage | VIO | | | V |
| Board input current | IIN | | | A |
| Storage temperature | TSTG | | | °C |

## Recommended operating conditions

| Parameter | Symbol | Min | Typ | Max | Unit |
| --- | --- | --- | --- | --- | --- |
| Board input voltage | VIN | | | | V |
| Digital interface voltage | VIO | | | | V |
| Ambient operating temperature | TA | | | | °C |
| Relative humidity | RH | | | | % |

## Electrical characteristics

| Parameter | Symbol | Conditions | Min | Typ | Max | Unit |
| --- | --- | --- | --- | --- | --- | --- |
| Input current, ToF idle | IIDLE | | | | | mA |
| Input current, ToF ranging | IRANGE | | | | | mA |
| Input power, ToF idle | PIDLE | | | | | W |
| Input power, ToF ranging | PRANGE | | | | | W |
| USB VBUS inrush current | IINRUSH | | | | | A |
| Supply ripple | VRIPPLE | | | | | mVpp |
| Power-on startup time | TSTART | | | | | ms |

Record the PCB revision, assembly variant, firmware, supply conditions, sensor
configuration, and measurement method when assigning values. Keep absolute
limits, recommended conditions, and measured characteristics distinct.
