# DAB Shield

The DABShield library controls the DABShield DAB/DAB+/FM receiver. It provides station and service information, DAB service text, FM RDS data, and access to analogue and digital audio outputs.

## Compatibility and hardware

The library metadata supports all Arduino architectures; actual compatibility depends on the board, its SPI implementation and the DABShield hardware revision. Check voltage requirements and pin assignments before connecting the shield.

See the [DABShield circuit guide](https://www.dabshield.com/circuit) for board pin usage.

## Install

In Arduino IDE Library Manager, search for **DABShield** and install it.

## Documentation

Read the [DABShield API and user guide](docs/API.md) for installation, SPI setup, examples, DAB/FM tuning, audio controls, callbacks, public data fields and troubleshooting.

The library includes `DABApp`, `DABLCD`, `DABSlideShow` and `HybridRadio` example sketches. Some examples require additional displays and libraries; see their sketch headers for details.

For support and project information, visit [dabshield.com](https://www.dabshield.com/).
