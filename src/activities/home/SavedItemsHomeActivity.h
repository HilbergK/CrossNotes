#pragma once

#include <FreeInkApp.h>
#include <FreeInkUIGfxRenderer.h>

#include <array>
#include <atomic>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "components/NotesListLayout.h"
#include "util/ButtonNavigator.h"

struct SavedBookEntry {
  std::string bookTitle;
  std::string bookAuthor;
  std::string bookPath;
  std::string bookType;
  uint16_t bookmarkCount = 0;
  uint16_t clippingCount = 0;
  // CrossInk Notes: notes carrying a tag or text, and the composed row subtitle
  // ("12 highlights - 5 notes"). The subtitle lives here because
  // fui::ListItem::subtitle borrows a const char*.
  uint16_t noteCount = 0;
  std::string subtitle;
};

class SavedItemsHomeActivity final : public Activity {
 public:
  SavedItemsHomeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::vector<SavedBookEntry> books;
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;
  bool longPressOpenHandled = false;
  using UiApp = freeink::ui::FreeInkApp<20, 4>;
  freeink::ui::GfxRendererTarget uiTarget;
  UiApp app;
  std::atomic<bool> uiReady{false};
  int visibleRows = 1;
  int topIndex = 0;
  // CrossInk Notes: counts get their own third line, drawn in the row gap, so a
  // long author can no longer truncate them away. Geometry is resolved from the
  // theme in buildListScreen() and used by render().
  std::array<std::string, 20> uiCounts;
  crossnotes::NotesListLayout notesLayout;

  int listTop = 0;
  int listBottom = 0;
  int listRowHeight = 0;
  int listRowStep = 0;

  static void listScreen(UiApp::ScreenType& screen, void* user);
  static void onRowEvent(const freeink::ui::ActionEvent& event, void* user);
  void buildListScreen(UiApp::ScreenType& screen);

  void reloadSavedBooks();
  void openSavedItems(int bookIndex);
  void openBookmarkList(const SavedBookEntry& entry);
  void openClippingList(const SavedBookEntry& entry);
  void showSavedKindMenu(int bookIndex);
  void showSavedBookActionMenu(int bookIndex, bool ignoreInitialConfirmRelease);
};
