#include "TagPickerActivity.h"

#include <I18n.h>

#include "MappedInputManager.h"
#include "NoteStore.h"
#include "activities/ActivityResult.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/ButtonNavigator.h"

TagPickerActivity::TagPickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const char initialTag)
    : Activity("TagPicker", renderer, mappedInput) {
  for (int i = 0; i < OPTION_COUNT; i++) {
    if (OPTIONS[i].tag == initialTag && i != WRITE_NOTE_INDEX) {
      selectedIndex = i;
      break;
    }
  }
}

void TagPickerActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void TagPickerActivity::openNoteKeyboard() {
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, "Write Note", "", NoteStore::kNoteTextMax),
      [this](const ActivityResult& result) {
        if (!result.isCancelled) {
          const auto& kb = std::get<KeyboardResult>(result.data);
          if (!kb.text.empty()) {
            setResult(TagResult{0, kb.text});
            finish();
            return;
          }
        }
        // Keyboard cancelled or empty — stay on the picker.
        requestUpdate();
      });
}

void TagPickerActivity::loop() {
  // Back — cancel without tagging
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    // Suppress the Back *release* so it doesn't leak to the reader restored underneath.
    finishAfterBackPress();
    return;
  }

  // Confirm — select current option
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (selectedIndex == WRITE_NOTE_INDEX) {
      openNoteKeyboard();
      return;
    }
    setResult(TagResult{OPTIONS[selectedIndex].tag});
    finish();
    return;
  }

  // Up / Down navigation
  buttonNavigator.onNext([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, OPTION_COUNT);
    requestUpdate();
  });

  buttonNavigator.onPrevious([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, OPTION_COUNT);
    requestUpdate();
  });
}

void TagPickerActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "Tag Highlight");

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;

  // Custom rows sized to fill the whole page — symbols only, no pagination.
  const int rowHeight = contentHeight / OPTION_COUNT;
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);

  for (int i = 0; i < OPTION_COUNT; i++) {
    const int rowY = contentTop + i * rowHeight;
    const bool isSelected = i == selectedIndex;
    if (isSelected) {
      renderer.fillRect(0, rowY, pageWidth, rowHeight, true);
    }
    const char* label = OPTIONS[i].label;
    const int textX = (pageWidth - renderer.getTextWidth(UI_12_FONT_ID, label, EpdFontFamily::BOLD)) / 2;
    const int textY = rowY + (rowHeight - lineHeight) / 2;
    renderer.drawText(UI_12_FONT_ID, textX, textY, label, !isSelected, EpdFontFamily::BOLD);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
