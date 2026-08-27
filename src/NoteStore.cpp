#include "NoteStore.h"

#include <Arduino.h>  // for millis()
#include <uzlib.h>

#include <algorithm>
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

void NoteStore::recoverIfInterrupted(const std::string& path) {
  // A save that was cut short between setting the old file aside and moving the
  // new one in leaves the notes under .bak. Without this the book would simply
  // read as having none.
  if (Storage.exists(path.c_str())) return;
  const std::string bakPath = path + ".bak";
  if (!Storage.exists(bakPath.c_str())) return;
  if (Storage.rename(bakPath.c_str(), path.c_str())) {
    LOG_INF(LOG_TAG, "Recovered interrupted notes save: %s", path.c_str());
  }
}

void NoteStore::loadForBook(const char* filePath, const char* /*bookType*/) {
  // Retry when the last attempt failed. A momentary SD hiccup sets loadFailed
  // exactly like real corruption does, and without this the early return meant
  // reopening the same book never tried again — saving stayed blocked for the
  // rest of the session unless the user happened to visit another book first.
  // Re-reading costs one file open; genuine corruption simply fails again.
  if (loaded && !loadFailed && bookFilePath == filePath) return;
  unload();
  bookFilePath = filePath;
  const std::string path = notesFilePath(filePath);
  recoverIfInterrupted(path);
  if (Storage.exists(path.c_str())) {
    if (!loadFromFile(path)) {
      LOG_ERR(LOG_TAG, "Failed to load notes from %s", path.c_str());
      loadFailed = true;  // see saveToFile: do not write over what we could not read
    }
  }
  loaded = true;
}

void NoteStore::unload() {
  loadFailed = false;
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
  const int fileVersion = doc["v"] | 0;  // 0 = written before the marker existed
  if (fileVersion > kNotesFileVersion) {
    LOG_ERR(LOG_TAG, "Notes file %s is version %d, newer than this firmware understands (%d) — reading what it can",
            path.c_str(), fileVersion, kNotesFileVersion);
  }
  const JsonArray arr = doc["notes"].as<JsonArray>();
  for (const JsonVariant entry : arr) {
    // A non-object row (e.g. a hand-edited "notes": [1,2,3]) would otherwise
    // deserialise entirely to defaults and be kept as a phantom note keyed
    // (0,0,0,0) — which can then match a real first clipping.
    if (!entry.is<JsonObject>()) continue;
    const JsonObject obj = entry.as<JsonObject>();
    Note note;
    note.spineIndex = obj["spineIndex"] | uint16_t(0);
    note.startPage = obj["startPage"] | uint16_t(0);
    note.startWordIndex = obj["startWordIndex"] | uint16_t(0);
    // Missing (pre-migration files) defaults to 0 — the legacy sentinel.
    note.clippingTimestamp = obj["clippingTimestamp"] | uint32_t(0);
    note.text = obj["text"] | std::string{};
    // kNoteTextMax was only enforced on save. A hand-edited file could hold a
    // multi-megabyte note, and this runs on every open of that book.
    if (note.text.size() > NOTE_TEXT_MAX) note.text.resize(NOTE_TEXT_MAX);
    note.timestamp = obj["timestamp"] | uint32_t(0);
    // tag stored as a 1-character string; 0 / missing = no tag
    const char* tagStr = obj["tag"] | "";
    note.tag = tagStr[0];  // '\0' if missing or empty — correct default
    notes.push_back(std::move(note));
  }
  return true;
}

bool NoteStore::saveToFile(const std::string& path) const {
  // The notes in memory are only a faithful picture of this file if it actually
  // parsed. If it did not, `notes` is empty, and writing it back would replace a
  // corrupt-but-present file with an empty one — turning a recoverable problem
  // into permanent loss on the user's next ordinary edit. Refuse instead; the
  // caller surfaces that as "Could not save note".
  if (loadFailed) {
    LOG_ERR(LOG_TAG, "Refusing to overwrite %s — its existing contents could not be read", path.c_str());
    return false;
  }
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
  doc["v"] = kNotesFileVersion;
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

  // Compare against the expected length, not just against zero. On a full card
  // serializeJson returns a short-but-nonzero count, which a "== 0" check lets
  // through — the truncated .tmp then gets renamed over a perfectly good file
  // and the .bak is deleted in the same breath, destroying the notes while
  // reporting success. Checked here, before anything is renamed.
  const size_t expected = measureJson(doc);
  const size_t written = serializeJson(doc, file);
  const bool synced = file.sync();
  file.close();
  if (!synced || written != expected) {
    LOG_ERR(LOG_TAG, "Short write to %s (%u of %u bytes, sync %s) — card full?", tmpPath.c_str(),
            static_cast<unsigned>(written), static_cast<unsigned>(expected), synced ? "ok" : "FAILED");
    Storage.remove(tmpPath.c_str());
    return false;
  }

  // Swap the new file in with no moment where neither exists. Removing the
  // original first and then renaming — the obvious way — leaves the book with
  // NO notes file at all if power is lost in between, losing every note it
  // ever had rather than just this edit. Keep the old one aside as .bak until
  // the new one is in place, and recover from it on load. ClippingStore does
  // the same for the same reason.
  const std::string bakPath = path + ".bak";
  if (Storage.exists(path.c_str())) {
    if (Storage.exists(bakPath.c_str())) Storage.remove(bakPath.c_str());
    if (!Storage.rename(path.c_str(), bakPath.c_str())) {
      LOG_ERR(LOG_TAG, "Could not set %s aside before replacing it", path.c_str());
      Storage.remove(tmpPath.c_str());
      return false;
    }
  }
  if (!Storage.rename(tmpPath.c_str(), path.c_str())) {
    LOG_ERR(LOG_TAG, "Could not move %s into place", tmpPath.c_str());
    if (Storage.exists(bakPath.c_str())) Storage.rename(bakPath.c_str(), path.c_str());
    return false;
  }
  if (Storage.exists(bakPath.c_str())) Storage.remove(bakPath.c_str());
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

bool NoteStore::saveNoteAndTag(const char* filePath, uint16_t spineIndex, uint16_t startPage, uint16_t startWordIndex,
                               uint32_t clippingTimestamp, const char* text, char tag, bool applyTag) {
  // Auto-load if needed (e.g. called from EpubReaderActivity while NOTES not loaded)
  if (!loaded || bookFilePath != filePath) {
    loadForBook(filePath, "epub");
  }

  const std::string path = notesFilePath(filePath);
  const int idx = findNoteIndex(spineIndex, startPage, startWordIndex, clippingTimestamp);

  if (idx >= 0) {
    // Update existing. Also migrates a legacy (0) key forward.
    notes[idx].clippingTimestamp = clippingTimestamp;
    if (text != nullptr) notes[idx].text = std::string(text).substr(0, NOTE_TEXT_MAX);
    if (applyTag) notes[idx].tag = tag;
    notes[idx].timestamp = millis();
  } else {
    Note note;
    note.spineIndex = spineIndex;
    note.startPage = startPage;
    note.startWordIndex = startWordIndex;
    note.clippingTimestamp = clippingTimestamp;
    note.text = text != nullptr ? std::string(text).substr(0, NOTE_TEXT_MAX) : std::string();
    note.tag = applyTag ? tag : 0;
    note.timestamp = millis();
    notes.push_back(std::move(note));
  }

  // Drop every note with no text and no tag — not just the one being edited.
  // Tag-clear and web portal writes can leave blank shells; clearing one note
  // on a legacy file also rewrites away any others in the same pass.
  notes.erase(std::remove_if(notes.begin(), notes.end(),
                             [](const Note& n) { return n.text.empty() && n.tag == 0; }),
              notes.end());
  // idx is stale after the sweep above; nothing below must use it.

  return saveToFile(path);
}

bool NoteStore::saveNote(const char* filePath, uint16_t spineIndex, uint16_t startPage, uint16_t startWordIndex,
                         uint32_t clippingTimestamp, const char* text) {
  return saveNoteAndTag(filePath, spineIndex, startPage, startWordIndex, clippingTimestamp, text, 0, false);
}

bool NoteStore::saveTag(const char* filePath, uint16_t spineIndex, uint16_t startPage, uint16_t startWordIndex,
                        uint32_t clippingTimestamp, char tag) {
  return saveNoteAndTag(filePath, spineIndex, startPage, startWordIndex, clippingTimestamp, nullptr, tag, true);
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

uint16_t NoteStore::bindLegacyNotes(const char* filePath, const std::vector<ClippingKey>& live) {
  if (!loaded || bookFilePath != filePath) return 0;
  if (live.empty()) return 0;  // same reasoning as pruneMissing: never act on a failed load

  uint16_t bound = 0;
  for (Note& n : notes) {
    if (n.clippingTimestamp != 0) continue;  // already bound
    for (const ClippingKey& k : live) {
      if (k.spineIndex != n.spineIndex || k.startPage != n.startPage || k.startWordIndex != n.startWordIndex) {
        continue;
      }
      // A clipping made in the first second after boot has timestamp 0 itself;
      // binding to it would leave the note legacy, so skip rather than churn.
      if (k.timestamp != 0) {
        n.clippingTimestamp = k.timestamp;
        ++bound;
      }
      break;  // first clipping at this position wins
    }
  }

  if (bound > 0) {
    saveToFile(notesFilePath(filePath));
    LOG_INF(LOG_TAG, "Bound %u legacy note(s) to their clipping for %s", bound, filePath);
  }
  return bound;
}

uint16_t NoteStore::pruneMissing(const char* filePath, const std::vector<ClippingKey>& live) {
  if (!loaded || bookFilePath != filePath) return 0;
  // Never prune against an empty set. A book with no clippings is
  // indistinguishable here from one whose clipping file failed to load, and
  // treating the latter as "everything is orphaned" would delete every note the
  // book has. Refusing to act costs only a stale count until the next visit.
  if (live.empty()) return 0;

  const size_t before = notes.size();
  notes.erase(std::remove_if(notes.begin(), notes.end(),
                             [&live](const Note& n) {
                               for (const auto& k : live) {
                                 if (k.spineIndex != n.spineIndex || k.startPage != n.startPage ||
                                     k.startWordIndex != n.startWordIndex) {
                                   continue;
                                 }
                                 // Legacy notes (clippingTimestamp 0) match on position alone.
                                 if (n.clippingTimestamp == 0 || n.clippingTimestamp == k.timestamp) return false;
                               }
                               return true;  // no surviving clipping — drop it
                             }),
              notes.end());

  const auto removed = static_cast<uint16_t>(before - notes.size());
  if (removed > 0) {
    const std::string path = notesFilePath(filePath);
    if (notes.empty()) {
      if (Storage.exists(path.c_str())) Storage.remove(path.c_str());
    } else {
      saveToFile(path);
    }
    LOG_INF(LOG_TAG, "Pruned %u orphaned note(s) for %s", removed, filePath);
  }
  return removed;
}

uint16_t NoteStore::countForFilePath(const std::string& filePath) {
  const std::string path = notesFilePath(filePath.c_str());
  if (!Storage.exists(path.c_str())) return 0;
  FsFile file = Storage.open(path.c_str(), O_RDONLY);
  if (!file) return 0;

  // Home screen only needs how many notes exist — not their text. Filtering
  // text out keeps peak heap near N × tiny metadata instead of file size.
  // Keep spineIndex (always written) so the filter does not suggest that
  // tagged-ness affects the count — we still count every object row.
  JsonDocument filter;
  filter["notes"][0]["spineIndex"] = true;
  JsonDocument doc;
  const DeserializationError err =
      deserializeJson(doc, file, DeserializationOption::Filter(filter.as<JsonVariantConst>()));
  file.close();
  if (err) return 0;

  // Count objects only, matching loadFromFile — non-object rows in a hand-edited
  // array must not inflate the badge relative to what the list would show.
  uint16_t count = 0;
  for (const JsonVariant entry : doc["notes"].as<JsonArray>()) {
    if (entry.is<JsonObject>()) count++;
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
