---
title: Installation
nav_order: 2
---

# Installation

## Web installer

1. Download `firmware-*.bin` from the [Releases page](https://github.com/HilbergK/CrossNotes/releases).
2. Connect your Xteink X4 or X3 via USB-C and wake the device.
3. Go to <https://crosspointreader.com/#flash-tools>, choose your device, select **Custom .bin**, and flash.

To revert: flash the official CrossInk or CrossPoint firmware from the same page.

## SD Card Firmware Update

For updating CrossNotes without USB. Also works on USB-locked devices.

1. Download the CrossNotes `firmware-*.bin` from the [Releases page](https://github.com/HilbergK/CrossNotes/releases).
2. Place the `.bin` file anywhere on your SD card.
3. On the device, go to `Settings > System > SD Card Firmware Update`, navigate to the `.bin` file, and update.

## USB Locked Devices

If your device has USB data transfer disabled, use the **SD Card Firmware Update** method above — it does not need a USB data connection.

## Build from source

```sh
git clone --recurse-submodules https://github.com/HilbergK/CrossNotes.git
cd CrossNotes
pio run -e default --target upload
```

See the [README](../README.md#building--installing) for details.
