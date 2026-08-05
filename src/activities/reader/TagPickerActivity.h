#pragma once

#include <string>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class TagPickerActivity final : public Activity {
 public:
  // initialTag preselects the matching row (e.g. when re-opening the picker
  // to change an existing tag). 0 = "No tag" is selected, which is index 0.
  TagPickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, char initialTag = 0);
  void onEnter() override;
  void loop() override;
  // A note typed and confirmed at the keyboard sits in pendingNote until Back
  // or Confirm hands it to the caller. Sleeping tears the activity stack down
  // with direct onExit() calls and never delivers that result, so the text
  // would go with no warning — hold the idle timer off while one is pending.
  bool preventAutoSleep() override;
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
  // Note typed during this visit. Once set, the picker stays open on a second
  // pass so the highlight can also be tagged, and the last row switches from
  // "Write a note..." to "Edit note..." to go back to the keyboard.
  std::string pendingNote;

  bool hasNote() const { return !pendingNote.empty(); }
  const char* noteRowLabel() const { return hasNote() ? "Edit note..." : "Write a note..."; }
  const char* headerTitle() const { return hasNote() ? "Add tag?" : "Tag Highlight"; }

  void openNoteKeyboard();
};
