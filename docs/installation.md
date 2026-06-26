---
title: Installation
nav_order: 14
---

# Installation

> **Status:** CrossNotes is a build-verified **beta** that has not yet been confirmed on a physical device. Prebuilt releases will be published once a build is tested on hardware. Until then you can build from source (see the [README](../README.md#building--flashing)) or flash a beta `.bin` if one is provided. Flashing only writes the app partition and is reversible — you can always reflash the official firmware.

## Web Installer

1. Download a `firmware-*.bin` file for the build variant of your choosing from the [releases page](https://github.com/HilbergK/CrossNotes/releases) (or use a beta `.bin` provided in an issue/discussion).
2. Connect your Xteink X4 or X3 to your computer via USB-C and wake/unlock the device.
3. Go to <https://crosspointreader.com/#flash-tools> and choose your device.
4. Select **Custom .bin** from the options.
5. Choose the `firmware-*.bin` file you downloaded and click **Flash**.

To revert back to the official firmware, flash the latest official firmware from <https://crosspointreader.com/#flash-tools>.

## Command Line

These instructions are for macOS and Linux. Windows users should use the web installer.

Install `esptool`:

```sh
pip3 install esptool
```

Download the `firmware-*.bin` file from the [releases page](https://github.com/HilbergK/CrossNotes/releases), then connect your device with USB-C.

Find the device port:

```sh
# Linux
dmesg | grep tty

# macOS
ls /dev/cu.*
```

Flash the firmware:

```sh
# Linux
esptool.py --chip esp32c3 --port /dev/ttyACM0 --baud 921600 write_flash 0x10000 /path/to/firmware.bin

# macOS
esptool.py --chip esp32c3 --port /dev/cu.usbmodem2101 --baud 921600 write_flash 0x10000 /path/to/firmware.bin
```

Replace the port and firmware path with your actual values.
