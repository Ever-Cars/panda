# CAN bus to FDCAN mapping on Red Panda

This document summarizes which on-chip FDCAN peripheral each Panda CAN bus
number uses and which STM32H7 pins back those peripherals when the harness is in
its normal orientation.

## Bus-to-peripheral mapping

The firmware keeps a small lookup table (`bus_config`) that maps Panda bus
numbers to hardware CAN controllers. Each entry stores both the bus number that
an FDCAN instance should report upstream and the reverse lookup used when a USB
client transmits on a Panda bus. In the default (normal) harness orientation the
mapping is:

| Panda bus | STM32 peripheral | Notes |
|-----------|------------------|-------|
| Bus 0     | FDCAN1           | Primary bus; remains mapped to CAN1 unless the harness is detected as flipped. |
| Bus 1     | FDCAN2           | Secondary bus; always driven by FDCAN2. |
| Bus 2     | FDCAN3           | Third bus; swaps with Bus 0 if the harness is flipped. |

Changing the harness orientation only swaps the entries for Bus 0 and Bus 2 so
that the physical harness connection always shows up as Panda bus 0.

## Pin configuration

During `common_init_gpio()` the firmware programs the STM32H7 GPIOs so each
FDCAN controller is attached to the expected pins:

- FDCAN1 uses `PB8`/`PB9`.
- FDCAN2 uses `PB5`/`PB6` (with pull-ups left enabled on `PB12`/`PB13` because
  those pins can be muxed onto the harness).
- FDCAN3 uses `PG9`/`PG10`.

When the board later selects its CAN mode, `red_set_can_mode()` keeps the
`PB5`/`PB6` alternate-function mapping active for the normal harness orientation
and powers up the matching transceiver. If the harness is flipped it instead
moves FDCAN2 onto `PB12`/`PB13` and enables the alternate transceiver.

This means that sending on Panda **bus 1** always drives the FDCAN2 peripheral,
which puts the bytes onto whichever pin pair (`PB5`/`PB6` or `PB12`/`PB13`) is
active for the current harness orientation. Sending on Panda **bus 2** goes out
through the FDCAN3 peripheral using pins `PG9`/`PG10`.
