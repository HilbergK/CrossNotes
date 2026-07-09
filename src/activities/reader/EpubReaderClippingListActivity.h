#pragma once

#include <string>
#include <vector>

#include "ClippingStore.h"
#include "NoteStore.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class EpubReaderClippingListActivity final : public Activity {
 public:
  EpubReaderClippingListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                 std::vector<Clipping> clippings, std::string bookPath = {})
      : Activity("EpubClippingList", renderer, mappedInput),
        clippings(std::move(clippings)),
        bookPath(std::move(bookPath)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::vector<Clipping> clippings;
  std::string bookPath;
  std::string detailText;
  // Wrapped quote lines, with the note (if any) appended directly after —
  // one continuous paginated flow.
  std::vector<std::string> detailLines;
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;
  int detailPage = 0;
  int detailLayoutWidth = 0;
  int detailLinesPerPage = 0;
  bool longPressConfirmHandled = false;
  bool detailMode = false;

  int getPageItems() const;
  int getDetailTextWidth() const;
  int getDetailLinesPerPage() const;
  int getDetailPageCount() const;
  void deleteSelectedClipping();
  void closeDetail();
  void jumpToSelectedClipping();
  void openSelectedDetail();
  void rebuildDetailLayoutIfNeeded();
  void showClippingActionMenu(bool ignoreInitialConfirmRelease);
  void editNoteForClipping(const Clipping& clipping);
  void editTagForClipping(const Clipping& clipping);
  void renderDetail();
};
