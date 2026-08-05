#pragma once

#include <I18n.h>

#include <string>
#include <vector>

#include "../Activity.h"
#include "components/OptionPopup.h"

enum class FileBrowserAction : int {
  Delete = 0,
  PinFavorite = 1,
  UnpinFavorite = 2,
  SetSleepFolder = 3,
  ClearSleepFolder = 4,
  DeleteCache = 5,
  ToggleCompleted = 6,
  RemoveFromRecents = 7,
  DeleteStats = 8,
  ViewBookmarks = 9,
  ViewClippings = 10,
  DeleteBookmarks = 11,
  DeleteClippings = 12,
  EpubRenderMode = 13,
  ResetReaderSettings = 14,
  SendNearby = 15,
  EditNote = 16,      // CrossInk Notes — edit a clipping's note on-device
  OpenClipping = 17,  // CrossInk Notes — jump to a clipping's location in the book
  EditTag = 18,       // CrossInk Notes — change a clipping's tag on-device
  FilterTag = 19,       // CrossInk Notes — show only clippings with a chosen tag
  SortClippings = 20,   // CrossInk Notes — order the clipping list
};

class FileBrowserActionActivity final : public Activity {
 public:
  struct MenuItem {
    FileBrowserAction action;
    StrId labelId;
  };

  FileBrowserActionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string title,
                            std::vector<MenuItem> items, bool ignoreInitialConfirmRelease = false);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  void finishCancelled();

  std::string title;
  std::vector<MenuItem> items;
  std::vector<std::string> optionLabels;
  OptionPopup optionPopup;
  bool ignoreConfirmRelease = false;
  bool ignoreTouchRelease = false;
  bool selectionMade = false;
};
