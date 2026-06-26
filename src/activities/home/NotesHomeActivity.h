#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

struct NotedBookEntry {
  std::string bookTitle;
  std::string bookAuthor;
  std::string bookPath;
  std::string bookType;
  uint16_t noteCount = 0;
};

class NotesHomeActivity final : public Activity {
 public:
  explicit NotesHomeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("NotesHome", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::vector<NotedBookEntry> books;
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;

  void reloadNotedBooks();
  void openNoteList(const NotedBookEntry& entry);
};
