# CrossNotes

> A fork of [CrossInk](https://github.com/uxjulia/CrossInk) / [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) for the **Xteink X4 / X3** e-reader that adds a phone-connected **highlights & notes system** — layered on top of a custom-font reading fork.

Highlight a passage on the device, tag it with one button press, then open a page on your phone (over the device's own Wi-Fi hotspot — no app, no cloud, no account) to read your highlights and type full notes alongside them. Everything is stored locally on the SD card.

---

## ⚠️ Status: build-verified beta — not yet hardware-tested

Be honest with yourself before flashing this:

- ✅ **It compiles and links cleanly** into a flashable ESP32-C3 image, and every feature was verified against the actual firmware source (APIs, activity lifecycle, web endpoints).
- ❌ **It has not yet been run on a physical Xteink X4.** "Compiles" is not the same as "works on the device." The on-device tag picker, the hotspot/QR flow, and the phone notes UI still need a real flash-and-check.

If you flash this, treat it as a **beta you are helping test**. The CrossPoint web installer flashes only the app partition and is fully reversible (you can reflash stock firmware), so the risk is low — but the features are unconfirmed until someone runs them on hardware. If you test it, please open an issue with what worked and what didn't. 🙏

---

## What CrossNotes adds

### On the device
- **Highlight → tag** — after you save a highlight, a quick picker lets you tag it with a single symbol, no typing:
  | Tag | Meaning |
  |-----|---------|
  | `!` | Important |
  | `?` | Question |
  | `>` | Key argument |
  | `<` | Counterpoint |
  | `*` | Cite / reference later |
  | `~` | Verify this claim |
  | _(skip)_ | save with no tag |
- **Notes in the clipping list** — tags and a preview of your written note appear under each highlight, on both the list and detail screens.
- **My Notes** home screen — lists every book that has highlights and jumps straight into its highlights, without opening the book first.
- **Quick Notes** home shortcut — starts the Wi-Fi hotspot and shows a QR code that opens your phone **directly** on the notes page.
- **Screenshots** home shortcut — same idea, but the QR opens directly on the screenshot gallery.

### On your phone (just the browser — nothing to install)
- A **`/highlights`** page in the device's built-in web portal: pick a book, read each highlight, and **add or edit a full written note** in a text box next to it. Notes save back to the device's SD card.
- On-device tags are shown next to each highlight.
- A **Screenshots** tab that shows a gallery of device screenshots (including per-book reader screenshots) with one-tap download.

---

## How it works (the flow)

1. **Read & highlight** a passage using the built-in clip selection.
2. **Tag it** — the picker pops up; press once to tag (or skip). Reading resumes.
3. Back at the home menu, choose **Quick Notes** → a QR code appears.
4. **Scan it** with your phone — it joins the device's hotspot and opens the notes page.
5. **Type your notes**; they sync to the device. Your tags and note previews now show on the device's clipping screens too.

No account, no internet, no companion app — the e-reader serves the page itself over a local hotspot.

---

## Building & flashing

CrossNotes builds with [PlatformIO](https://platformio.org/), exactly like upstream CrossInk.

```sh
# 1. Clone WITH submodules (the open-x4-sdk submodule is required, or the build fails)
git clone --recurse-submodules https://github.com/HilbergK/CrossNotes.git
cd CrossNotes
# (if you already cloned without it:  git submodule update --init --recursive )

# 2. Build  (teensy / tiny / xlarge are font-size variants; pick one)
pio run -e tiny

# 3. Flash over USB-C
pio run -e tiny --target upload
```

> **Windows note:** build from **PowerShell or CMD**, not Git Bash/MSYS — the ESP32 toolchain rejects the MSYS environment. (`pio run` works fine from PowerShell.)

Prefer a no-build-tools route? Once a build is confirmed on hardware, prebuilt `firmware-*.bin` files can be flashed with the [CrossPoint web installer](./docs/installation.md) — no toolchain needed.

> **Flash budget:** this build sits at ~97% of the flash partition. It fits, but that's the ceiling — keep an eye on it if you add more features.

---

## The reading fork underneath (fonts, sizes, reader features)

CrossNotes is built on a custom-font CrossInk fork. Those features are unchanged and still here.

### Reader Fonts
The default fonts are ChareInk, Lexend Deca, and Bitter — chosen for reading fluency and crisp e-ink rendering with reduced ghosting. The UI uses [Inter](https://fonts.google.com/specimen/Inter) for better readability at small sizes.

- [ChareInk](https://www.mobileread.com/forums/showthread.php?t=184056) — a long-time e-reading community favorite based on [Charis](https://software.sil.org/charis/).
- [Lexend Deca](https://fonts.google.com/specimen/Lexend+Deca) — a research-backed sans-serif designed to improve reading fluency.
- [Bitter](https://fonts.google.com/specimen/Bitter) — a slab serif designed for comfortable on-screen reading; the medium weight renders well on the X4/X3.

### Other reader features
- Limited Unicode [emoji](https://unicode-explorer.com/b/1F600) and [misc symbols](https://unicode-explorer.com/b/2600) support (Noto Emoji / Noto Sans Symbols).
- Adjustable font sizes across the `teensy`, `tiny`, and `xlarge` build variants — see [Font Build Variants](./docs/font-build-variants.md).
- Strikethrough, thicker underlines, `<hr>` section breaks, redaction-style rendering, simple tables, bookmarks.
- A custom `Minimal` theme and sleep screen.
- Reader-only front-button remapping, Bionic Reading, Guide Dots, and Force Paragraph Indents — see [Reader Features](./docs/reader-features.md) and [Controls](./docs/controls.md).

---

## Tips for the best reading experience

CrossInk runs on an ESP32-C3 with limited RAM, so very large folders or complex EPUBs can be slow.

- Keep folders under ~200 files (50–100 is smoothest). 1000+ books is fine if split into subfolders.
- Avoid dumping every book in the SD card root.
- Text-first EPUBs work best; aim for under ~20 MB. Large image-heavy books may be slow or memory-sensitive.
- Use a reliable SD card with some free space — settings, progress, caches, stats, **and now your notes** live there.

---

## Documentation

- [User Guide](./USER_GUIDE.md)
- [Installation](./docs/installation.md)
- [Web server usage](./docs/webserver.md) · [Web server endpoints](./docs/webserver-endpoints.md)
- [Reader Features](./docs/reader-features.md) · [Controls](./docs/controls.md)
- [Font Build Variants](./docs/font-build-variants.md)
- [Simulator](./docs/simulator.md) · [Data Cache](./docs/data-cache.md)
- [Common issues](./docs/troubleshooting.md) · [Project scope](./SCOPE.md)

---

## Credits & license

CrossNotes stands on the shoulders of:

- **[CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader)** — the open-source e-reader firmware this is all based on.
- **[CrossInk](https://github.com/uxjulia/CrossInk)** — the font/typography fork this builds on.
- The CrossInk Notes feature work in this fork was developed with assistance from Claude (Anthropic).

Licensed under the **MIT License** (© 2025 Dave Allie) — see [LICENSE](./LICENSE). The notes/tags/web-UI additions are released under the same license.
