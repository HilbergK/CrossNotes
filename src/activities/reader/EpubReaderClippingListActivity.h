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
  int listLeft = 0;
  int listWidth = 0;
  int noteLineHeight = 0;
  // Where the note line is drawn, resolved from the theme's list geometry so it
  // lines up with the rows the widget draws on any device/theme.
  int noteTextLeft = 0;
  int noteMaxWidth = 0;
  bool noteInvertOnSelect = false;

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
