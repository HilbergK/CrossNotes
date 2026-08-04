#include "EpubReaderClippingListActivity.h"

#include <Arduino.h>  // for delay()
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "MappedInputManager.h"
#include "activities/ActivityResult.h"
#include "activities/home/BookActions.h"
#include "activities/home/FileBrowserActionActivity.h"
#include "activities/reader/TagPickerActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/TouchHeaderBackButton.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
constexpr fui::ActionId ACTION_ROW = 1;
// CrossInk Notes: face used for the row subtitle (tag + chapter + note). Kept
// as a single constant because it is bound to the list's FONT_SMALL slot AND
// used to measure/truncate the text, which must agree.
constexpr int SUBTITLE_FONT_ID = SMALL_FONT_ID;
constexpr int DETAIL_START_Y = 70;
constexpr int DETAIL_SIDE_MARGIN = 20;
constexpr int DETAIL_BOTTOM_RESERVE = 55;
constexpr int DETAIL_LINE_GAP = 6;
constexpr int TOUCH_DETAIL_BUTTON_HEIGHT = 52;
constexpr int TOUCH_DETAIL_PAGE_LABEL_RESERVE = 24;
constexpr unsigned long CLIPPING_DELETE_HOLD_MS = 1000;
// KeyboardEntryActivity's line-wrapping re-measures the whole remaining
// string on every trimmed character while it hunts for a fitting line
// width — effectively O(n^2) in text length. That's fine for normal notes
// but a very long note (e.g. written on the phone, up to kNoteTextMax)
// can take long enough to trip the watchdog. Block on-device editing above
// this length rather than risk a reboot; such notes are still fully
// editable from Notes Connect. Kept low (not just under whatever threshold
// happened not to crash) since typing on-device is slow anyway — long
// notes belong on the phone regardless of the crash risk.
constexpr size_t EDIT_NOTE_MAX_LENGTH = 250;

Rect clippingHeaderRect(const Rect& safe, const ThemeMetrics& metrics, const MappedInputManager& mappedInput) {
  return Rect{safe.x, safe.y + metrics.topPadding, safe.width, TouchHeaderBackButton::height(metrics, mappedInput)};
}

Rect touchDetailOpenButtonRect(const Rect& safe, const ThemeMetrics& metrics) {
  const int sidePadding = std::min(metrics.contentSidePadding, std::max(0, safe.width / 2 - 1));
  return Rect{safe.x + sidePadding, safe.y + safe.height - metrics.verticalSpacing - TOUCH_DETAIL_BUTTON_HEIGHT,
              std::max(1, safe.width - sidePadding * 2), TOUCH_DETAIL_BUTTON_HEIGHT};
}

bool isUtf8SpaceAt(const std::string& text, const size_t index, size_t& advance) {
  const auto c = static_cast<unsigned char>(text[index]);
  if (c == 0xC2 && index + 1 < text.size() && static_cast<unsigned char>(text[index + 1]) == 0xA0) {
    advance = 2;
    return true;
  }
  if (c == 0xE2 && index + 2 < text.size() && static_cast<unsigned char>(text[index + 1]) == 0x80) {
    const auto c2 = static_cast<unsigned char>(text[index + 2]);
    if (c2 == 0x83 || c2 == 0xAF) {
      advance = 3;
      return true;
    }
  }
  return false;
}

void buildOneLineSnippetText(const std::string& text, std::string& out) {
  out.clear();
  bool lastWasSpace = true;
  for (size_t i = 0; i < text.size();) {
    size_t advance = 0;
    if (isUtf8SpaceAt(text, i, advance)) {
      if (!lastWasSpace) {
        out += ' ';
        lastWasSpace = true;
      }
      i += advance;
      continue;
    }

    const char c = text[i++];
    if (c == '\r' || c == '\n' || c == '\t') {
      if (!lastWasSpace) {
        out += ' ';
        lastWasSpace = true;
      }
      continue;
    }
    out += c;
    lastWasSpace = c == ' ';
  }
  while (!out.empty() && out.back() == ' ') {
    out.pop_back();
  }
}

size_t utf8CharLen(const std::string& text, const size_t index) {
  const auto c = static_cast<unsigned char>(text[index]);
  if ((c & 0x80) == 0) return 1;
  if ((c & 0xE0) == 0xC0 && index + 1 < text.size()) return 2;
  if ((c & 0xF0) == 0xE0 && index + 2 < text.size()) return 3;
  if ((c & 0xF8) == 0xF0 && index + 3 < text.size()) return 4;
  return 1;
}

void appendLongWordLines(const GfxRenderer& renderer, const int fontId, const std::string& word, const int maxWidth,
                         std::vector<std::string>& out) {
  std::string line;
  for (size_t i = 0; i < word.size();) {
    const size_t charLen = utf8CharLen(word, i);
    const std::string next = word.substr(i, charLen);
    const std::string candidate = line + next;
    if (!line.empty() && renderer.getTextWidth(fontId, candidate.c_str()) > maxWidth) {
      out.push_back(line);
      line = next;
    } else {
      line = candidate;
    }
    i += charLen;
  }
  if (!line.empty()) out.push_back(line);
}

void appendWrappedWord(const GfxRenderer& renderer, const int fontId, const std::string& word, const int maxWidth,
                       std::string& currentLine, std::vector<std::string>& out) {
  if (word.empty()) return;

  if (renderer.getTextWidth(fontId, word.c_str()) > maxWidth) {
    if (!currentLine.empty()) {
      out.push_back(currentLine);
      currentLine.clear();
    }
    appendLongWordLines(renderer, fontId, word, maxWidth, out);
    return;
  }

  const std::string candidate = currentLine.empty() ? word : currentLine + " " + word;
  if (renderer.getTextWidth(fontId, candidate.c_str()) <= maxWidth) {
    currentLine = candidate;
    return;
  }

  if (!currentLine.empty()) out.push_back(currentLine);
  currentLine = word;
}

void buildWrappedDetailLines(const GfxRenderer& renderer, const int fontId, const std::string& text, const int maxWidth,
                             std::vector<std::string>& out) {
  out.clear();
  if (maxWidth <= 0) return;

  std::string currentLine;
  size_t wordStart = 0;
  while (wordStart < text.size()) {
    while (wordStart < text.size() && text[wordStart] == ' ') {
      wordStart++;
    }
    if (wordStart >= text.size()) break;

    size_t wordEnd = wordStart;
    while (wordEnd < text.size() && text[wordEnd] != ' ') {
      wordEnd++;
    }

    appendWrappedWord(renderer, fontId, text.substr(wordStart, wordEnd - wordStart), maxWidth, currentLine, out);
    wordStart = wordEnd;
  }

  if (!currentLine.empty()) out.push_back(currentLine);
}
}  // namespace

EpubReaderClippingListActivity::EpubReaderClippingListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("EpubClippingList", renderer, mappedInput),
      uiTarget(makeUiTarget(renderer)),
      app(uiTarget, uiTarget.deviceContext()) {}

void EpubReaderClippingListActivity::rebuildVisibleClippings() {
  visibleClippings.clear();
  const size_t total = CLIPPINGS.clippingCount();
  visibleClippings.reserve(total);
  for (size_t i = 0; i < total; ++i) {
    if (tagFilter != 0) {
      const Clipping* c = CLIPPINGS.clippingAt(i);
      if (!c) continue;
      const Note* note = NOTES.getNoteForClipping(c->spineIndex, c->startPage, c->startWordIndex, c->timestamp);
      if (note == nullptr || note->tag != tagFilter) continue;
    }
    visibleClippings.push_back(static_cast<uint16_t>(i));
  }
  const int count = visibleCount();
  if (selectedIndex >= count) selectedIndex = count > 0 ? count - 1 : 0;
  if (selectedIndex < 0) selectedIndex = 0;
  if (topIndex > selectedIndex) topIndex = selectedIndex;
  uiItems.resize(visibleClippings.size());
}

void EpubReaderClippingListActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  topIndex = 0;
  visibleRows = 1;
  uiReady = false;
  app.setTheme(uiThemeTokens(uiTarget));
  app.on(ACTION_ROW, &EpubReaderClippingListActivity::onRowEvent, this);
  app.setScreen(&EpubReaderClippingListActivity::listScreen, this);
  detailText.reserve(CLIPPING_TEXT_MAX);
  detailLines.reserve(32);
  // CrossInk Notes: the list now reads clippings from the store, so take the
  // book path from there too.
  if (!CLIPPINGS.getBookFilePath().empty()) {
    NOTES.loadForBook(CLIPPINGS.getBookFilePath().c_str(), "epub");
  }
  rebuildVisibleClippings();
  requestUpdate();
}

void EpubReaderClippingListActivity::onExit() {
  if (!CLIPPINGS.getBookFilePath().empty()) NOTES.unload();
  Activity::onExit();
}

int EpubReaderClippingListActivity::getDetailTextWidth() const {
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  return std::max(1, safe.width - DETAIL_SIDE_MARGIN * 2);
}

int EpubReaderClippingListActivity::getDetailLinesPerPage() const {
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int lineStep = renderer.getLineHeight(UI_10_FONT_ID) + DETAIL_LINE_GAP;
#if CROSSINK_APP_CAP_TOUCH
  if (mappedInput.hasTouchHardware()) {
    const Rect header = clippingHeaderRect(safe, metrics, mappedInput);
    const Rect openButton = touchDetailOpenButtonRect(safe, metrics);
    const int textStart = header.y + header.height + metrics.verticalSpacing;
    const int textBottom = openButton.y - metrics.verticalSpacing - TOUCH_DETAIL_PAGE_LABEL_RESERVE;
    return std::max(1, (textBottom - textStart) / std::max(1, lineStep));
  }
#endif
  const int available = safe.height - DETAIL_START_Y - DETAIL_BOTTOM_RESERVE;
  return std::max(1, available / std::max(1, lineStep));
}

int EpubReaderClippingListActivity::getDetailPageCount() const {
  if (detailLinesPerPage <= 0) return 1;
  return std::max(1, static_cast<int>((detailLines.size() + detailLinesPerPage - 1) / detailLinesPerPage));
}

void EpubReaderClippingListActivity::closeDetail() {
  detailMode = false;
  detailPage = 0;
  detailText.clear();
  detailLines.clear();
  detailLayoutWidth = 0;
  detailLinesPerPage = 0;
  requestUpdate();
}

void EpubReaderClippingListActivity::jumpToSelectedClipping() {
  if (selectedIndex < 0 || selectedIndex >= visibleCount()) return;

  const Clipping* clipping = CLIPPINGS.clippingAt(storeIndexFor(selectedIndex));
  if (!clipping) return;
  setResult(ClippingJumpResult{clipping->spineIndex, clipping->startPage, clipping->pageCount, clipping->paragraphIndex,
                               static_cast<uint16_t>(selectedIndex)});
  finish();
}

void EpubReaderClippingListActivity::openSelectedDetail() {
  if (selectedIndex < 0 || selectedIndex >= visibleCount()) return;

  std::string text;
  text.reserve(CLIPPING_TEXT_MAX);
  if (!CLIPPINGS.readClippingText(storeIndexFor(selectedIndex), text)) {
    text.clear();
  }
  buildOneLineSnippetText(text, detailText);
  detailMode = true;
  detailPage = 0;
  detailLayoutWidth = 0;
  detailLinesPerPage = 0;
  rebuildDetailLayoutIfNeeded();
  requestUpdate();
}

void EpubReaderClippingListActivity::rebuildDetailLayoutIfNeeded() {
  const int textWidth = getDetailTextWidth();
  const int linesPerPage = getDetailLinesPerPage();
  if (textWidth == detailLayoutWidth && linesPerPage == detailLinesPerPage && !detailLines.empty()) return;

  buildWrappedDetailLines(renderer, UI_10_FONT_ID, detailText, textWidth, detailLines);
  if (detailLines.empty()) detailLines.push_back("");

  // Append the note (if any) directly below the quote, as part of the same
  // scrollable/paginated flow, so long notes remain fully readable.
  const Clipping* detailClipping =
      selectedIndex >= 0 ? CLIPPINGS.clippingAt(storeIndexFor(selectedIndex)) : nullptr;
  if (detailClipping) {
    const Clipping& clipping = *detailClipping;
    const Note* note =
        NOTES.getNoteForClipping(clipping.spineIndex, clipping.startPage, clipping.startWordIndex, clipping.timestamp);
    if (note != nullptr && (note->tag != 0 || !note->text.empty())) {
      detailLines.push_back("");
      std::string label = "Note";
      if (note->tag != 0) {
        label += " [";
        label += note->tag;
        label += "]";
      }
      label += ":";
      detailLines.push_back(std::move(label));
      if (!note->text.empty()) {
        std::string noteFlat;
        buildOneLineSnippetText(note->text, noteFlat);
        std::vector<std::string> noteLines;
        buildWrappedDetailLines(renderer, UI_10_FONT_ID, noteFlat, textWidth, noteLines);
        detailLines.insert(detailLines.end(), noteLines.begin(), noteLines.end());
      }
    }
  }

  detailLayoutWidth = textWidth;
  detailLinesPerPage = linesPerPage;
  detailPage = std::min(detailPage, getDetailPageCount() - 1);
}

void EpubReaderClippingListActivity::deleteSelectedClipping() {
  if (selectedIndex < 0 || selectedIndex >= visibleCount()) return;

  // Remove the clipping's note/tag along with it — otherwise the note record
  // would sit orphaned in the notes file forever.
  if (!CLIPPINGS.getBookFilePath().empty()) {
    const Clipping* doomedPtr = CLIPPINGS.clippingAt(storeIndexFor(selectedIndex));
    if (doomedPtr) {
      const Clipping& doomed = *doomedPtr;
      NOTES.deleteNote(CLIPPINGS.getBookFilePath().c_str(), doomed.spineIndex, doomed.startPage,
                       doomed.startWordIndex, doomed.timestamp);
    }
  }

  if (!CLIPPINGS.removeClippingAt(storeIndexFor(selectedIndex))) return;

  detailMode = false;
  detailText.clear();
  detailLines.clear();
  detailLayoutWidth = 0;
  detailLinesPerPage = 0;
  // Rebuild first: the store just shrank, so every index in visibleClippings
  // past the removed one is stale. Clamping the selection before rebuilding
  // would measure it against the old list.
  rebuildVisibleClippings();
  if (visibleCount() == 0) {
    selectedIndex = 0;
  } else if (selectedIndex >= visibleCount()) {
    selectedIndex = visibleCount() - 1;
  }
  topIndex = followListSelection(selectedIndex, topIndex, visibleRows, visibleCount());
  requestUpdate();
}

void EpubReaderClippingListActivity::showTagFilterMenu() {
  // Offer only the tags this book actually uses, plus "All" to clear the
  // filter — an alphabet of unused symbols would be noise.
  std::vector<char> tags;
  for (size_t i = 0; i < CLIPPINGS.clippingCount(); ++i) {
    const Clipping* c = CLIPPINGS.clippingAt(i);
    if (!c) continue;
    const Note* note = NOTES.getNoteForClipping(c->spineIndex, c->startPage, c->startWordIndex, c->timestamp);
    if (note == nullptr || note->tag == 0) continue;
    if (std::find(tags.begin(), tags.end(), note->tag) == tags.end()) tags.push_back(note->tag);
  }
  std::sort(tags.begin(), tags.end());

  if (tags.empty()) {
    BookActions::drawToast(renderer, tr(STR_NO_CLIPPINGS));
    delay(1000);
    requestUpdate();
    return;
  }

  std::vector<std::string> labels;
  labels.reserve(tags.size() + 1);
  labels.emplace_back(tr(STR_ALL_TAGS));
  for (const char t : tags) labels.emplace_back(1, t);

  int selected = 0;
  if (tagFilter != 0) {
    const auto it = std::find(tags.begin(), tags.end(), tagFilter);
    if (it != tags.end()) selected = static_cast<int>(std::distance(tags.begin(), it)) + 1;
  }

  optionPopup.show(StrId::STR_FILTER_BY_TAG, labels, selected,
                   [this, tags](const int idx) {
                     tagFilter = (idx <= 0) ? 0 : tags[static_cast<size_t>(idx - 1)];
                     selectedIndex = 0;
                     topIndex = 0;
                     rebuildVisibleClippings();
                     requestUpdate();
                   });
  requestUpdate();
}

void EpubReaderClippingListActivity::showClippingActionMenu(const bool ignoreInitialConfirmRelease) {
  if (selectedIndex < 0 || selectedIndex >= visibleCount()) return;

  const Clipping* selectedClipping = CLIPPINGS.clippingAt(storeIndexFor(selectedIndex));
  if (!selectedClipping) return;
  const char* title = selectedClipping->chapterTitle[0] != '\0' ? selectedClipping->chapterTitle : tr(STR_CLIPPINGS);
  const uint16_t selectedSpineIndex = selectedClipping->spineIndex;
  const uint16_t selectedStartPage = selectedClipping->startPage;
  const uint16_t selectedStartWordIndex = selectedClipping->startWordIndex;
  const uint32_t selectedTimestamp = selectedClipping->timestamp;
  std::vector<FileBrowserActionActivity::MenuItem> items;
  items.reserve(4);
  items.push_back({FileBrowserAction::OpenClipping, StrId::STR_OPEN});
  if (!CLIPPINGS.getBookFilePath().empty()) {
    items.push_back({FileBrowserAction::EditTag, StrId::STR_EDIT_TAG});
    items.push_back({FileBrowserAction::EditNote, StrId::STR_EDIT_NOTE});
  }
  items.push_back({FileBrowserAction::FilterTag, StrId::STR_FILTER_BY_TAG});
  items.push_back({FileBrowserAction::Delete, StrId::STR_DELETE});

  startActivityForResult(
      std::make_unique<FileBrowserActionActivity>(renderer, mappedInput, title, std::move(items),
                                                  ignoreInitialConfirmRelease),
      [this, selectedSpineIndex, selectedStartPage, selectedStartWordIndex,
       selectedTimestamp](const ActivityResult& result) {
        longPressConfirmHandled = false;
        if (result.isCancelled) {
          requestUpdate();
          return;
        }

        const auto* actionResult = std::get_if<FileBrowserActionResult>(&result.data);
        if (!actionResult) {
          requestUpdate();
          return;
        }
        if (static_cast<FileBrowserAction>(actionResult->action) == FileBrowserAction::OpenClipping) {
          jumpToSelectedClipping();
          return;
        }
        if (static_cast<FileBrowserAction>(actionResult->action) == FileBrowserAction::EditTag) {
          if (const Clipping* c = CLIPPINGS.clippingAt(storeIndexFor(selectedIndex))) {
            editTagForClipping(*c);
          } else {
            requestUpdate();
          }
          return;
        }
        if (static_cast<FileBrowserAction>(actionResult->action) == FileBrowserAction::EditNote) {
          if (const Clipping* c = CLIPPINGS.clippingAt(storeIndexFor(selectedIndex))) {
            editNoteForClipping(*c);
          } else {
            requestUpdate();
          }
          return;
        }
        if (static_cast<FileBrowserAction>(actionResult->action) == FileBrowserAction::FilterTag) {
          showTagFilterMenu();
          return;
        }
        if (static_cast<FileBrowserAction>(actionResult->action) != FileBrowserAction::Delete) {
          requestUpdate();
          return;
        }

        for (size_t i = 0; i < CLIPPINGS.clippingCount(); ++i) {
          const Clipping* clipping = CLIPPINGS.clippingAt(i);
          if (!clipping) continue;
          if (clipping->spineIndex == selectedSpineIndex && clipping->startPage == selectedStartPage &&
              clipping->startWordIndex == selectedStartWordIndex && clipping->timestamp == selectedTimestamp) {
            selectedIndex = static_cast<int>(i);
            deleteSelectedClipping();
            return;
          }
        }
        requestUpdate();
      });
}

void EpubReaderClippingListActivity::editTagForClipping(const Clipping& clipping) {
  const Note* existing =
      NOTES.getNoteForClipping(clipping.spineIndex, clipping.startPage, clipping.startWordIndex, clipping.timestamp);
  const char currentTag = existing != nullptr ? existing->tag : 0;

  startActivityForResult(std::make_unique<TagPickerActivity>(renderer, mappedInput, currentTag),
                         [this, clipping](const ActivityResult& result) {
                           if (!result.isCancelled) {
                             const auto& tagResult = std::get<TagResult>(result.data);
                             // The picker can now return a note *and* a tag in
                             // one pass, so apply both rather than either/or.
                             if (!tagResult.noteText.empty()) {
                               NOTES.saveNote(CLIPPINGS.getBookFilePath().c_str(), clipping.spineIndex, clipping.startPage,
                                              clipping.startWordIndex, clipping.timestamp, tagResult.noteText.c_str());
                             }
                             // tag == 0 clears the tag while preserving any existing note text.
                             NOTES.saveTag(CLIPPINGS.getBookFilePath().c_str(), clipping.spineIndex, clipping.startPage,
                                           clipping.startWordIndex, clipping.timestamp, tagResult.tag);
                             detailLayoutWidth = 0;  // tag changed — force detail re-layout
                           }
                           requestUpdate();
                         });
}

void EpubReaderClippingListActivity::editNoteForClipping(const Clipping& clipping) {
  const Note* existing =
      NOTES.getNoteForClipping(clipping.spineIndex, clipping.startPage, clipping.startWordIndex, clipping.timestamp);
  const std::string initialText = existing != nullptr ? existing->text : std::string{};

  if (initialText.length() > EDIT_NOTE_MAX_LENGTH) {
    BookActions::drawToast(renderer, tr(STR_NOTE_TOO_LONG_TO_EDIT));
    delay(1000);
    requestUpdate();
    return;
  }

  startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_EDIT_NOTE), initialText,
                                                                 NoteStore::kNoteTextMax),
                         [this, clipping](const ActivityResult& result) {
                           if (!result.isCancelled) {
                             const auto& kb = std::get<KeyboardResult>(result.data);
                             const Note* existing = NOTES.getNoteForClipping(
                                 clipping.spineIndex, clipping.startPage, clipping.startWordIndex, clipping.timestamp);
                             if (!kb.text.empty()) {
                               NOTES.saveNote(CLIPPINGS.getBookFilePath().c_str(), clipping.spineIndex, clipping.startPage,
                                              clipping.startWordIndex, clipping.timestamp, kb.text.c_str());
                             } else if (existing != nullptr) {
                               if (existing->tag != 0) {
                                 // Cleared the text but a tag exists — keep the tag, drop the text.
                                 NOTES.saveNote(CLIPPINGS.getBookFilePath().c_str(), clipping.spineIndex, clipping.startPage,
                                                clipping.startWordIndex, clipping.timestamp, "");
                               } else {
                                 NOTES.deleteNote(CLIPPINGS.getBookFilePath().c_str(), clipping.spineIndex, clipping.startPage,
                                                  clipping.startWordIndex, clipping.timestamp);
                               }
                             }
                             detailLayoutWidth = 0;  // note changed — force detail re-layout
                           }
                           requestUpdate();
                         });
}

void EpubReaderClippingListActivity::loop() {
  // CrossInk Notes: the tag-filter popup owns input while it is showing.
  if (optionPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return;

  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const Rect header = clippingHeaderRect(safe, metrics, mappedInput);
  if (TouchHeaderBackButton::wasTapped(mappedInput, header)) {
    if (detailMode) {
      closeDetail();
    } else {
      ActivityResult result;
      result.isCancelled = true;
      setResult(std::move(result));
      finish();
    }
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (detailMode) {
      closeDetail();
      return;
    }
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  if (CLIPPINGS.clippingCount() > 0 && !longPressConfirmHandled &&
      mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      mappedInput.getHeldTime() >= CLIPPING_DELETE_HOLD_MS) {
    longPressConfirmHandled = true;
    showClippingActionMenu(true);
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (longPressConfirmHandled) {
      longPressConfirmHandled = false;
      return;
    }
    if (CLIPPINGS.clippingCount() > 0 && selectedIndex >= 0 &&
        selectedIndex < visibleCount()) {
      if (detailMode) {
        // Open the action menu (Open / Edit Note / Delete) — more discoverable
        // than hiding note editing behind a long press only.
        showClippingActionMenu(false);
      } else {
        openSelectedDetail();
      }
    }
    return;
  }

  const int total = visibleCount();
  if (total == 0) return;

  if (detailMode) {
    int touchX = 0;
    int touchY = 0;
    int detailTouchTop = safe.y + DETAIL_START_Y;
    int detailTouchBottom = safe.y + safe.height - DETAIL_BOTTOM_RESERVE;
#if CROSSINK_APP_CAP_TOUCH
    if (mappedInput.hasTouchHardware()) {
      const Rect openButton = touchDetailOpenButtonRect(safe, metrics);
      if (mappedInput.wasTapInRect(openButton.x, openButton.y, openButton.width, openButton.height)) {
        jumpToSelectedClipping();
        return;
      }
      detailTouchTop = header.y + header.height + metrics.verticalSpacing;
      detailTouchBottom = openButton.y;
    }
#endif
    if (!longPressConfirmHandled && mappedInput.isScreenTouchLongPress(touchX, touchY, CLIPPING_DELETE_HOLD_MS) &&
        touchY >= detailTouchTop && touchY < detailTouchBottom) {
      mappedInput.suppressNextTouchTap();
      longPressConfirmHandled = true;
      showClippingActionMenu(false);
      return;
    }
    rebuildDetailLayoutIfNeeded();
    const int detailPageCount = getDetailPageCount();
    const auto swipe = mappedInput.wasSwipe();
    if (swipe == MappedInputManager::SwipeDir::Up && detailPage < detailPageCount - 1) {
      detailPage++;
      requestUpdate();
      return;
    }
    if (swipe == MappedInputManager::SwipeDir::Down && detailPage > 0) {
      detailPage--;
      requestUpdate();
      return;
    }
    buttonNavigator.onNextRelease([this, detailPageCount] {
      if (detailPage < detailPageCount - 1) {
        detailPage++;
        requestUpdate();
      }
    });
    buttonNavigator.onPreviousRelease([this] {
      if (detailPage > 0) {
        detailPage--;
        requestUpdate();
      }
    });
    buttonNavigator.onNextContinuous([this, detailPageCount] {
      if (detailPage < detailPageCount - 1) {
        detailPage++;
        requestUpdate();
      }
    });
    buttonNavigator.onPreviousContinuous([this] {
      if (detailPage > 0) {
        detailPage--;
        requestUpdate();
      }
    });
    return;
  }

  int tx = 0;
  int ty = 0;
  if (!longPressConfirmHandled && mappedInput.isScreenTouchLongPress(tx, ty, CLIPPING_DELETE_HOLD_MS) &&
      listRowStep > 0 && ty >= listTop && ty < listBottom) {
    const int offset = ty - listTop;
    const int row = offset / listRowStep;
    const int touchedIndex = topIndex + row;
    if (row < visibleRows && offset % listRowStep < listRowHeight && touchedIndex < total) {
      selectedIndex = touchedIndex;
      mappedInput.suppressNextTouchTap();
      longPressConfirmHandled = true;
      showClippingActionMenu(false);
    }
    return;
  }
  if (uiReady) {
    const fui::InputSnapshot snap = touchSnapshotFrom(mappedInput);
    if (snap.touchPressed || snap.touchReleased) {
      const auto event = app.route(snap);
      if (app.invalidated()) requestUpdate();
      if (event) return;
    }
  }

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
    const int next = scrollListBy(topIndex, swipe == MappedInputManager::SwipeDir::Up ? visibleRows : -visibleRows,
                                  visibleRows, total);
    if (next != topIndex) {
      topIndex = next;
      requestUpdate();
    }
    return;
  }

  const auto moveSelection = [this, total](const int next) {
    selectedIndex = next;
    topIndex = followListSelection(selectedIndex, topIndex, visibleRows, total);
    requestUpdate();
  };
  buttonNavigator.onNextRelease(
      [this, total, &moveSelection] { moveSelection(ButtonNavigator::nextIndex(selectedIndex, total)); });
  buttonNavigator.onPreviousRelease(
      [this, total, &moveSelection] { moveSelection(ButtonNavigator::previousIndex(selectedIndex, total)); });
  buttonNavigator.onNextContinuous([this, total, &moveSelection] {
    moveSelection(ButtonNavigator::nextPageIndex(selectedIndex, total, visibleRows));
  });
  buttonNavigator.onPreviousContinuous([this, total, &moveSelection] {
    moveSelection(ButtonNavigator::previousPageIndex(selectedIndex, total, visibleRows));
  });
}

void EpubReaderClippingListActivity::onRowEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<EpubReaderClippingListActivity*>(user);
  if (event.value < 0 || event.value >= static_cast<int16_t>(CLIPPINGS.clippingCount())) return;
  self->selectedIndex = event.value;
  self->app.clearTapFlash();
  self->openSelectedDetail();
}

void EpubReaderClippingListActivity::listScreen(UiApp::ScreenType& screen, void* user) {
  static_cast<EpubReaderClippingListActivity*>(user)->buildListScreen(screen);
}

void EpubReaderClippingListActivity::buildListScreen(UiApp::ScreenType& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMargin(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + TouchHeaderBackButton::height(metrics, mappedInput)),
      static_cast<int16_t>(renderer.getScreenWidth() - safe.x - safe.width),
      static_cast<int16_t>(renderer.getScreenHeight() - safe.y - safe.height), static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  const size_t count = static_cast<size_t>(visibleCount());
  if (count == 0) {
    screen.centeredText(tr(STR_NO_CLIPPINGS), screen.theme().bodyText);
    return;
  }
  fui::ListProps props;
  props.items = uiItems.data();
  props.count = static_cast<uint16_t>(uiItems.size());
  props.selectedIndex = static_cast<int16_t>(selectedIndex);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  const fui::Rect bounds = screen.body();
  listTop = bounds.y;
  listBottom = bounds.bottom();
  listLeft = bounds.x;
  listWidth = bounds.width;
  // CrossInk Notes: the subtitle carries tag + chapter + note, so render it in
  // a genuinely smaller face to fit more in. FreeInkUI addresses fonts by slot
  // (FONT_SMALL/BODY/TITLE), and uiScaleSpec() binds SMALL to the same font as
  // BODY — so rebind this activity's SMALL slot and keep the row label on BODY.
  // uiTarget belongs to this activity, so no other screen is affected.
  uiTarget.setFont(fui::GfxRendererTarget::FONT_SMALL, SUBTITLE_FONT_ID);
  props.labelText.font = fui::GfxRendererTarget::FONT_BODY;
  props.subtitleText.font = fui::GfxRendererTarget::FONT_SMALL;
  props.subtitleText.bold = false;
  // CrossInk Notes: the list draws only a label and a subtitle, but a row shows
  // three lines here (clipping / chapter / tag+note). Grow the row so there is
  // room for the third line, which render() draws itself. The widget centres
  // the label+subtitle block, so the extra height is split above and below —
  // reserve two line heights to leave a full line clear at the bottom.
  // The row holds the clipping and the chapter; the note goes in the gap below
  // it. Reserving the note's space inside the row instead would force an equal
  // amount of dead space above it, because the widget centres the
  // label+subtitle block vertically — so the row hugs its two lines and the
  // selection highlight stays centred on them.
  noteLineHeight = renderer.getLineHeight(SUBTITLE_FONT_ID);
  const int labelLineHeight = renderer.getLineHeight(uiScaleSpec().bodyFontId);
  constexpr int kRowPadding = 6;
  int rowHeight = labelLineHeight + noteLineHeight + kRowPadding;
  if (mappedInput.hasTouchHardware()) {
    rowHeight = std::max(rowHeight, static_cast<int>(uiListRowHeight(screen.theme(), UiListRowType::WithSubtitle)));
  }
  props.rowHeight = static_cast<int16_t>(rowHeight);
  // Widen the row gap to hold the note line plus a little air before the next
  // row. listVisibleRows() accounts for the gap, so paging stays correct.
  props.rowGap = static_cast<int16_t>(noteLineHeight + kRowPadding);
  // Draw every row unselected: no background on any state. An entry spans three
  // lines but the widget only knows about two, so it cannot highlight the whole
  // thing — render() inverts the complete entry in one pass instead. Painting
  // the row and the note line separately left a seam at their edges, let the
  // two halves drift apart while scrolling, and skipped the note's line
  // entirely when a clipping had no note.
  fui::StyleSet rowStyles;
  rowStyles.normal.background = fui::Paint::none();
  rowStyles.normal.foreground = fui::Paint::solid(fui::Color::Black);
  rowStyles.selected = rowStyles.normal;
  rowStyles.focused = rowStyles.normal;
  rowStyles.active = rowStyles.normal;
  rowStyles.disabled = rowStyles.normal;
  rowStyles.explicitlySet = true;
  props.rowStyles = rowStyles;
  const auto rows = configureUiList(props, screen.theme(), bounds, UiListRowType::WithSubtitle);
  listRowHeight = props.rowHeight;
  listRowStep = props.rowHeight + props.rowGap;
  visibleRows = rows > 0 ? rows : 1;
  // CrossInk Notes: resolve where a row's text actually starts. FreeInkApp::list
  // overwrites the ListProps defaults with theme values before drawing (side
  // padding, row inset, scroll track), so those theme values — not the struct
  // defaults — decide the row geometry. Mirror the same math here so the third
  // line lines up on every device and theme.
  {
    const auto& th = screen.theme();
    const int rowInset = th.listInset < 0 ? 0 : th.listInset;
    const int sidePad = th.listSidePadding < 0 ? 8 : th.listSidePadding;
    const int scrollW = th.listScrollWidth < 0 ? 3 : th.listScrollWidth;
    const int scrollIns = th.listScrollInset < 0 ? 0 : th.listScrollInset;
    const bool scrollLeft = th.listScrollSide == 1;
    int areaX = static_cast<int>(bounds.x) + rowInset;
    int areaW = static_cast<int>(bounds.width) - rowInset * 2;
    if (static_cast<int>(count) > visibleRows && scrollW > 0) {
      const int needed = scrollW + scrollIns + 2;
      if (rowInset < needed) {
        const int cut = needed - rowInset;
        areaW -= cut;
        if (scrollLeft) areaX += cut;
      }
    }
    noteRowLeft = areaX;
    noteRowWidth = areaW;
    noteTextLeft = areaX + sidePad;
    noteMaxWidth = std::max(0, areaW - sidePad * 2);
  }

  topIndex = scrollListBy(topIndex, 0, visibleRows, static_cast<int>(count));
  props.topIndex = static_cast<uint16_t>(topIndex);
  const int end = std::min(static_cast<int>(count), topIndex + visibleRows);
  for (int i = topIndex; i < end; ++i) {
    const size_t slot = static_cast<size_t>(i - topIndex);
    uiRawText[slot].clear();
    CLIPPINGS.readClippingText(storeIndexFor(i), uiRawText[slot]);
    buildOneLineSnippetText(uiRawText[slot], uiLabels[slot]);
    const Clipping* clipping = CLIPPINGS.clippingAt(storeIndexFor(i));
    fui::ListItem& item = uiItems[static_cast<size_t>(i)];
    item = fui::ListItem{};
    item.label = uiLabels[slot].c_str();
    // CrossInk Notes: the second line shows this highlight's tag and note, so
    // they are visible without opening the clipping, followed by the chapter
    // (upstream shows the chapter alone here). Both compete for one line, so
    // the chapter is reserved a slice of the width and the note is truncated
    // into whatever is left — that way neither can push the other off screen.
    std::string& subtitle = uiSubtitles[slot];
    subtitle.clear();
    if (clipping) {
      const Note* note = NOTES.getNoteForClipping(clipping->spineIndex, clipping->startPage, clipping->startWordIndex,
                                                  clipping->timestamp);
      // Line 2 is the chapter (upstream's own subtitle); line 3 is this
      // highlight's tag and note, drawn by render() in the reserved space.
      subtitle = clipping->chapterTitle[0] != '\0' ? clipping->chapterTitle : tr(STR_UNKNOWN_CHAPTER);
      item.subtitle = subtitle.c_str();

      std::string& noteLine = uiNotes[slot];
      noteLine.clear();
      if (note) {
        if (note->tag != 0) {
          noteLine += '[';
          noteLine += note->tag;
          noteLine += "] ";
        }
        if (!note->text.empty()) {
          std::string noteFlat;
          buildOneLineSnippetText(note->text, noteFlat);
          noteLine += noteFlat;
        }
      }
    }
    item.actionValue = static_cast<int16_t>(i);
  }
  screen.list(props);
}

void EpubReaderClippingListActivity::renderDetail() {
  rebuildDetailLayoutIfNeeded();

  // Front-button hints move to a different physical edge for each reader
  // orientation. Keep all detail content inside the same safe area used by
  // the list view so it cannot render beneath that hint band.
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const int contentX = safe.x;
  const int contentWidth = safe.width;
  const int contentY = safe.y;
  const auto& metrics = UITheme::getInstance().getMetrics();

  const char* chapter = tr(STR_CLIPPINGS);
  const Clipping* selectedClipping =
      selectedIndex >= 0 ? CLIPPINGS.clippingAt(storeIndexFor(selectedIndex)) : nullptr;
  if (selectedClipping && selectedClipping->chapterTitle[0] != '\0') {
    chapter = selectedClipping->chapterTitle;
  }

  int textStartY = DETAIL_START_Y + contentY;
#if CROSSINK_APP_CAP_TOUCH
  const bool showTouchControls = mappedInput.hasTouchHardware();
  Rect openButton{};
  if (showTouchControls) {
    const Rect header = clippingHeaderRect(safe, metrics, mappedInput);
    TouchHeaderBackButton::draw(renderer, uiTarget, header, chapter, true);
    textStartY = header.y + header.height + metrics.verticalSpacing;
    openButton = touchDetailOpenButtonRect(safe, metrics);
  } else
#endif
  {
    const std::string title =
        renderer.truncatedText(UI_12_FONT_ID, chapter, contentWidth - DETAIL_SIDE_MARGIN * 2, EpdFontFamily::BOLD);
    const int titleX =
        contentX + (contentWidth - renderer.getTextWidth(UI_12_FONT_ID, title.c_str(), EpdFontFamily::BOLD)) / 2;
    renderer.drawText(UI_12_FONT_ID, titleX, 15 + contentY, title.c_str(), true, EpdFontFamily::BOLD);
  }

  const int lineStep = renderer.getLineHeight(UI_10_FONT_ID) + DETAIL_LINE_GAP;
  const int textX = contentX + DETAIL_SIDE_MARGIN;
  const int firstLine = detailPage * detailLinesPerPage;
  const int lastLine = std::min(static_cast<int>(detailLines.size()), firstLine + detailLinesPerPage);
  int y = textStartY;
  for (int i = firstLine; i < lastLine; i++) {
    renderer.drawText(UI_10_FONT_ID, textX, y, detailLines[i].c_str());
    y += lineStep;
  }

  const int detailPageCount = getDetailPageCount();
  if (detailPageCount > 1) {
    char pageBuf[16];
    snprintf(pageBuf, sizeof(pageBuf), "%d/%d", detailPage + 1, detailPageCount);
    const int pageLabelWidth = renderer.getTextWidth(SMALL_FONT_ID, pageBuf);
    int pageLabelY = safe.y + safe.height - 35;
#if CROSSINK_APP_CAP_TOUCH
    if (showTouchControls) {
      pageLabelY = openButton.y - metrics.verticalSpacing - renderer.getLineHeight(SMALL_FONT_ID);
    }
#endif
    renderer.drawText(SMALL_FONT_ID, contentX + contentWidth - DETAIL_SIDE_MARGIN - pageLabelWidth, pageLabelY,
                      pageBuf);
  }

#if CROSSINK_APP_CAP_TOUCH
  if (showTouchControls) {
    renderer.fillRectDither(openButton.x, openButton.y, openButton.width, openButton.height, Color::White);
    renderer.drawRect(openButton.x, openButton.y, openButton.width, openButton.height, true);
    const char* label = tr(STR_OPEN);
    const int labelX =
        openButton.x + (openButton.width - renderer.getTextWidth(UI_10_FONT_ID, label, EpdFontFamily::BOLD)) / 2;
    const int labelY = openButton.y + (openButton.height - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
    renderer.drawText(UI_10_FONT_ID, labelX, labelY, label, true, EpdFontFamily::BOLD);
    return;
  }
#endif

  // CrossInk Notes: detail-view Confirm opens the action menu (Open / Edit Tag /
  // Edit Note / Delete), so the hint reads MENU rather than upstream's OPEN.
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_MENU), detailPage > 0 ? tr(STR_DIR_UP) : "",
                                            detailPage < detailPageCount - 1 ? tr(STR_DIR_DOWN) : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);
}

void EpubReaderClippingListActivity::render(RenderLock&&) {
  // CrossInk Notes: the tag-filter popup draws over the list.
  if (optionPopup.processRender(renderer, mappedInput)) return;

  renderer.clearScreen();

  if (detailMode) {
    renderDetail();
    renderer.displayBuffer();
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const Rect header = clippingHeaderRect(safe, metrics, mappedInput);
  if (mappedInput.hasTouchHardware()) {
    TouchHeaderBackButton::draw(renderer, uiTarget, header, tr(STR_NOTES), true);
  } else {
    GUI.drawHeader(renderer, header, tr(STR_NOTES), nullptr, true);
  }
  uiReady = false;
  app.render();
  uiReady = true;

  // CrossInk Notes: an entry is three lines — the clipping and chapter drawn by
  // the list, plus this note line drawn underneath in the row gap. Everything
  // is drawn unselected first; the selection is applied afterwards by inverting
  // the whole entry in one pass (see below).
  const int clippingCount = visibleCount();
  const int lastVisible = std::min(clippingCount, topIndex + visibleRows);
  if (listRowStep > 0 && noteLineHeight > 0) {
    for (int i = topIndex; i < lastVisible; ++i) {
      const size_t slot = static_cast<size_t>(i - topIndex);
      if (slot >= uiNotes.size() || uiNotes[slot].empty()) continue;
      const int rowY = listTop + static_cast<int>(slot) * listRowStep;
      // Just under the row, in the gap. This also groups each entry visually:
      // the chapter sits tight under its clipping, the note a little further
      // down, rather than all three being evenly spaced.
      const int noteY = rowY + listRowHeight + 2;
      if (noteY + noteLineHeight > listBottom) break;
      const std::string line = renderer.truncatedText(SUBTITLE_FONT_ID, uiNotes[slot].c_str(), noteMaxWidth);
      renderer.drawText(SUBTITLE_FONT_ID, noteTextLeft, noteY, line.c_str(), true);
    }
  }

  // CrossInk Notes: the selection. The list widget paints no row background
  // (see the row styles in buildListScreen), so the whole entry — clipping,
  // chapter and note line — is inverted here as a single rectangle. Doing it in
  // one pass is what keeps the highlight seamless, keeps it from splitting
  // apart while scrolling, and still covers the note's line when a clipping has
  // no note.
  if (listRowStep > 0 && selectedIndex >= topIndex && selectedIndex < lastVisible) {
    const int rowY = listTop + (selectedIndex - topIndex) * listRowStep;
    // Cover the row and its note line, leaving the sliver before the next entry
    // clear so consecutive selections never look merged.
    const int height = std::min(listRowStep - 2, listBottom - rowY);
    if (height > 0) {
      renderer.invertRect(noteRowLeft, rowY, noteRowWidth, height);
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), visibleCount() == 0 ? "" : tr(STR_OPEN),
                                            tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);
  renderer.displayBuffer();
}
