#include "DictionaryWordSelectActivity.h"

#include <BidiUtils.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <MemoryBudget.h>
#include <SdCardFont.h>
#include <Utf8.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <cstring>

#include "../settings/DictionarySelectActivity.h"
#include "CrossPointSettings.h"
#include "DictionaryDefinitionActivity.h"
#include "MappedInputManager.h"
#include "Memory.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/Dictionary.h"
#include "util/DictionaryActivityUtils.h"

namespace {

// Soft-hyphen U+00AD encoded as 2 UTF-8 bytes. Layout (ParsedText.cpp:19)
// strips these before measurement, so we mirror that here — otherwise
// derived word widths include the soft-hyphen glyph's advance and the
// highlight rectangle overruns into the inter-word gap.
constexpr char SOFT_HYPHEN_UTF8[] = "\xC2\xAD";
constexpr size_t SOFT_HYPHEN_BYTES = 2;

int16_t measureWordAdvanceX(const GfxRenderer& renderer, int fontId, const std::string& word,
                            EpdFontFamily::Style style) {
  if (word.find(SOFT_HYPHEN_UTF8) == std::string::npos) {
    return static_cast<int16_t>(renderer.getTextAdvanceX(fontId, word.c_str(), style));
  }
  std::string sanitized = word;
  size_t pos = 0;
  while ((pos = sanitized.find(SOFT_HYPHEN_UTF8, pos)) != std::string::npos) {
    sanitized.erase(pos, SOFT_HYPHEN_BYTES);
  }
  return static_cast<int16_t>(renderer.getTextAdvanceX(fontId, sanitized.c_str(), style));
}

int16_t measureWordAdvanceX(const GfxRenderer& renderer, int fontId, const std::string& word,
                            EpdFontFamily::Style style, uint8_t bionicBoundary, uint16_t bionicSuffixX) {
  if (bionicBoundary == 0 || bionicSuffixX == 0) {
    return measureWordAdvanceX(renderer, fontId, word, style);
  }
  const size_t suffixStart = std::min<size_t>(bionicBoundary, word.size());
  return static_cast<int16_t>(bionicSuffixX + renderer.getTextAdvanceX(fontId, word.c_str() + suffixStart, style));
}

int16_t measureWordAdvanceX(const GfxRenderer& renderer, int fontId, const std::string& word,
                            EpdFontFamily::Style style, uint8_t bionicBoundary, uint16_t bionicRunOffset,
                            bool wordIsRtl) {
  if (!wordIsRtl || bionicBoundary == 0 || bionicRunOffset == 0) {
    return measureWordAdvanceX(renderer, fontId, word, style, bionicBoundary, bionicRunOffset);
  }

  const auto boldStyle = static_cast<EpdFontFamily::Style>(style | EpdFontFamily::BOLD);
  char boldBuf[40];
  const size_t boldLen = std::min<size_t>({static_cast<size_t>(bionicBoundary), word.size(), sizeof(boldBuf) - 1});
  memcpy(boldBuf, word.c_str(), boldLen);
  boldBuf[boldLen] = '\0';
  return static_cast<int16_t>(bionicRunOffset + renderer.getTextAdvanceX(fontId, boldBuf, boldStyle));
}

bool isRtlWord(const char* word, const bool fallbackRtl) {
  return BidiUtils::detectParagraphLevel(word, fallbackRtl ? 1 : 0) == 1;
}

// Single-style prewarm/advance-table bitmask: bit 0 = REGULAR, 1 = BOLD,
// 2 = ITALIC, 3 = BOLD_ITALIC. The `& 0x03` is defensive — Style enum
// is two bits, but UNDERLINE etc. live in higher bits if ever OR'd in.
constexpr uint8_t styleToBitMask(EpdFontFamily::Style style) {
  return static_cast<uint8_t>(1u << (static_cast<uint8_t>(style) & 0x03));
}

}  // namespace

void DictionaryWordSelectActivity::onEnter() {
  Activity::onEnter();
  Dictionary::clearLookupDictPathOverride();
  mappedInput.setReaderTouchscreenOverride(true);
  ignoreInitialBackRelease_ = mappedInput.isPressed(MappedInputManager::Button::Back);
  const bool consumeInitialConfirm = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  if (!buildWorkingSet(consumeInitialConfirm)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }
  autoLookupInitialWord_ = false;
  requestUpdate();
}

bool DictionaryWordSelectActivity::buildWorkingSet(const bool consumeInitialConfirm) {
  if (!page) {
    LOG_ERR("DICT", "Cannot build word selection without a reader page");
    return false;
  }
  std::vector<WordSelectNavigator::WordInfo> words;
  std::vector<WordSelectNavigator::Row> rows;
  std::string textPool;
  textPool.reserve(512);
  extractWords(words, rows, textPool);
  mergeHyphenatedWords(words, rows, textPool);
  navigator.load(std::move(words), std::move(rows), std::move(textPool), consumeInitialConfirm);
#if CROSSINK_APP_CAP_TOUCH
  navigator.setTouchDragCursorVisible(mappedInput.hasTouch());
  bool initialTouchHit = false;
  if (initialTouchX_ >= 0 && initialTouchY_ >= 0) {
    navigator.selectWordAtPoint(initialTouchX_, initialTouchY_, renderer.getLineHeight(SETTINGS.getReaderFontId()),
                                &initialTouchHit);
  }
  if (autoLookupInitialWord_) {
    const auto* selected = initialTouchHit ? navigator.getSelected() : nullptr;
    if (!selected) {
      ActivityResult result;
      result.isCancelled = true;
      setResult(std::move(result));
      finish();
      return false;
    }
    touchDragLookup_ = navigator.beginTouchMultiSelect();
  }
#else
  navigator.setTouchDragCursorVisible(false);
#endif
  return true;
}

void DictionaryWordSelectActivity::suspendWorkingSet() {
  if (workingSetSuspended_ || !readerPageReload_) return;
  if (const auto* selected = navigator.getSelected()) {
    suspendedSelectionX_ = selected->screenX + selected->width / 2;
    suspendedSelectionY_ = selected->screenY + renderer.getLineHeight(SETTINGS.getReaderFontId()) / 2;
  }
  navigator.releaseWorkingSet();
  page.reset();
  workingSetSuspended_ = true;
  MemoryBudget::logHeapShape("dict.parent_suspended");
}

bool DictionaryWordSelectActivity::restoreWorkingSet() {
  if (!workingSetSuspended_) return true;
  page = readerPageReload_(readerContext_);
  if (!page) {
    LOG_ERR("DICT", "Failed to reload reader page after dictionary definition");
    return false;
  }
  if (!buildWorkingSet(/*consumeInitialConfirm=*/false)) return false;
  if (suspendedSelectionX_ >= 0 && suspendedSelectionY_ >= 0) {
    navigator.selectWordAtPoint(suspendedSelectionX_, suspendedSelectionY_,
                                renderer.getLineHeight(SETTINGS.getReaderFontId()));
  }
  workingSetSuspended_ = false;
  MemoryBudget::logHeapShape("dict.parent_restored");
  return true;
}

void DictionaryWordSelectActivity::onExit() {
  controller.onExit();
  Dictionary::clearLookupDictPathOverride();
  mappedInput.setReaderTouchscreenOverride(false);
  const auto& sdFonts = renderer.getSdCardFonts();
  auto it = sdFonts.find(SETTINGS.getReaderFontId());
  if (it != sdFonts.end()) it->second->clearPersistentCache();
  Activity::onExit();
}

void DictionaryWordSelectActivity::prewarmHighlightGlyphs(int currIdx) {
  const auto* w = navigator.getWordAt(currIdx);
  if (!w) return;
  auto* fcm = renderer.getFontCacheManager();
  if (!fcm) return;
  uint8_t styleMask = styleToBitMask(w->style);
  if (w->bionicBoundary > 0) {
    styleMask |= styleToBitMask(static_cast<EpdFontFamily::Style>(w->style | EpdFontFamily::BOLD));
  }
  fcm->prewarmCache(SETTINGS.getReaderFontId(), navigator.getDisplay(*w), styleMask);
}

void DictionaryWordSelectActivity::prebuildAdvanceTable() {
  // Concatenate every word on the page and OR the style flags. ~2KB transient
  // string; freed on return. Matches FontCacheManager::PrewarmScope's
  // scanText_ allocation pattern.
  std::string pageText;
  pageText.reserve(2048);
  uint8_t pageStyleMask = 0;
  for (const auto& element : page->elements) {
    if (element->getTag() != TAG_PageLine) continue;
    const auto* line = static_cast<const PageLine*>(element.get());
    const auto& block = line->getBlock();
    if (!block) continue;
    for (uint16_t i = 0; i < block->wordCount(); i++) {
      pageText.append(block->wordText(i));
      pageText.push_back(' ');
      pageStyleMask |= styleToBitMask(block->wordStyle(i));
    }
  }
  if (pageStyleMask == 0) pageStyleMask = styleToBitMask(EpdFontFamily::REGULAR);
  // The advance table persists across clearCache() (SdCardFont.h:201) so
  // this only pays the SD cost on the first entry; subsequent ones
  // amortize.
  renderer.ensureSdCardFontReady(SETTINGS.getReaderFontId(), pageText.c_str(), pageStyleMask);
}

void DictionaryWordSelectActivity::clearFrontButtonHintArea() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int hintSize = metrics.buttonHintsHeight;
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();

  switch (renderer.getOrientation()) {
    case GfxRenderer::Orientation::Portrait:
      renderer.fillRect(0, screenHeight - hintSize, screenWidth, hintSize, false);
      break;
    case GfxRenderer::Orientation::LandscapeClockwise:
      renderer.fillRect(0, 0, hintSize, screenHeight, false);
      break;
    case GfxRenderer::Orientation::PortraitInverted:
      renderer.fillRect(0, 0, screenWidth, hintSize, false);
      break;
    case GfxRenderer::Orientation::LandscapeCounterClockwise:
      renderer.fillRect(screenWidth - hintSize, 0, hintSize, screenHeight, false);
      break;
  }
}

void DictionaryWordSelectActivity::renderDefinitionBackground() {
  if (!page) {
    LOG_ERR("DICT", "Cannot redraw dictionary background without a reader page");
    return;
  }
  renderer.clearScreen();

  // Dictionary layout can evict the reader font's bitmap glyph cache. Rebuild
  // it before redrawing the page behind the modal; the persistent advance
  // table only preserves glyph widths, not the bitmaps themselves.
  auto* fcm = renderer.getFontCacheManager();
  auto scope = fcm->createPrewarmScope();
  page->render(renderer, SETTINGS.getReaderFontId(), marginLeft, marginTop);  // scan pass
  scope.endScanAndPrewarm();
  page->render(renderer, SETTINGS.getReaderFontId(), marginLeft, marginTop);
}

void DictionaryWordSelectActivity::renderDefinitionBackgroundCallback(void* context) {
  static_cast<DictionaryWordSelectActivity*>(context)->renderDefinitionBackground();
}

void DictionaryWordSelectActivity::extractWords(std::vector<WordSelectNavigator::WordInfo>& words,
                                                std::vector<WordSelectNavigator::Row>& rows, std::string& textPool) {
  words.clear();
  words.reserve(64);
  rows.clear();
  rows.reserve(16);

  // Populate the SD font's advance table once so every getTextAdvanceX call
  // below takes the fast in-RAM path.
  prebuildAdvanceTable();

  // Fallback used by blocks where we can't derive a per-line gap
  // (single-word blocks, degenerate first-word measurements).
  const int16_t naturalSpaceWidth =
      static_cast<int16_t>(renderer.getTextAdvanceX(SETTINGS.getReaderFontId(), " ", EpdFontFamily::REGULAR));

  for (const auto& element : page->elements) {
    if (element->getTag() != TAG_PageLine) continue;
    const auto* line = static_cast<const PageLine*>(element.get());
    const auto& block = line->getBlock();
    if (!block) continue;

    const uint16_t wordCount = block->wordCount();
    const int rubyShift = block->getRubyShift(renderer.getFontAscenderSize(SETTINGS.getReaderFontId()));

    // Per-line gap = xPos[1] - xPos[0] - firstWordWidth. Justified blocks
    // stretch the gap (ParsedText.cpp:514-553 adds justifyExtra), so a
    // global space-width can't be reused — we measure per-block.
    int16_t lineGapWidth = naturalSpaceWidth;
    if (wordCount >= 2 && block->wordTextLen(0) > 0) {
      const EpdFontFamily::Style firstStyle = block->wordStyle(0);
      const uint8_t firstBionicBoundary = block->bionicBoundary(0);
      const uint16_t firstBionicSuffixX = block->bionicRunOffset(0);
      const std::string firstWordText(block->wordText(0), block->wordTextLen(0));
      const bool firstWordIsRtl = isRtlWord(firstWordText.c_str(), block->getBlockStyle().isRtl);
      const int16_t firstWidth = measureWordAdvanceX(renderer, SETTINGS.getReaderFontId(), firstWordText, firstStyle,
                                                     firstBionicBoundary, firstBionicSuffixX, firstWordIsRtl);
      const int16_t derivedGap = static_cast<int16_t>(block->wordXpos(1) - block->wordXpos(0) - firstWidth);
      // When wordList[1] is a continuation (attached punctuation etc., ParsedText.cpp:537-544)
      // the layout inserts no inter-word gap, so derivedGap collapses to the kerning offset
      // (~1-3 px). Real gaps are always >= getSpaceAdvance(...), so a half-space threshold
      // cleanly separates a real gap from a continuation kerning without needing Block to
      // expose continuesVec. Without the threshold, an undersized lineGapWidth propagates as
      // a per-word width overestimate (~4-6 px) — the highlight rectangle bleeds past the
      // word into the inter-word space.
      if (derivedGap > naturalSpaceWidth / 2) lineGapWidth = derivedGap;
    }

    int lastSelectableWordIndex = -2;
    for (uint16_t wordIndex = 0; wordIndex < wordCount; ++wordIndex) {
      int16_t screenX = line->xPos + block->wordXpos(wordIndex) + marginLeft;
      int16_t screenY = line->yPos + marginTop + rubyShift;
      const std::string wordText(block->wordText(wordIndex), block->wordTextLen(wordIndex));
      const EpdFontFamily::Style wordStyle = block->wordStyle(wordIndex);
      const uint8_t bionicBoundary = block->bionicBoundary(wordIndex);
      const uint16_t bionicSuffixX = block->bionicRunOffset(wordIndex);
      const bool wordIsRtl = isRtlWord(wordText.c_str(), block->getBlockStyle().isRtl);

      // Use the same Unicode-aware predicate as lookup cleanup so accented
      // Latin, CJK, Cyrillic, Greek, Arabic, Hebrew, and other rendered scripts
      // remain selectable while standalone punctuation/symbols do not.
      if (!utf8ContainsLookupCharacter(wordText)) {
        lastSelectableWordIndex = -2;
        continue;
      }

      // Split on en-dash (U+2013: E2 80 93) and em-dash (U+2014: E2 80 94)
      std::vector<size_t> splitStarts;
      splitStarts.reserve(4);
      size_t partStart = 0;
      for (size_t i = 0; i < wordText.size();) {
        if (i + 2 < wordText.size() && static_cast<uint8_t>(wordText[i]) == 0xE2 &&
            static_cast<uint8_t>(wordText[i + 1]) == 0x80 &&
            (static_cast<uint8_t>(wordText[i + 2]) == 0x93 || static_cast<uint8_t>(wordText[i + 2]) == 0x94)) {
          if (i > partStart) splitStarts.push_back(partStart);
          i += 3;
          partStart = i;
        } else {
          i++;
        }
      }
      if (partStart < wordText.size()) splitStarts.push_back(partStart);

      if (splitStarts.size() <= 1 && partStart == 0) {
        // Non-bionic width = (xPos[i+1] - xPos[i]) - lineGapWidth, which is the
        // layout's xpos diff with the trailing inter-word gap removed.
        // Bionic words use direct split-run measurement because bold/non-bold
        // run widths can diverge from the gap heuristic.
        // Punctuation tokens skipped above kept their xpos entries as boundary
        // markers, so this works regardless of what the next token is.
        // Last word per block has no next xpos; fall back to direct
        // measurement. Clamp to 1 to guard pathological cases (continuation
        // negative kerning, short words where the entire xpos diff is the
        // gap).
        int16_t wordWidth;
        if (bionicBoundary > 0 && bionicSuffixX > 0) {
          wordWidth = measureWordAdvanceX(renderer, SETTINGS.getReaderFontId(), wordText, wordStyle, bionicBoundary,
                                          bionicSuffixX, wordIsRtl);
        } else if (wordIndex + 1 < wordCount) {
          const int16_t raw = static_cast<int16_t>(block->wordXpos(wordIndex + 1) - block->wordXpos(wordIndex));
          wordWidth = std::max(static_cast<int16_t>(1), static_cast<int16_t>(raw - lineGapWidth));
        } else {
          wordWidth = measureWordAdvanceX(renderer, SETTINGS.getReaderFontId(), wordText, wordStyle, bionicBoundary,
                                          bionicSuffixX);
        }

        bool joinWithoutSpaceBefore = false;
        if (lastSelectableWordIndex == static_cast<int>(wordIndex) - 1 && !words.empty()) {
          const uint16_t previousIndex = wordIndex - 1;
          const auto previousStyle = block->wordStyle(previousIndex);
          // Adjacent lookup tokens are overwhelmingly per-character CJK runs,
          // which bypass bionic splitting. Measure the prior null-terminated
          // block token directly to avoid allocating another std::string in
          // this per-word loop.
          const int16_t previousMeasuredWidth = static_cast<int16_t>(
              renderer.getTextAdvanceX(SETTINGS.getReaderFontId(), block->wordText(previousIndex), previousStyle));
          const int16_t currentMeasuredWidth = measureWordAdvanceX(renderer, SETTINGS.getReaderFontId(), wordText,
                                                                   wordStyle, bionicBoundary, bionicSuffixX, wordIsRtl);
          const int currentLeft = screenX;
          const int currentRight = screenX + currentMeasuredWidth;
          const int previousLeft = words.back().screenX;
          const int previousRight = previousLeft + previousMeasuredWidth;
          const int gap = currentLeft >= previousLeft ? currentLeft - previousRight : previousLeft - currentRight;
          joinWithoutSpaceBefore = gap < naturalSpaceWidth / 2;
          if (joinWithoutSpaceBefore) {
            // The generic width heuristic subtracts an inter-word gap. Adjacent
            // script tokens have no such gap, so retain their measured widths
            // for accurate highlights and future adjacency checks.
            words.back().width = previousMeasuredWidth;
            wordWidth = currentMeasuredWidth;
          }
        }
        {
          uint16_t off = WordSelectNavigator::poolAppend(textPool, wordText.c_str(), wordText.size());
          WordSelectNavigator::WordInfo wi;
          wi.textOffset = off;
          wi.textLen = static_cast<uint16_t>(wordText.size());
          wi.lookupOffset = off;
          wi.lookupLen = wi.textLen;
          wi.screenX = screenX;
          wi.screenY = screenY;
          wi.width = wordWidth;
          wi.style = wordStyle;
          wi.fontId = SETTINGS.getReaderFontId();
          wi.isRtl = wordIsRtl;
          wi.joinWithoutSpaceBefore = joinWithoutSpaceBefore;
          wi.bionicBoundary = bionicBoundary;
          wi.bionicSuffixX = bionicSuffixX;
          words.push_back(wi);
          lastSelectableWordIndex = wordIndex;
        }
      } else {
        for (size_t si = 0; si < splitStarts.size(); si++) {
          size_t start = splitStarts[si];
          size_t end = (si + 1 < splitStarts.size()) ? splitStarts[si + 1] : wordText.size();
          size_t textEnd = end;
          while (textEnd > start && textEnd <= wordText.size()) {
            if (textEnd >= 3 && static_cast<uint8_t>(wordText[textEnd - 3]) == 0xE2 &&
                static_cast<uint8_t>(wordText[textEnd - 2]) == 0x80 &&
                (static_cast<uint8_t>(wordText[textEnd - 1]) == 0x93 ||
                 static_cast<uint8_t>(wordText[textEnd - 1]) == 0x94)) {
              textEnd -= 3;
            } else {
              break;
            }
          }
          std::string part = wordText.substr(start, textEnd - start);
          if (part.empty()) continue;

          std::string prefix = wordText.substr(0, start);
          // Dash-split words are rare (~0-2 per page); per-part measurement
          // is fine here. Soft-hyphen stripping matches the rest of
          // extractWords and matches layout's preprocessor.
          int16_t offsetX =
              prefix.empty() ? 0 : measureWordAdvanceX(renderer, SETTINGS.getReaderFontId(), prefix, wordStyle);
          int16_t partWidth = measureWordAdvanceX(renderer, SETTINGS.getReaderFontId(), part, wordStyle);
          {
            uint16_t off = WordSelectNavigator::poolAppend(textPool, part.c_str(), part.size());
            WordSelectNavigator::WordInfo wi;
            wi.textOffset = off;
            wi.textLen = static_cast<uint16_t>(part.size());
            wi.lookupOffset = off;
            wi.lookupLen = wi.textLen;
            wi.screenX = static_cast<int16_t>(screenX + offsetX);
            wi.screenY = screenY;
            wi.width = partWidth;
            wi.style = wordStyle;
            wi.fontId = SETTINGS.getReaderFontId();
            wi.isRtl = wordIsRtl;
            words.push_back(wi);
          }
        }
        // Dash-separated pieces are independent lookup choices. Do not infer
        // that the following source token was directly attached to the last
        // emitted piece merely because the dash itself is not selectable.
        lastSelectableWordIndex = -2;
      }
    }
  }

  WordSelectNavigator::organizeIntoRows(words, rows);
}

void DictionaryWordSelectActivity::mergeHyphenatedWords(std::vector<WordSelectNavigator::WordInfo>& words,
                                                        std::vector<WordSelectNavigator::Row>& rows,
                                                        std::string& textPool) {
  WordSelectNavigator::mergeHyphenatedPairs(words, rows, textPool);

  // Cross-page hyphenation: update lookup text when the last word on this page
  // ends with a hyphen and its continuation begins the next page.
  if (!nextPageFirstWord.empty() && !rows.empty()) {
    const int lastWordIdx = rows.back().firstWord + rows.back().wordCount - 1;
    const char* lastWord = textPool.data() + words[lastWordIdx].textOffset;
    uint16_t lastLen = words[lastWordIdx].textLen;
    if (lastLen > 0 && utf8EndsWithHyphen(lastWord, lastLen) && lastWord[0] != '-') {
      std::string firstPart(lastWord, lastLen);
      utf8RemoveTrailingHyphen(firstPart);
      std::string merged = firstPart + nextPageFirstWord;
      uint16_t off = WordSelectNavigator::poolAppend(textPool, merged.c_str(), merged.size());
      words[lastWordIdx].lookupOffset = off;
      words[lastWordIdx].lookupLen = static_cast<uint16_t>(merged.size());
    }
  }
}

void DictionaryWordSelectActivity::openDictionarySwitch() {
  auto picker = makeUniqueNoThrow<DictionarySelectActivity>(renderer, mappedInput, cachePath, true, true);
  if (!picker) {
    LOG_ERR("DICT", "OOM: DictionarySelectActivity");
    return;
  }

  startActivityForResult(std::move(picker), [this](const ActivityResult& result) {
    forceFullRepaintOnNextRender();
    if (result.isCancelled) {
      requestUpdate();
      return;
    }
    const auto* selection = std::get_if<FilePathResult>(&result.data);
    if (!selection) {
      LOG_ERR("DICT", "Dictionary switch returned no path");
      requestUpdate();
      return;
    }
    Dictionary::setLookupDictPathOverride(selection->path.c_str());
    controller.startLookup(controller.getLookupWord(), false);
  });
}

void DictionaryWordSelectActivity::loop() {
  if (ignoreInitialBackRelease_) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        !mappedInput.isPressed(MappedInputManager::Button::Back)) {
      ignoreInitialBackRelease_ = false;
    }
    return;
  }

  if (controller.isActive()) {
    switch (controller.handleInput()) {
      case DictionaryLookupController::LookupEvent::FoundDefinition: {
        // Rebuild the reader page while its font is still resident. The child
        // then overlays the modal after swapping to the dictionary font, so we
        // never need a second framebuffer or two live SD-font families.
        {
          RenderLock lock(*this);
          renderDefinitionBackground();
        }
        auto definition = makeUniqueNoThrow<DictionaryDefinitionActivity>(
            renderer, mappedInput, controller.getFoundWord(), controller.getFoundLocation(), true, cachePath,
            controller.getRecordHistory(), controller.getLookupWord(),
            DictionaryLookupController::toHistStatus(controller.getFoundStatus()),
            readerBackgroundRender_ ? readerContext_ : this,
            readerBackgroundRender_ ? readerBackgroundRender_
                                    : &DictionaryWordSelectActivity::renderDefinitionBackgroundCallback,
            dictionaryFontFamilyName_, dictionaryFontPointSize_, true, &highlightSnapshotStorage_);
        if (!definition) {
          LOG_ERR("DICT", "OOM allocating DictionaryDefinitionActivity (%u bytes)",
                  static_cast<unsigned>(sizeof(DictionaryDefinitionActivity)));
          forceFullRepaintOnNextRender();
          requestUpdate();
          break;
        }
        suspendWorkingSet();
        startActivityForResult(std::move(definition), [this](const ActivityResult& result) {
          if (!result.isCancelled) {
            setResult(ActivityResult{});
            finish();
          } else {
            {
              RenderLock lock(*this);
              if (!restoreWorkingSet()) {
                ActivityResult parentResult;
                parentResult.isCancelled = true;
                setResult(std::move(parentResult));
                finish();
                return;
              }
            }
            forceFullRepaintOnNextRender();
            requestUpdate();
          }
        });
        break;
      }
      case DictionaryLookupController::LookupEvent::NotFoundDismissedBack:
        forceFullRepaintOnNextRender();
        requestUpdate();
        break;
      case DictionaryLookupController::LookupEvent::NotFoundDismissedDone:
        setResult(ActivityResult{});
        finish();
        break;
      case DictionaryLookupController::LookupEvent::SwitchDictionary:
        openDictionarySwitch();
        break;
      case DictionaryLookupController::LookupEvent::Cancelled:
        forceFullRepaintOnNextRender();
        requestUpdate();
        break;
      default:
        break;
    }
    return;
  }

  if (navigator.isEmpty()) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      DictUtils::cancelAndFinish(*this);
    }
    return;
  }

  if (navigator.handleNavigation(mappedInput, renderer)) {
    requestUpdate();
  }

#if CROSSINK_APP_CAP_TOUCH
  if (touchDragLookup_) {
    int dragX = 0;
    int dragY = 0;
    if (mappedInput.isScreenTouchHeld(dragX, dragY)) {
      if (navigator.selectWordAtPoint(dragX, dragY, renderer.getLineHeight(SETTINGS.getReaderFontId()))) {
        requestUpdate();
      }
      return;
    }

    touchDragLookup_ = false;
    controller.lookupOrPopup(navigator.finishTouchMultiSelect());
    return;
  }

  int touchX = 0;
  int touchY = 0;
  if (mappedInput.wasScreenTouchDown(touchX, touchY)) {
    bool touchedWord = false;
    navigator.selectWordAtPoint(touchX, touchY, renderer.getLineHeight(SETTINGS.getReaderFontId()), &touchedWord);
    if (touchedWord && navigator.beginTouchMultiSelect()) {
      touchDragLookup_ = true;
      // Finish this fast refresh before lookup can replace the screen, so the
      // touched word always provides visible press feedback on e-ink.
      requestUpdateAndWait();
    }
    return;
  }
#endif

  // Check Back early when not in multi-select mode. This allows exit even when
  // confirmReleaseConsumed is stuck true (menu-triggered entry has no Confirm release).
  if (!navigator.isMultiSelecting() && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    DictUtils::cancelAndFinish(*this);
    return;
  }

  if (controller.handleMultiSelect(navigator)) return;

  if (navigator.isMultiSelecting()) return;

  if (controller.handleConfirmLookup(navigator)) return;

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    DictUtils::cancelAndFinish(*this);
    return;
  }
}

void DictionaryWordSelectActivity::render(RenderLock&&) {
  const int lineHeight = renderer.getLineHeight(SETTINGS.getReaderFontId());
  const int currIdx = navigator.getCurrentFlatIndex();

  // Differential fast path. Only valid when:
  //   - we set it up on the previous frame (RenderMode::Differential),
  //   - the controller has nothing pending to draw,
  //   - we have a current selection.
  if (nextRenderMode_ == RenderMode::Differential && !controller.isActive() && currIdx >= 0) {
    prewarmHighlightGlyphs(currIdx);
    auto dirty = navigator.renderHighlightDifferential(renderer, lineHeight, prevHighlightIdx_, currIdx);
    if (dirty.has_value()) {
      // Push full panel — the SDK's windowed-refresh path produces alternating black→white
      // transition failures on consecutive fast partial refreshes, so it's intentionally not
      // wired up here. The savings come from skipping page->render, which dominates the
      // pre-optimization cost; the full push at the end is a hardware floor (~444ms).
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);
      prevHighlightIdx_ = currIdx;
      return;
    }
    // Fall through to full repaint.
  }

  // Skip-initial-render fast path. Fires at most once per activity instance,
  // when the caller signalled the framebuffer already contains the page at
  // our margins (currently only EpubReaderActivity's hold-to-lookup path).
  // Conditions:
  //   - flag still set (one-shot),
  //   - controller has nothing to draw (an active controller would mean we
  //     re-entered render() after a sub-activity returned without the
  //     framebuffer being reset by forceFullRepaintOnNextRender()),
  //   - we have a current selection (currIdx >= 0); otherwise there is
  //     nothing to overlay and we fall through to a normal repaint.
  // We consume the flag unconditionally on first entry so any later
  // full-repaint goes through the normal clearScreen + page->render path.
  if (framebufferContainsPage_) {
    framebufferContainsPage_ = false;
    if (!controller.isActive() && currIdx >= 0) {
      // Clear the bottom strip the caller reserved (status bar OR auto-turn
      // label). Match the menu→lookup path, which wipes via clearScreen() +
      // page->render(); we skipped both, so clear that one region instead.
      if (reservedBottomHeight_ > 0) {
        int bezelTop, bezelRight, bezelBottom, bezelLeft;
        renderer.getOrientedViewableTRBL(&bezelTop, &bezelRight, &bezelBottom, &bezelLeft);
        const int clearY = renderer.getScreenHeight() - bezelBottom - reservedBottomHeight_;
        const int clearW = renderer.getScreenWidth() - bezelLeft - bezelRight;
        renderer.fillRect(bezelLeft, clearY, clearW, reservedBottomHeight_, false);
      }

      prewarmHighlightGlyphs(currIdx);

      auto setup = navigator.renderHighlightDifferential(renderer, lineHeight, /*prevWordIdx=*/-1, currIdx);
      bool snapshotPrimed = setup.has_value();
      if (!snapshotPrimed) {
        // Hyphenated wrap or oversize capture. The framebuffer still holds
        // the page, but we cannot prime the snapshot for the differential
        // path. Draw the multi-word highlight (which overwrites pixels under
        // each highlight rect) and force the next render to do a full
        // repaint so the renderer state is consistent. The user just pays
        // for one regular page render on the next cursor move instead of
        // on entry.
        navigator.renderHighlight(renderer, lineHeight);
      }
      clearFrontButtonHintArea();
      DictUtils::drawWordSelectButtonHints(renderer, mappedInput, navigator);
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);
      prevHighlightIdx_ = currIdx;
      nextRenderMode_ = snapshotPrimed ? RenderMode::Differential : RenderMode::FullPage;
      return;
    }
    // Flag was set but conditions weren't met (controller active or no
    // current selection). Fall through to the normal full-repaint path.
  }

  // Full repaint path.
  renderer.clearScreen();
  if (controller.render()) {
    // Controller drew an overlay; framebuffer state is unknown.
    nextRenderMode_ = RenderMode::FullPage;
    prevHighlightIdx_ = -1;
    return;
  }

  // Font prewarm: scan pass accumulates text, then prewarm, then real render.
  // Without this, every cold codepoint cold-misses the 8-slot SD glyph
  // overflow ring and the page render serializes ~100+ individual SD reads.
  // Same pattern as EpubReaderActivity::renderContents().
  auto* fcm = renderer.getFontCacheManager();
  auto scope = fcm->createPrewarmScope();
  page->render(renderer, SETTINGS.getReaderFontId(), marginLeft, marginTop);  // scan pass
  scope.endScanAndPrewarm();
  page->render(renderer, SETTINGS.getReaderFontId(), marginLeft, marginTop);

  // Set up snapshot AND draw the highlight via the differential entry point with
  // prevWordIdx = -1 (no previous highlight to wipe). This both draws the highlight
  // for this frame and primes snapshot_ so the next frame can run the fast path.
  // If the navigator declines (multi-select, hyphenated, oversize), fall back to
  // the multi-word renderHighlight and stay on the full path next frame.
  //
  // The -1 literal is load-bearing: renderHighlightDifferential uses prevWordIdx
  // < 0 as the signal "framebuffer was just redrawn from scratch, discard any
  // stale snapshot rather than restoring it on top of fresh pixels." This is the
  // only path that disturbs the framebuffer outside the differential cycle, so
  // it's also the only call site that must pass -1.
  bool snapshotPrimed = false;
  if (currIdx >= 0) {
    auto setup = navigator.renderHighlightDifferential(renderer, lineHeight, /*prevWordIdx=*/-1, currIdx);
    snapshotPrimed = setup.has_value();
  }
  if (!snapshotPrimed) {
    navigator.renderHighlight(renderer, lineHeight);
  }

  clearFrontButtonHintArea();
  DictUtils::drawWordSelectButtonHints(renderer, mappedInput, navigator);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);

  prevHighlightIdx_ = currIdx;
  nextRenderMode_ = snapshotPrimed ? RenderMode::Differential : RenderMode::FullPage;
}
