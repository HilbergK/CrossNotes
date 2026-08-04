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
    // Missing (pre-migration files) defaults to 0 — the legacy sentinel.
    note.clippingTimestamp = obj["clippingTimestamp"] | uint32_t(0);
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
    if (note.clippingTimestamp != 0) {
      obj["clippingTimestamp"] = note.clippingTimestamp;
    }
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

int NoteStore::findNoteIndex(uint16_t spineIndex, uint16_t startPage, uint16_t startWordIndex,
                             uint32_t clippingTimestamp) const {
  // Exact match, including the clipping's own timestamp — the reliable path.
  // (spineIndex, startPage, startWordIndex) alone can collide between two
  // distinct clippings (ClippingStore's own equality checks add timestamp
  // for the same reason), which previously caused a note/tag written for
  // one highlight to silently show up on a different, unrelated one.
  for (int i = 0; i < static_cast<int>(notes.size()); i++) {
    const Note& n = notes[i];
    if (n.spineIndex == spineIndex && n.startPage == startPage && n.startWordIndex == startWordIndex &&
        n.clippingTimestamp == clippingTimestamp) {
      return i;
    }
  }
  // Legacy fallback: notes saved before clippingTimestamp existed have it
  // set to 0. Match those on the old 3-field key so they aren't orphaned by
  // this change. Callers migrate the record forward by writing the real
  // clippingTimestamp on next save (see saveNote/saveTag below).
  if (clippingTimestamp != 0) {
    for (int i = 0; i < static_cast<int>(notes.size()); i++) {
      const Note& n = notes[i];
      if (n.spineIndex == spineIndex && n.startPage == startPage && n.startWordIndex == startWordIndex &&
          n.clippingTimestamp == 0) {
        return i;
      }
    }
  }
  return -1;
}

const Note* NoteStore::getNoteForClipping(uint16_t spineIndex, uint16_t startPage, uint16_t startWordIndex,
                                          uint32_t clippingTimestamp) const {
  const int idx = findNoteIndex(spineIndex, startPage, startWordIndex, clippingTimestamp);
  return idx >= 0 ? &notes[idx] : nullptr;
}

// ─── Save / Delete ────────────────────────────────────────────────────────────

bool NoteStore::saveNote(const char* filePath, uint16_t spineIndex, uint16_t startPage, uint16_t startWordIndex,
                         uint32_t clippingTimestamp, const char* text) {
  // Auto-load if needed (e.g. called from EpubReaderActivity while NOTES not loaded)
  if (!loaded || bookFilePath != filePath) {
    loadForBook(filePath, "epub");
  }

  const std::string path = notesFilePath(filePath);
  const int idx = findNoteIndex(spineIndex, startPage, startWordIndex, clippingTimestamp);

  if (idx >= 0) {
    // Update existing — preserve tag. Also migrates a legacy (0) key forward.
    notes[idx].clippingTimestamp = clippingTimestamp;
    notes[idx].text = std::string(text).substr(0, NOTE_TEXT_MAX);
    notes[idx].timestamp = millis();
  } else {
    // Create new
    Note note;
    note.spineIndex = spineIndex;
    note.startPage = startPage;
    note.startWordIndex = startWordIndex;
    note.clippingTimestamp = clippingTimestamp;
    note.text = std::string(text).substr(0, NOTE_TEXT_MAX);
    note.timestamp = millis();
    note.tag = 0;
    notes.push_back(std::move(note));
  }

  return saveToFile(path);
}

bool NoteStore::saveTag(const char* filePath, uint16_t spineIndex, uint16_t startPage, uint16_t startWordIndex,
                        uint32_t clippingTimestamp, char tag) {
  // Auto-load if needed
  if (!loaded || bookFilePath != filePath) {
    loadForBook(filePath, "epub");
  }

  const std::string path = notesFilePath(filePath);
  const int idx = findNoteIndex(spineIndex, startPage, startWordIndex, clippingTimestamp);

  if (idx >= 0) {
    // Update existing — preserve text. Also migrates a legacy (0) key forward.
    notes[idx].clippingTimestamp = clippingTimestamp;
    notes[idx].tag = tag;
    notes[idx].timestamp = millis();
  } else {
    // Create new note with empty text and just the tag
    Note note;
    note.spineIndex = spineIndex;
    note.startPage = startPage;
    note.startWordIndex = startWordIndex;
    note.clippingTimestamp = clippingTimestamp;
    note.text = "";
    note.timestamp = millis();
    note.tag = tag;
    notes.push_back(std::move(note));
  }

  return saveToFile(path);
}

bool NoteStore::deleteNote(const char* filePath, uint16_t spineIndex, uint16_t startPage, uint16_t startWordIndex,
                           uint32_t clippingTimestamp) {
  if (!loaded || bookFilePath != filePath) {
    loadForBook(filePath, "epub");
  }

  const std::string path = notesFilePath(filePath);
  const int idx = findNoteIndex(spineIndex, startPage, startWordIndex, clippingTimestamp);
  if (idx < 0) return true;  // Already gone

  notes.erase(notes.begin() + idx);
  return saveToFile(path);
}

// ─── Migration / bulk delete ────────────────────────────────────────────────

uint16_t NoteStore::countForFilePath(const std::string& filePath) {
  const std::string path = notesFilePath(filePath.c_str());
  if (!Storage.exists(path.c_str())) return 0;
  FsFile file = Storage.open(path.c_str(), O_RDONLY);
  if (!file) return 0;
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, file);
  file.close();
  if (err) return 0;
  uint16_t count = 0;
  for (const JsonObject obj : doc["notes"].as<JsonArray>()) {
    const char* text = obj["text"] | "";
    const char* tag = obj["tag"] | "";
    if ((text != nullptr && text[0] != ' ') || (tag != nullptr && tag[0] != ' ')) count++;
  }
  return count;
}

void NoteStore::deleteForFilePath(const std::string& filePath) {
  const std::string path = notesFilePath(filePath.c_str());
  if (Storage.exists(path.c_str())) {
    Storage.remove(path.c_str());
  }
  // If the singleton is holding this book's notes in memory, drop them so a
  // later loadForBook() doesn't short-circuit and show stale, file-less notes.
  NoteStore& inst = getInstance();
  if (inst.loaded && inst.bookFilePath == filePath) {
    inst.unload();
  }
}

void NoteStore::migrateForFilePath(const std::string& oldPath, const std::string& newPath) {
  const std::string oldFile = notesFilePath(oldPath.c_str());
  const std::string newFile = notesFilePath(newPath.c_str());
  if (Storage.exists(oldFile.c_str()) && !Storage.exists(newFile.c_str())) {
    Storage.rename(oldFile.c_str(), newFile.c_str());
  }
  // Drop stale in-memory state for the old path so a subsequent save can't
  // recreate the file at the old location.
  NoteStore& inst = getInstance();
  if (inst.loaded && inst.bookFilePath == oldPath) {
    inst.unload();
  }
}
