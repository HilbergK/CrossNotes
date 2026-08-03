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
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

}  // namespace DictUtils
