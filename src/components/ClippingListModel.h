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

  // tagFilter_ is a char: 0 = show all, printable chars are real tags.
  // Control bytes below are attribute filters (not valid tag glyphs).
  static constexpr char kFilterNone = 0;
  static constexpr char kFilterUntagged = 0x01;  // "No tag"
  static constexpr char kFilterWithNote = 0x02;  // "With a note"
  static constexpr char kFilterBare = 0x03;      // "No tag or note"
  static constexpr char kFilterAnyTag = 0x04;    // "Any tag"
  // Labels, offer rule (0 < count < scanned), and predicates must stay in
  // lockstep with web/pages/highlights.js populateTagFilter / filteredHighlights
  // (*any, *none, *note, *bare). There is no shared schema — change both.

  // Recomputes the visible set, the tags in use, attribute-filter offers, and
  // the filter row, from the currently loaded CLIPPINGS and NOTES. Call after
  // anything that changes a clipping, tag, or note.
  void rebuild() { rebuildInternal(true); }

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

  // Attribute filters offered only when they match some but not all clippings.
  bool offerUntagged() const { return offerUntagged_; }
  bool offerWithNote() const { return offerWithNote_; }
  bool offerBare() const { return offerBare_; }

  // True when the filter picker would have at least one choice besides (or
  // including) a literal tag / attribute option.
  bool hasFilterOptions() const {
    return !tagsInUse_.empty() || offerUntagged_ || offerWithNote_ || offerBare_ || tagFilter_ != kFilterNone;
  }

  char tagFilter() const { return tagFilter_; }
  void setTagFilter(const char tag) { tagFilter_ = tag; }

  SortOrder sortOrder() const { return sortOrder_; }
  void setSortOrder(const SortOrder order) { sortOrder_ = order; }

 private:
  void rebuildInternal(const bool allowEmptyFilterReset) {
    visible_.clear();
    tagsInUse_.clear();
    offerUntagged_ = false;
    offerWithNote_ = false;
    offerBare_ = false;

    const size_t total = CLIPPINGS.clippingCount();
    visible_.reserve(total);
    size_t scanned = 0;
    size_t countUntagged = 0;
    size_t countWithNote = 0;
    size_t countBare = 0;

    // One pass for the visible set, tags in use, and attribute-offer counts.
    // Attribute counts must be taken over every clipping *before* the filter
    // continue — otherwise using a filter zeros the offers and strands the user.
    for (size_t i = 0; i < total; ++i) {
      const Clipping* c = CLIPPINGS.clippingAt(i);
      if (!c) continue;
      ++scanned;
      const Note* note = NOTES.getNoteForClipping(c->spineIndex, c->startPage, c->startWordIndex, c->timestamp);
      const char tag = note != nullptr ? note->tag : 0;
      const bool hasTag = tag != 0;
      const bool hasNote = note != nullptr && !note->text.empty();

      if (hasTag && std::find(tagsInUse_.begin(), tagsInUse_.end(), tag) == tagsInUse_.end()) {
        tagsInUse_.push_back(tag);
      }
      if (!hasTag) ++countUntagged;
      if (hasNote) ++countWithNote;
      if (!hasTag && !hasNote) ++countBare;

      switch (tagFilter_) {
        case kFilterNone:
          break;
        case kFilterUntagged:
          if (hasTag) continue;
          break;
        case kFilterAnyTag:
          if (!hasTag) continue;
          break;
        case kFilterWithNote:
          if (!hasNote) continue;
          break;
        case kFilterBare:
          if (hasTag || hasNote) continue;
          break;
        default:
          if (tag != tagFilter_) continue;
          break;
      }
      visible_.push_back(static_cast<uint16_t>(i));
    }
    std::sort(tagsInUse_.begin(), tagsInUse_.end());

    // Offer only when the filter would actually narrow the list.
    offerUntagged_ = countUntagged > 0 && countUntagged < scanned;
    offerWithNote_ = countWithNote > 0 && countWithNote < scanned;
    offerBare_ = countBare > 0 && countBare < scanned;

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

    const size_t optionCount =
        tagsInUse_.size() + (offerUntagged_ ? 2u : 0u) + (offerWithNote_ ? 1u : 0u) + (offerBare_ ? 1u : 0u);
    // Two or more choices (or a filter already on, so it can be cleared).
    showFilterRow_ = optionCount > 1 || tagFilter_ != kFilterNone;
    filterRowLabel_ = std::string(tr(STR_FILTER_BY_TAG)) + ":  " + activeFilterLabel();

    // Deleting the last match under an active filter must not leave a blank
    // list with no way back — clear the filter and rebuild once.
    if (allowEmptyFilterReset && visible_.empty() && tagFilter_ != kFilterNone) {
      tagFilter_ = kFilterNone;
      rebuildInternal(false);
    }
  }

  static std::string activeFilterLabel(const char filter) {
    switch (filter) {
      case kFilterNone:
        return std::string(tr(STR_ALL_TAGS));
      case kFilterUntagged:
        return "No tag";
      case kFilterAnyTag:
        return "Any tag";
      case kFilterWithNote:
        return "With a note";
      case kFilterBare:
        return "No tag or note";
      default:
        return std::string(1, filter);
    }
  }

  std::string activeFilterLabel() const { return activeFilterLabel(tagFilter_); }

  // Display row (less the filter row) -> index in ClippingStore.
  std::vector<uint16_t> visible_;
  std::vector<char> tagsInUse_;
  std::string filterRowLabel_;
  char tagFilter_ = kFilterNone;
  SortOrder sortOrder_ = SortOrder::Added;
  bool showFilterRow_ = false;
  bool offerUntagged_ = false;
  bool offerWithNote_ = false;
  bool offerBare_ = false;
};

}  // namespace crossnotes
