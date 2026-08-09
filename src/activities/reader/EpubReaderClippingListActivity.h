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
#include "components/ClippingListModel.h"
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
  // CrossInk Notes: what the list is made of — which clippings are shown, in
  // what order, and the display-row <-> store-index mapping that follows from
  // that. Lives in ClippingListModel so the divergence from upstream sits in a
  // file we own. The sort order is deliberately not persisted, which keeps it
  // out of the upstream settings file; it resets to Added on each open.
  crossnotes::ClippingListModel model;
  OptionPopup optionPopup;
  crossnotes::NotesListLayout notesLayout;

  // Building a row reads its text off the SD card — one open/seek/read/close
  // each, since v1.5.0 moved clipping text out of the record — and the rows are
  // rebuilt on every render. Moving the selection redraws but changes none of
  // that text, so without this the whole visible window was re-read from SD on
  // every button press. Invalidated when the window moves or the data changes.
  int rowCacheTopIndex = -1;
  int rowCacheRows = -1;
  bool rowCacheDirty = true;

  // A filter row sits above the clippings when the book uses more than one tag,
  // so the filter is reachable by scrolling up instead of through the menu.
  // Every row index below is a *display* row: the model converts.
  //
  // These forward to the model so the call sites below read the same either
  // way; the mapping itself has one implementation, in ClippingListModel.
  void rebuildVisibleClippings();
  int filterRowOffset() const { return model.filterRowOffset(); }
  bool isFilterRow(int row) const { return model.isFilterRow(row); }
  int visibleCount() const { return model.rowCount(); }
  size_t storeIndexFor(int row) const { return model.storeIndexFor(row); }
  int displayRowForStoreIndex(size_t storeIndex) const { return model.displayRowForStoreIndex(storeIndex); }
  void showTagFilterMenu();
  void showSortMenu();
  // The list's own menu (filter, sort). showClippingActionMenu is the menu for
  // one highlight and belongs to the detail view, which is where a highlight is
  // already open — offering it from the list too put per-item actions on a row
  // the user had not chosen to act on.
  void showListMenu(bool ignoreInitialConfirmRelease);

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
