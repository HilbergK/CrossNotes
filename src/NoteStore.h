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

  // Returns nullptr if no note exists for this clipping. clippingTimestamp
  // should be the clipping's own creation timestamp (Clipping::timestamp) —
  // required to disambiguate clippings that otherwise share the same
  // spine/page/word key.
  const Note* getNoteForClipping(uint16_t spineIndex, uint16_t startPage, uint16_t startWordIndex,
                                 uint32_t clippingTimestamp) const;

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

  // Delete all notes/tags for a book — e.g. when the book file is deleted or
  // its clippings are cleared. Static, operates on disk; mirrors
  // ClippingStore::deleteForFilePath.
  // Number of notes carrying a tag or text for a book, without disturbing the
  // singleton's currently loaded book. Reads and parses that book's notes file,
  // so call it per screen build, not per frame.
  static uint16_t countForFilePath(const std::string& filePath);

  static void deleteForFilePath(const std::string& filePath);

  // Migrate a book's notes to a new path when the book moves (e.g. Move to
  // /Read). Static, operates on disk; mirrors ClippingStore::migrateForFilePath.
  static void migrateForFilePath(const std::string& oldPath, const std::string& newPath);

 private:
  NoteStore() = default;

  static std::string notesFilePath(const char* bookFilePath);
  bool loadFromFile(const std::string& path);
  bool saveToFile(const std::string& path) const;

  // Internal: find note index by clipping identity, or -1 if not found.
  // Tries the exact (spine, page, word, clippingTimestamp) match first; if
  // clippingTimestamp is nonzero and that fails, falls back to matching a
  // legacy note (clippingTimestamp == 0) on the old 3-field key so notes
  // saved before this field existed aren't orphaned.
  int findNoteIndex(uint16_t spineIndex, uint16_t startPage, uint16_t startWordIndex, uint32_t clippingTimestamp) const;

  bool loaded = false;
  std::string bookFilePath;
  std::vector<Note> notes;
};

#define NOTES NoteStore::getInstance()
