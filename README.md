# CrossNotes

> ⚠️ **Not yet released.** This firmware has not been tested on a physical device. It compiles cleanly, but hardware testing is still pending. A release will be published once confirmed. Flash at your own risk — you can always revert via the CrossPoint web installer.

---

A fork of [CrossInk](https://github.com/uxjulia/CrossInk) that adds phone-connected highlights & notes to the Xteink X4 / X3.

Highlight a passage, tag it, then open a page on your phone (over the device's own Wi-Fi hotspot) to read your highlights and type notes. No app, no cloud, no account — everything lives on the SD card.

## What it adds

**On the device**
- Tag picker after every highlight — `!` `?` `>` `<` `*` `~` or skip
- Tags and note previews shown in the clipping list and detail view
- **My Notes** home screen — lists books with highlights, jumps to clippings
- **Quick Notes** home shortcut — starts hotspot, shows QR → opens notes on phone
- **Screenshots** home shortcut — same, but opens the screenshot gallery

**On your phone (just the browser)**
- `/highlights` page: read each highlight and type a full note next to it
- Tags shown alongside each highlight
- Screenshots tab with gallery and one-tap download

## How it works

1. Highlight a passage in the reader
2. Tag it (or skip) — reading resumes immediately
3. Home → **Quick Notes** → scan the QR code with your phone
4. Type your notes; they save to the device

## Building

```sh
git clone --recurse-submodules https://github.com/HilbergK/CrossNotes.git
cd CrossNotes
pio run -e tiny    # or teensy / xlarge
```

> Build from PowerShell or CMD on Windows — not Git Bash.

Prebuilt `.bin` files will be on the [Releases page](https://github.com/HilbergK/CrossNotes/releases) once hardware-tested. Flash via the [CrossPoint web installer](./docs/installation.md).

## Credits

Built on [CrossInk](https://github.com/uxjulia/CrossInk) (fonts, reader polish) which is built on [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader). Everything from CrossInk is unchanged. Notes feature developed with Claude (Anthropic). MIT License.
