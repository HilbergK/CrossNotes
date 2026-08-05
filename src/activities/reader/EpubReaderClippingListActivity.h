#pragma once

#include <FreeInkApp.h>
#include <FreeInkUIGfxRenderer.h>

#include <array>
#include <atomic>
#include <string>
#include <vector>

#include "ClippingStore.h"
#include "NoteStore.h"
#include "activities/Activity.h"
#include "components/NotesListLayout.h"
#include "components/OptionPopup.h"
#include "util/ButtonNavigator.h"

class EpubReaderClippingListActivity final : public Activity {
 public:
  EpubReaderClippingListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
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
  using UiApp = freeink::ui::FreeInkApp<20, 4>;
  freeink::ui::GfxRendererTarget uiTarget;
  UiApp app;
  std::atomic<bool> uiReady{false};
  int visibleRows = 1;
  int topIndex = 0;
  int listTop = 0;
  int listBottom = 0;
  int listRowHeight = 0;
  int listRowStep = 0;
  std::vector<freeink::ui::ListItem> uiItems;
  std::array<std::string, 20> uiRawText;
  std::array<std::string, 20> uiLabels;
  // CrossInk Notes: per-row subtitle buffer (the chapter). Persistent because
  // fui::ListItem::subtitle holds a borrowed const char*.
  std::array<std::string, 20> uiSubtitles;
  // CrossInk Notes: third row line ("[tag] note"). The FreeInkUI list only
  // draws a label and a subtitle, so this line is drawn over the rendered rows
  // in the space reserved by the taller rowHeight.
  std::array<std::string, 20> uiNotes;
  // CrossInk Notes: tag filter. visibleClippings maps a row position to its
  // index in ClippingStore, so the list can show a subset without anything else
  // having to know: every store access goes through storeIndexFor(). An empty
  // filter (0) means show everything.
  std::vector<uint16_t> visibleClippings;
  char tagFilter = 0;
  // Tags this book actually uses, rebuilt with the visible set. Both the filter
  // row's visibility and the picker's contents derive from this, so they cannot
  // disagree about whether a tag exists.
  std::vector<char> tagsInUse;
  OptionPopup optionPopup;
  crossnotes::NotesListLayout notesLayout;

  // A filter row sits above the clippings when the book uses more than one tag,
  // so the filter is reachable by scrolling up instead of through the menu.
  // Every row index below is a *display* row: subtract filterRowOffset() to get
  // the position within visibleClippings.
  bool showFilterRow = false;
  std::string filterRowLabel;

  void rebuildVisibleClippings();
  int filterRowOffset() const { return showFilterRow ? 1 : 0; }
  bool isFilterRow(int row) const { return showFilterRow && row == 0; }
  int visibleCount() const { return static_cast<int>(visibleClippings.size()) + filterRowOffset(); }
  size_t storeIndexFor(int row) const {
    const int i = row - filterRowOffset();
    return (i >= 0 && i < static_cast<int>(visibleClippings.size()))
               ? static_cast<size_t>(visibleClippings[static_cast<size_t>(i)])
               : 0;
  }
  void showTagFilterMenu();


  static void listScreen(UiApp::ScreenType& screen, void* user);
  static void onRowEvent(const freeink::ui::ActionEvent& event, void* user);
  void buildListScreen(UiApp::ScreenType& screen);
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
