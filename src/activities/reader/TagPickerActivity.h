#pragma once

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class TagPickerActivity final : public Activity {
 public:
  TagPickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

  static constexpr int OPTION_COUNT = 11;
  // Last row opens the on-device keyboard instead of returning a tag.
  static constexpr int WRITE_NOTE_INDEX = OPTION_COUNT - 1;

  struct Option {
    char tag;  // 0 = no tag
    const char* label;
  };

  static constexpr Option OPTIONS[OPTION_COUNT] = {
      {0, "No tag"},
      {'!', "!"},
      {'?', "?"},
      {'*', "*"},
      {'~', "~"},
      {'+', "+"},
      {'=', "="},
      {'#', "#"},
      {'<', "<"},
      {'>', ">"},
      {0, "Write a note..."},
  };

 private:
  int selectedIndex = 0;
  ButtonNavigator buttonNavigator;

  void openNoteKeyboard();
};
