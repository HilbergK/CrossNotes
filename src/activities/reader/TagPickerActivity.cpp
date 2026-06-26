#include "TagPickerActivity.h"

#include <I18n.h>

#include "MappedInputManager.h"
#include "activities/ActivityResult.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/ButtonNavigator.h"

TagPickerActivity::TagPickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("TagPicker", renderer, mappedInput) {}

void TagPickerActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
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

  // Use GUI.drawList — rowIcon is optional (nullptr = no icon column)
  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, OPTION_COUNT, selectedIndex,
      [](int i) { return std::string(OPTIONS[i].label); }, [](int i) { return std::string(OPTIONS[i].description); });

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
