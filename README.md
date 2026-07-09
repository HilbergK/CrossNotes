# CrossNotes

A fork of [CrossInk](https://github.com/uxjulia/CrossInk) which is a fork of CrossPoint. CrossNotes adds a phone-connected note-taking system for the Xteink X4 / X3.

Highlight a passage, tag it, then open a page on your phone (over the device's own Wi-Fi hotspot) to read your highlights and type notes. The notes are stored on the device and shown right alongside the highlight.

## What it adds

**On the device**
- Tag picker after every highlight — `!` `?` `*` `~` `+` `=` `#` `<` `>` or no tag, plus a "Write a note…" option using the built-in keyboard
- Tags and note previews shown in the clipping list and detail view
- **Notes and Bookmarks** home entry — browse every book with highlights or bookmarks; Notes and Bookmarks are separate lists per book
- **Phone Notes** home shortcut — starts the hotspot and shows a QR code straight to the notes page on your phone
- Edit Tag / Edit Note / Delete available from the clipping menu (delete asks for confirmation)

**On your phone (just the browser)**
- `/highlights` page: read each highlight, set its tag from a dropdown, and type a full note next to it
- **Export Notes** — download all of a book's highlights, tags, and notes as a Markdown file
- **Screenshots** tab: gallery of device screenshots with one-tap download or delete

## How it works

1. Highlight a passage in the reader
2. Tag it (or skip, or write a note directly) — reading resumes immediately
3. Home → **Phone Notes** → scan the QR code with your phone
4. Read your highlights and type notes; they save straight to the device

## Building & installing

Prebuilt `.bin` files are on the [Releases page](https://github.com/HilbergK/CrossNotes/releases) — see the [installation guide](./docs/installation.md) for the web-based flash tool.

To build from source:

```sh
git clone --recurse-submodules https://github.com/HilbergK/CrossNotes.git
cd CrossNotes
pio run -e tiny    # or teensy / xlarge
```

## Credits

Built on [CrossInk](https://github.com/uxjulia/CrossInk) (fonts, reader polish) which is built on [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader). Everything from CrossInk is unchanged. Notes feature developed with Claude. MIT License.
