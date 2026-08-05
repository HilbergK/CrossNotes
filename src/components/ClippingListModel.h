#pragma once

// CrossInk Notes — which clippings the list shows, and in what order.
//
// Upstream's clipping list draws ClippingStore in store order, one row per
// clipping, so a row index *is* a store index. CrossNotes adds a tag filter and
// a sort order, which breaks that: rows can be hidden, reordered, and offset by
// the filter row that sits above them.
//
// That mapping is the whole reason this file exists. Keeping it here does two
// things: it holds the divergence from upstream in a file we own, and it gives
// the display -> store conversion exactly one implementation. Assigning a store
// index to a row (or the reverse) has been the source of real bugs — a jump
// that landed on a neighbouring highlight, and a delete that removed the wrong
// one — so the two directions are named, and neither is done by hand:
//
//     storeIndexFor(row)            display row -> store index
//     displayRowForStoreIndex(i)    store index -> display row, or -1 if hidden
//
// The activity keeps everything the user is doing *to* the list — which row is
// selected, where it is scrolled, whether the detail view is open. This owns
// only what the list is made of, and recomputes it in rebuild().

#include <I18n.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ClippingStore.h"
#include "NoteStore.h"

namespace crossnotes {

class ClippingListModel {
 public:
  // Added is ClippingStore's own order. Clippings are appended as they are
  // made, so that is already creation order — note that Clipping::timestamp
  // cannot be used for this, as it counts from boot and resets.
  enum class SortOrder : uint8_t { Added, Location };

  // Recomputes the visible set, the tags in use, and the filter row, from the
  // currently loaded CLIPPINGS and NOTES. Call after anything that changes a
  // clipping or a tag.
  void rebuild() {
    visible_.clear();
    tagsInUse_.clear();
    const size_t total = CLIPPINGS.clippingCount();
    visible_.reserve(total);
    // One pass for both: the visible set and the tags in use each need this
    // clipping's note, and looking a note up is a linear scan of the notes — so
    // scanning twice made this O(clippings x notes) twice over.
    for (size_t i = 0; i < total; ++i) {
      const Clipping* c = CLIPPINGS.clippingAt(i);
      if (!c) continue;
      const Note* note = NOTES.getNoteForClipping(c->spineIndex, c->startPage, c->startWordIndex, c->timestamp);
      const char tag = note != nullptr ? note->tag : 0;
      if (tag != 0 && std::find(tagsInUse_.begin(), tagsInUse_.end(), tag) == tagsInUse_.end()) {
        tagsInUse_.push_back(tag);
      }
      if (tagFilter_ != 0 && tag != tagFilter_) continue;
      visible_.push_back(static_cast<uint16_t>(i));
    }
    std::sort(tagsInUse_.begin(), tagsInUse_.end());

    // Store order is creation order, so Added needs no work. Location reorders
    // by position in the book; stable_sort so two clippings starting at the
    // same word keep the order they were made in.
    if (sortOrder_ == SortOrder::Location) {
      std::stable_sort(visible_.begin(), visible_.end(), [](const uint16_t lhs, const uint16_t rhs) {
        const Clipping* a = CLIPPINGS.clippingAt(lhs);
        const Clipping* b = CLIPPINGS.clippingAt(rhs);
        if (!a || !b) return false;  // strict weak ordering: unreadable rows stay put
        if (a->spineIndex != b->spineIndex) return a->spineIndex < b->spineIndex;
        if (a->startPage != b->startPage) return a->startPage < b->startPage;
        return a->startWordIndex < b->startWordIndex;
      });
    }

    // Offer the filter row only when there is something to choose between: more
    // than one tag in use (or a filter already applied, so it can be cleared).
    showFilterRow_ = tagsInUse_.size() > 1 || tagFilter_ != 0;
    filterRowLabel_ = std::string(tr(STR_FILTER_BY_TAG)) + ":  " +
                      (tagFilter_ != 0 ? std::string(1, tagFilter_) : std::string(tr(STR_ALL_TAGS)));
  }

  // Rows the list draws, the filter row included.
  int rowCount() const { return static_cast<int>(visible_.size()) + filterRowOffset(); }

  int filterRowOffset() const { return showFilterRow_ ? 1 : 0; }
  bool isFilterRow(const int row) const { return showFilterRow_ && row == 0; }
  const std::string& filterRowLabel() const { return filterRowLabel_; }

  // Display row -> store index. Out-of-range rows (including the filter row)
  // fall back to 0; callers guard with isFilterRow() before using this.
  size_t storeIndexFor(const int row) const {
    const int i = row - filterRowOffset();
    return (i >= 0 && i < static_cast<int>(visible_.size())) ? static_cast<size_t>(visible_[static_cast<size_t>(i)])
                                                             : 0;
  }

  // Store index -> display row, or -1 when the current filter hides it. The
  // inverse of storeIndexFor(): a store index must never be assigned to a row.
  int displayRowForStoreIndex(const size_t storeIndex) const {
    for (size_t r = 0; r < visible_.size(); ++r) {
      if (static_cast<size_t>(visible_[r]) == storeIndex) return static_cast<int>(r) + filterRowOffset();
    }
    return -1;
  }

  // Tags this book actually uses, sorted. The filter row's existence and the
  // picker's contents both derive from this, so they cannot disagree.
  const std::vector<char>& tagsInUse() const { return tagsInUse_; }

  char tagFilter() const { return tagFilter_; }
  void setTagFilter(const char tag) { tagFilter_ = tag; }

  SortOrder sortOrder() const { return sortOrder_; }
  void setSortOrder(const SortOrder order) { sortOrder_ = order; }

 private:
  // Display row (less the filter row) -> index in ClippingStore.
  std::vector<uint16_t> visible_;
  std::vector<char> tagsInUse_;
  std::string filterRowLabel_;
  char tagFilter_ = 0;  // 0 = show everything
  SortOrder sortOrder_ = SortOrder::Added;
  bool showFilterRow_ = false;
};

}  // namespace crossnotes
