#pragma once
#include <GfxRenderer.h>
#include <I18n.h>
#include <MappedInputManager.h>

#include "activities/Activity.h"
#include "components/UITheme.h"
#include "util/WordSelectNavigator.h"

namespace DictUtils {

// D-006: Back-cancel pattern — sets isCancelled=true and finishes the activity.
inline void cancelAndFinish(Activity& act) {
  ActivityResult r;
  r.isCancelled = true;
  act.setResult(std::move(r));
  act.finish();
}

inline void drawWordSelectButtonHints(GfxRenderer& renderer, const MappedInputManager& mappedInput,
                                      const WordSelectNavigator& navigator) {
  const char* confirmLabel = navigator.isMultiSelecting() ? tr(STR_DONE) : tr(STR_LOOKUP_SHORT);
  const auto orientation = renderer.getOrientation();
  const bool isLandscape = orientation == GfxRenderer::Orientation::LandscapeClockwise ||
                           orientation == GfxRenderer::Orientation::LandscapeCounterClockwise;
  const char* previousLabel = isLandscape ? tr(STR_DIR_UP) : tr(STR_DIR_LEFT);
  const char* nextLabel = isLandscape ? tr(STR_DIR_DOWN) : tr(STR_DIR_RIGHT);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, previousLabel, nextLabel);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

}  // namespace DictUtils
