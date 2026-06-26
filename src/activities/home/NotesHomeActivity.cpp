#include "NotesHomeActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include "../reader/EpubReaderClippingListActivity.h"
#include "ClippingStore.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void NotesHomeActivity::reloadNotedBooks() {
  books.clear();

  std::vector<ClippedBookEntry> clippedBooks;
  ClippingStore::getAllClippedBooks(clippedBooks);

  // For simplicity, we just list all books that have clippings.
  // Since our Session 4 update makes the Clippings screen show notes,
  // this acts as the entry point to those notes.
  for (const auto& entry : clippedBooks) {
    books.push_back({entry.bookTitle, entry.bookAuthor, entry.bookPath, entry.bookType, entry.count});
  }

  if (books.empty()) {
    selectedIndex = 0;
  } else if (selectedIndex >= static_cast<int>(books.size())) {
    selectedIndex = static_cast<int>(books.size()) - 1;
  }
}

void NotesHomeActivity::onEnter() {
  Activity::onEnter();
  reloadNotedBooks();
  requestUpdate();
}

void NotesHomeActivity::onExit() {
  books.clear();
  Activity::onExit();
}

void NotesHomeActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (!books.empty() && selectedIndex >= 0 && selectedIndex < static_cast<int>(books.size())) {
      openNoteList(books[selectedIndex]);
    }
    return;
  }

  const int pageItems = UITheme::getInstance().getNumberOfItemsPerPage(renderer, true, false, true, true);
  const int listSize = static_cast<int>(books.size());

  buttonNavigator.onNextRelease([this, listSize] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, listSize);
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this, listSize] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, listSize);
    requestUpdate();
  });
  buttonNavigator.onNextContinuous([this, listSize, pageItems] {
    selectedIndex = ButtonNavigator::nextPageIndex(selectedIndex, listSize, pageItems);
    requestUpdate();
  });
  buttonNavigator.onPreviousContinuous([this, listSize, pageItems] {
    selectedIndex = ButtonNavigator::previousPageIndex(selectedIndex, listSize, pageItems);
    requestUpdate();
  });
}

void NotesHomeActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "My Notes");

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  if (books.empty()) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + metrics.verticalSpacing,
                      "No notes found.");
  } else {
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, books.size(), selectedIndex,
        [this](int index) { return books[index].bookTitle; }, [this](int index) { return books[index].bookAuthor; },
        nullptr);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_HOME), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void NotesHomeActivity::openNoteList(const NotedBookEntry& entry) {
  CLIPPINGS.loadForBook(entry.bookPath, entry.bookTitle, entry.bookAuthor, entry.bookType);

  // We pass entry.bookPath to trigger Session 4's note loading
  startActivityForResult(
      std::make_unique<EpubReaderClippingListActivity>(renderer, mappedInput, CLIPPINGS.getClippings(), entry.bookPath),
      [this, entry](const ActivityResult& result) {
        if (!result.isCancelled) {
          const auto* clipping = std::get_if<ClippingJumpResult>(&result.data);
          if (clipping) {
            APP_STATE.pendingBookmarkSpine = clipping->spineIndex;
            APP_STATE.pendingBookmarkProgress =
                clipping->pageCount > 0 ? static_cast<float>(clipping->page) / clipping->pageCount : 0.0f;
            APP_STATE.pendingBookmarkParagraphIndex = clipping->paragraphIndex;
            APP_STATE.pendingClippingIndex = clipping->clippingIndex;
            APP_STATE.saveToFile();
            onSelectBook(entry.bookPath);
          } else {
            requestUpdate();
          }
        } else {
          reloadNotedBooks();
          requestUpdate();
        }
      });
}
