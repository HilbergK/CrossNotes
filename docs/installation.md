---
title: Installation
nav_order: 14
---

# Installation

> ⚠️ **No release yet.** CrossNotes has not been tested on hardware. Prebuilt firmware will appear on the [Releases page](https://github.com/HilbergK/CrossNotes/releases) once confirmed. Until then, build from source or wait.

## Web installer (once a release is available)

1. Download `firmware-*.bin` from the [Releases page](https://github.com/HilbergK/CrossNotes/releases).
2. Connect your Xteink X4 or X3 via USB-C and wake the device.
3. Go to <https://crosspointreader.com/#flash-tools>, choose your device, select **Custom .bin**, and flash.

To revert: flash the official firmware from the same page.

## Build from source

```sh
git clone --recurse-submodules https://github.com/HilbergK/CrossNotes.git
cd CrossNotes
pio run -e tiny --target upload
```

See the [README](../README.md#building) for details.
