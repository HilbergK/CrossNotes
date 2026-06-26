#pragma once

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class TagPickerActivity final : public Activity {
 public:
  TagPickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

  static constexpr int OPTION_COUNT = 7;

  struct Option {
    char tag;  // 0 = skip (no tag)
    const char* label;
    const char* description;
  };

  static constexpr Option OPTIONS[OPTION_COUNT] = {
      {'!', "Important", "!  Mark for review"},
      {'?', "Question", "?  I don't understand this"},
      {'>', "Key argument", ">  Core thesis / main point"},
      {'<', "Counterpoint", "<  Challenges the argument"},
      {'*', "Cite", "*  Worth referencing later"},
      {'~', "Verify", "~  Check this claim"},
      {0, "Skip", "   No tag — just save"},
  };

 private:
  int selectedIndex = 0;
  ButtonNavigator buttonNavigator;
};
