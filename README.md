# CrossNotes

> A fork of [CrossInk](https://github.com/uxjulia/CrossInk) that adds a phone-connected **highlights & notes system** to the Xteink X4 / X3.

Highlight a passage, tag it with one button press, then open a page on your phone (over the device's own Wi-Fi hotspot — no app, no cloud, no account) to read your highlights and type full notes alongside them. Everything lives on the SD card.

---

## How it works

1. **Highlight** a passage using the built-in clip selection.
2. **Tag it** — a picker pops up immediately after saving; press once to tag or skip. Reading resumes.
3. From the home menu, choose **Quick Notes** — a QR code appears.
4. **Scan with your phone** — it joins the device's hotspot and opens the notes page directly.
5. **Type your notes**; they save to the device. Tags and note previews now show in the device's clipping screens too.

No internet, no companion app — the e-reader serves the page itself.

---

## Status

Not yet tested on a physical device. The firmware compiles and links cleanly, and every integration point was verified against the actual CrossInk source — but it hasn't been flashed and confirmed on a real X4. Will update once it's been run on hardware. If you test it before then, please open an issue with results.

To revert at any time: reflash the official firmware via the CrossPoint web installer. No permanent changes.

---

## What CrossNotes adds

### On the device
- **Highlight → tag** — after saving a highlight, a quick picker lets you tag it with one symbol:

  | Tag | Meaning |
  |-----|---------|
  | `!` | Important |
  | `?` | Question |
  | `>` | Key argument |
  | `<` | Counterpoint |
  | `*` | Cite / reference later |
  | `~` | Verify this claim |
  | _(skip)_ | no tag |

- **Notes in the clipping list** — tags and a written note preview appear under each highlight, on both the list and detail screens.
- **My Notes** — home screen entry that lists every book with highlights and jumps straight into its clipping list.
- **Quick Notes** — home shortcut that starts the hotspot and shows a QR code opening your phone directly on the notes page.
- **Screenshots** — home shortcut that does the same, landing on the screenshot gallery.

### On your phone (just the browser)
- **`/highlights`** page in the device's built-in web portal: pick a book, read each highlight, and add or edit a full written note next to it. Notes save back to the device.
- Tags shown next to each highlight.
- **Screenshots tab** — gallery of device screenshots (including per-book reader screenshots) with one-tap download.

---

## Building & flashing

Builds with PlatformIO, same as CrossInk. Prebuilt `.bin` files will be published to [Releases](https://github.com/HilbergK/CrossNotes/releases) once the firmware is confirmed on hardware — for now, build from source:

```sh
git clone --recurse-submodules https://github.com/HilbergK/CrossNotes.git
cd CrossNotes
pio run -e tiny          # or teensy / xlarge
pio run -e tiny --target upload
```

> **Windows:** build from PowerShell or CMD, not Git Bash — the ESP32 toolchain rejects the MSYS environment.

Flash with the [CrossPoint web installer](./docs/installation.md) once a Release is up — no toolchain needed.

> **Flash budget:** this build sits at ~97% of the flash partition. It fits, but that's the ceiling.

---

## Credits & license

- **[CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader)** — the firmware this is all based on.
- **[CrossInk](https://github.com/uxjulia/CrossInk)** — the fork this builds on, which adds custom fonts and reader polish. Everything from CrossInk is still here unchanged.
- CrossInk Notes feature work developed with assistance from Claude (Anthropic).

MIT License (© 2025 Dave Allie) — see [LICENSE](./LICENSE). Notes/tags/web-UI additions released under the same license.
