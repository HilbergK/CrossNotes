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
  std::string text;
  char tag = 0;  // NEW: single-char tag: '!' '?' '>' '<' '*' '~'  or 0 for none
  uint32_t timestamp = 0;
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

  // Returns nullptr if no note exists for this clipping.
  const Note* getNoteForClipping(uint16_t spineIndex, uint16_t startPage, uint16_t startWordIndex) const;

  // Save or update the text of a note for a clipping.
  // Preserves existing tag if the note already exists.
  bool saveNote(const char* filePath, uint16_t spineIndex, uint16_t startPage, uint16_t startWordIndex,
                const char* text);

  // Save or update the tag of a note for a clipping.
  // Preserves existing text if the note already exists.
  // Pass tag = 0 to remove the tag while keeping the text.
  bool saveTag(const char* filePath, uint16_t spineIndex, uint16_t startPage, uint16_t startWordIndex, char tag);

  // Delete a note entirely.
  bool deleteNote(const char* filePath, uint16_t spineIndex, uint16_t startPage, uint16_t startWordIndex);

  // Migrate notes when a file is moved/renamed.
  void migrateForFilePath(const std::string& oldPath, const std::string& newPath);

 private:
  NoteStore() = default;

  static std::string notesFilePath(const char* bookFilePath);
  bool loadFromFile(const std::string& path);
  bool saveToFile(const std::string& path) const;

  // Internal: find note index by clipping coords, or -1 if not found.
  int findNoteIndex(uint16_t spineIndex, uint16_t startPage, uint16_t startWordIndex) const;

  bool loaded = false;
  std::string bookFilePath;
  std::vector<Note> notes;
};

#define NOTES NoteStore::getInstance()
