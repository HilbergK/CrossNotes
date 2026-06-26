---
title: Installation
nav_order: 14
---

# Installation

> **Beta status:** CrossNotes has not yet been confirmed on a physical device. Prebuilt firmware will be published to [Releases](https://github.com/HilbergK/CrossNotes/releases) once tested on hardware. Until then, build from source (see [README](../README.md#building--flashing)) or flash a beta `.bin` if one is provided in an issue or discussion. Flashing only writes the app partition and is reversible — reflash the official firmware at any time.

## Web installer

1. Download a `firmware-*.bin` file from the [CrossNotes releases page](https://github.com/HilbergK/CrossNotes/releases).
2. Connect your Xteink X4 or X3 via USB-C and wake/unlock the device.
3. Go to <https://crosspointreader.com/#flash-tools> and choose your device.
4. Select **Custom .bin**, choose your downloaded file, and click **Flash**.

To revert to the official firmware, flash it from the same page at <https://crosspointreader.com/#flash-tools>.

## Command line

Install `esptool`:

```sh
pip3 install esptool
```

Download `firmware-*.bin` from the [releases page](https://github.com/HilbergK/CrossNotes/releases) and connect your device via USB-C.

Find the port:

```sh
# Linux
dmesg | grep tty

# macOS
ls /dev/cu.*
```

Flash:

```sh
# Linux
esptool.py --chip esp32c3 --port /dev/ttyACM0 --baud 921600 write_flash 0x10000 firmware.bin

# macOS
esptool.py --chip esp32c3 --port /dev/cu.usbmodem2101 --baud 921600 write_flash 0x10000 firmware.bin
```

> **Windows:** use the web installer above. The command-line path works but is more involved; the web tool is simpler.
