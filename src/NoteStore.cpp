#include "NoteStore.h"

#include <Arduino.h>  // for millis()
#include <uzlib.h>

#include <cinttypes>  // for PRIx32 (not guaranteed via <cstdint>)
#include <cstring>

static constexpr const char* LOG_TAG = "NoteStore";
static constexpr const char* NOTES_DIR = "/.crosspoint/notes/";
static constexpr size_t NOTE_TEXT_MAX = NoteStore::kNoteTextMax;

// ─── Singleton ────────────────────────────────────────────────────────────────

NoteStore& NoteStore::getInstance() {
  static NoteStore instance;
  return instance;
}

// ─── Path Helpers ─────────────────────────────────────────────────────────────

std::string NoteStore::notesFilePath(const char* bookFilePath) {
  const uint32_t crc = uzlib_crc32(bookFilePath, static_cast<unsigned int>(strlen(bookFilePath)), 0);
  char filename[32];
  snprintf(filename, sizeof(filename), "%08" PRIx32 ".json", crc);
  return std::string(NOTES_DIR) + filename;
}

// ─── Load / Unload ────────────────────────────────────────────────────────────

void NoteStore::loadForBook(const char* filePath, const char* /*bookType*/) {
  if (loaded && bookFilePath == filePath) return;
  unload();
  bookFilePath = filePath;
  const std::string path = notesFilePath(filePath);
  if (Storage.exists(path.c_str())) {
    if (!loadFromFile(path)) {
      LOG_ERR(LOG_TAG, "Failed to load notes from %s", path.c_str());
    }
  }
  loaded = true;
}

void NoteStore::unload() {
  notes.clear();
  bookFilePath.clear();
  loaded = false;
}

bool NoteStore::loadFromFile(const std::string& path) {
  FsFile file = Storage.open(path.c_str(), O_RDONLY);
  if (!file) return false;
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, file);
  file.close();
  if (err) {
    LOG_ERR(LOG_TAG, "JSON parse error: %s", err.c_str());
    return false;
  }
  const JsonArray arr = doc["notes"].as<JsonArray>();
  for (const JsonObject obj : arr) {
    Note note;
    note.spineIndex = obj["spineIndex"] | uint16_t(0);
    note.startPage = obj["startPage"] | uint16_t(0);
    note.startWordIndex = obj["startWordIndex"] | uint16_t(0);
    note.text = obj["text"] | std::string{};
    note.timestamp = obj["timestamp"] | uint32_t(0);
    // tag stored as a 1-character string; 0 / missing = no tag
    const char* tagStr = obj["tag"] | "";
    note.tag = tagStr[0];  // '\0' if missing or empty — correct default
    notes.push_back(std::move(note));
  }
  return true;
}

bool NoteStore::saveToFile(const std::string& path) const {
  // Ensure directory exists
  if (!Storage.exists(NOTES_DIR)) {
    Storage.mkdir(NOTES_DIR);
  }

  // Write to .tmp, then rename — protects against corruption on power loss.
  // Use O_TRUNC (not FILE_WRITE) so a stale .tmp from an interrupted write is
  // overwritten, not appended to — FILE_WRITE maps to O_AT_END (append).
  const std::string tmpPath = path + ".tmp";
  FsFile file = Storage.open(tmpPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
  if (!file) {
    LOG_ERR(LOG_TAG, "Cannot open %s for writing", tmpPath.c_str());
    return false;
  }

  JsonDocument doc;
  JsonArray arr = doc["notes"].to<JsonArray>();
  for (const Note& note : notes) {
    JsonObject obj = arr.add<JsonObject>();
    obj["spineIndex"] = note.spineIndex;
    obj["startPage"] = note.startPage;
    obj["startWordIndex"] = note.startWordIndex;
    obj["text"] = note.text;
    obj["timestamp"] = note.timestamp;
    // tag: store as 1-char string, or omit if no tag
    if (note.tag != 0) {
      obj["tag"] = std::string(1, note.tag);
    }
  }

  const size_t written = serializeJson(doc, file);
  file.sync();
  file.close();
  if (written == 0) {
    LOG_ERR(LOG_TAG, "serializeJson wrote 0 bytes to %s", tmpPath.c_str());
    Storage.remove(tmpPath.c_str());
    return false;
  }

  // Atomic rename
  if (Storage.exists(path.c_str())) Storage.remove(path.c_str());
  Storage.rename(tmpPath.c_str(), path.c_str());
  return true;
}

// ─── Lookup ───────────────────────────────────────────────────────────────────

int NoteStore::findNoteIndex(uint16_t spineIndex, uint16_t startPage, uint16_t startWordIndex) const {
  for (int i = 0; i < static_cast<int>(notes.size()); i++) {
    const Note& n = notes[i];
    if (n.spineIndex == spineIndex && n.startPage == startPage && n.startWordIndex == startWordIndex) {
      return i;
    }
  }
  return -1;
}

const Note* NoteStore::getNoteForClipping(uint16_t spineIndex, uint16_t startPage, uint16_t startWordIndex) const {
  const int idx = findNoteIndex(spineIndex, startPage, startWordIndex);
  return idx >= 0 ? &notes[idx] : nullptr;
}

// ─── Save / Delete ────────────────────────────────────────────────────────────

bool NoteStore::saveNote(const char* filePath, uint16_t spineIndex, uint16_t startPage, uint16_t startWordIndex,
                         const char* text) {
  // Auto-load if needed (e.g. called from EpubReaderActivity while NOTES not loaded)
  if (!loaded || bookFilePath != filePath) {
    loadForBook(filePath, "epub");
  }

  const std::string path = notesFilePath(filePath);
  const int idx = findNoteIndex(spineIndex, startPage, startWordIndex);

  if (idx >= 0) {
    // Update existing — preserve tag
    notes[idx].text = std::string(text).substr(0, NOTE_TEXT_MAX);
    notes[idx].timestamp = millis();
  } else {
    // Create new
    Note note;
    note.spineIndex = spineIndex;
    note.startPage = startPage;
    note.startWordIndex = startWordIndex;
    note.text = std::string(text).substr(0, NOTE_TEXT_MAX);
    note.timestamp = millis();
    note.tag = 0;
    notes.push_back(std::move(note));
  }

  return saveToFile(path);
}

bool NoteStore::saveTag(const char* filePath, uint16_t spineIndex, uint16_t startPage, uint16_t startWordIndex,
                        char tag) {
  // Auto-load if needed
  if (!loaded || bookFilePath != filePath) {
    loadForBook(filePath, "epub");
  }

  const std::string path = notesFilePath(filePath);
  const int idx = findNoteIndex(spineIndex, startPage, startWordIndex);

  if (idx >= 0) {
    // Update existing — preserve text
    notes[idx].tag = tag;
    notes[idx].timestamp = millis();
  } else {
    // Create new note with empty text and just the tag
    Note note;
    note.spineIndex = spineIndex;
    note.startPage = startPage;
    note.startWordIndex = startWordIndex;
    note.text = "";
    note.timestamp = millis();
    note.tag = tag;
    notes.push_back(std::move(note));
  }

  return saveToFile(path);
}

bool NoteStore::deleteNote(const char* filePath, uint16_t spineIndex, uint16_t startPage, uint16_t startWordIndex) {
  if (!loaded || bookFilePath != filePath) {
    loadForBook(filePath, "epub");
  }

  const std::string path = notesFilePath(filePath);
  const int idx = findNoteIndex(spineIndex, startPage, startWordIndex);
  if (idx < 0) return true;  // Already gone

  notes.erase(notes.begin() + idx);
  return saveToFile(path);
}

// ─── Migration ────────────────────────────────────────────────────────────────

void NoteStore::migrateForFilePath(const std::string& oldPath, const std::string& newPath) {
  const std::string oldFile = notesFilePath(oldPath.c_str());
  const std::string newFile = notesFilePath(newPath.c_str());
  if (Storage.exists(oldFile.c_str()) && !Storage.exists(newFile.c_str())) {
    Storage.rename(oldFile.c_str(), newFile.c_str());
  }
}
