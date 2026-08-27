#pragma once

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cstdint>
#include <string>
#include <vector>

struct Note {
  uint16_t spineIndex = 0;
  uint16_t startPage = 0;
  uint16_t startWordIndex = 0;
  // The clipping's own creation timestamp. (spineIndex, startPage,
  // startWordIndex) alone is not a reliable identity for a clipping —
  // ClippingStore itself disambiguates clippings the same way for exactly
  // this reason (see its own delete-match logic). 0 means "not set" (a note
  // saved before this field existed); such notes are matched by the legacy
  // 3-field key and migrated forward on next save. See findNoteIndex().
  uint32_t clippingTimestamp = 0;
  std::string text;
  char tag = 0;            // single-char tag: '!' '?' '>' '<' '*' '~' etc., or 0 for none
  uint32_t timestamp = 0;  // this note's own last-modified time
};

class NoteStore {
 public:
  // Maximum stored note length; longer text is truncated on save.
  static constexpr size_t kNoteTextMax = 4096;

  static NoteStore& getInstance();

  // Load all notes for a book. Must be called before getNotesForBook / getNoteForClipping.
  void loadForBook(const char* filePath, const char* bookType);

  // Unload current book's notes from memory.
  void unload();

  bool isLoaded() const { return loaded; }

  const std::vector<Note>& getNotes() const { return notes; }

  // Identifies a clipping that still exists, for pruneMissing(). Kept as a
  // plain key so NoteStore does not need to know about ClippingStore.
  struct ClippingKey {
    uint16_t spineIndex;
    uint16_t startPage;
    uint16_t startWordIndex;
    uint32_t timestamp;
  };

  // Drop notes whose clipping is gone. Notes only ever display next to a
  // clipping, so an orphan is invisible but still counted — which made the
  // per-book counts read high. Returns how many were removed; call with the
  // book's notes already loaded.
  uint16_t pruneMissing(const char* filePath, const std::vector<ClippingKey>& live);

  // Bind legacy notes (clippingTimestamp 0, written before a note recorded its
  // clipping's timestamp) to a specific clipping, so findNoteIndex()'s
  // position-only fallback stops being reachable for this book.
  //
  // Two clippings can start at the same word — addClipping does not
  // de-duplicate — and a legacy note cannot say which of them it belonged to.
  // Left alone, the fallback hands the same note to both, and editing either
  // one stamps the note with that clipping's timestamp, silently taking it away
  // from the other. Binding each legacy note to the first clipping at its
  // position settles that once, at load, and only ever chooses between
  // clippings that were already indistinguishable. Returns how many were bound.
  uint16_t bindLegacyNotes(const char* filePath, const std::vector<ClippingKey>& live);

  // Returns nullptr if no note exists for this clipping. clippingTimestamp
  // should be the clipping's own creation timestamp (Clipping::timestamp) —
  // required to disambiguate clippings that otherwise share the same
  // spine/page/word key.
  const Note* getNoteForClipping(uint16_t spineIndex, uint16_t startPage, uint16_t startWordIndex,
                                 uint32_t clippingTimestamp) const;

  // Schema version written into the notes file. Every field so far has an
  // unambiguous default, so nothing needs it yet — but without a marker a
  // future change could not tell "old file, field absent" from "new file,
  // field deliberately zero". One integer now is far cheaper than retrofitting
  // one later. 0 means a file written before the marker existed.
  static constexpr int kNotesFileVersion = 1;

  // Applies text and/or tag in ONE file write. saveNote() followed by saveTag()
  // re-serialises and rewrites the whole per-book notes file twice for what the
  // user experienced as a single action.
  //   text == nullptr    leave any existing text untouched
  //   applyTag == false  leave any existing tag untouched
  //   applyTag == true   set the tag; tag 0 clears it
  // saveNote()/saveTag() below are thin wrappers over this.
  bool saveNoteAndTag(const char* filePath, uint16_t spineIndex, uint16_t startPage, uint16_t startWordIndex,
                      uint32_t clippingTimestamp, const char* text, char tag, bool applyTag);

  // Save or update the text of a note for a clipping.
  // Preserves existing tag if the note already exists.
  bool saveNote(const char* filePath, uint16_t spineIndex, uint16_t startPage, uint16_t startWordIndex,
                uint32_t clippingTimestamp, const char* text);

  // Save or update the tag of a note for a clipping.
  // Preserves existing text if the note already exists.
  // Pass tag = 0 to remove the tag while keeping the text.
  bool saveTag(const char* filePath, uint16_t spineIndex, uint16_t startPage, uint16_t startWordIndex,
               uint32_t clippingTimestamp, char tag);

  // Delete a note entirely.
  bool deleteNote(const char* filePath, uint16_t spineIndex, uint16_t startPage, uint16_t startWordIndex,
                  uint32_t clippingTimestamp);

  // Number of note entries for a book, without disturbing the singleton's
  // currently loaded book. Parses with text filtered out so home reload stays
  // bounded; counts objects only (same rule as loadFromFile). After save,
  // empty shells are purged so entries match real notes. Used only by
  // SavedItemsHomeActivity (subtitle badge and sort key).
  static uint16_t countForFilePath(const std::string& filePath);

  // Delete all notes/tags for a book — e.g. when the book file is deleted or
  // its clippings are cleared. Static, operates on disk; mirrors
  // ClippingStore::deleteForFilePath.
  static void deleteForFilePath(const std::string& filePath);

  // Migrate a book's notes to a new path when the book moves (e.g. Move to
  // /Read). Static, operates on disk; mirrors ClippingStore::migrateForFilePath.
  // True when there is nothing to move or the rename succeeded.
  static bool migrateForFilePath(const std::string& oldPath, const std::string& newPath);

 private:
  NoteStore() = default;

  static std::string notesFilePath(const char* bookFilePath);
  bool loadFromFile(const std::string& path);
  bool saveToFile(const std::string& path) const;
  // Put back a notes file whose replacement was interrupted (see saveToFile).
  static void recoverIfInterrupted(const std::string& path);

  // Internal: find note index by clipping identity, or -1 if not found.
  // Tries the exact (spine, page, word, clippingTimestamp) match first; if
  // clippingTimestamp is nonzero and that fails, falls back to matching a
  // legacy note (clippingTimestamp == 0) on the old 3-field key so notes
  // saved before this field existed aren't orphaned.
  int findNoteIndex(uint16_t spineIndex, uint16_t startPage, uint16_t startWordIndex, uint32_t clippingTimestamp) const;

  bool loaded = false;
  // Set when the book's notes file existed but would not parse. Blocks writes
  // so a corrupt file is never silently replaced by an empty one.
  bool loadFailed = false;
  // Set when the file's version is newer than this firmware writes. Blocks
  // saves so a future format is not rewritten as v1 and stripped of fields
  // we did not understand. Distinct from loadFailed: that one retries on
  // the next loadForBook, this one must not.
  bool newerVersionOnDisk = false;
  std::string bookFilePath;
  std::vector<Note> notes;
};

#define NOTES NoteStore::getInstance()
