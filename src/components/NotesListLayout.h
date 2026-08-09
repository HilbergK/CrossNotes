#pragma once

// CrossInk Notes — three-line list rows.
//
// FreeInkUI's list draws a label and a subtitle. CrossNotes rows carry a third
// line (a highlight's tag+note, or a book's counts), which the widget has no
// slot for. Rather than scatter that workaround through the upstream activities,
// it lives here: those files keep deciding *what* the third line says, and this
// decides *where* everything goes and paints it.
//
// Two constraints drive the design, both learned the hard way:
//
//  * The widget centres the label+subtitle block vertically, so height reserved
//    inside a row is split above and below it. Reserving room for the third line
//    inside the row therefore leaves an equal band of dead space above it. The
//    third line goes in a widened row gap instead, and the row hugs its two.
//
//  * A selection painted in two pieces (the widget's row plus a fill under the
//    third line) shows a seam, drifts apart while scrolling, and misses rows
//    with no third line. So the widget paints no row background at all and the
//    whole entry is inverted here in one pass.
//
// Geometry is mirrored from the same theme tokens FreeInkApp::list resolves
// (side padding, row inset, scroll track) rather than from ListProps' struct
// defaults, which never survive to render time.

#include <GfxRenderer.h>

#include <algorithm>
#include <string>

#include "MappedInputManager.h"
#include "components/UIScale.h"
#include "components/UIThemeTokens.h"
#include "fontIds.h"

namespace crossnotes {

namespace fui = freeink::ui;

// Face for the subtitle and the third line. Bound to the list's FONT_SMALL slot
// and used to measure and truncate, which must agree: FreeInkUI addresses fonts
// by slot (FONT_SMALL == 0) while GfxRenderer uses hashed ids from fontIds.h,
// where 0 is the "not found" sentinel. Passing a slot to getTextWidth() silently
// measures nothing.
inline constexpr int kSmallFontId = SMALL_FONT_ID;

class NotesListLayout {
 public:
  // Configures props for three-line rows and captures the geometry needed to
  // draw them. Returns the number of rows that fit, like configureUiList().
  uint16_t configure(fui::ListProps& props, fui::GfxRendererTarget& uiTarget, const fui::ThemeTokens& theme,
                     const fui::Rect& bounds, const GfxRenderer& renderer, const MappedInputManager& input,
                     const int itemCount) {
    // uiScaleSpec() binds FONT_SMALL to the same font as FONT_BODY, so a row's
    // subtitle would otherwise match its label. Rebind this activity's slot to a
    // smaller face and pin the label to BODY. uiTarget belongs to the calling
    // activity, so no other screen is affected.
    uiTarget.setFont(fui::GfxRendererTarget::FONT_SMALL, kSmallFontId);
    props.labelText.font = fui::GfxRendererTarget::FONT_BODY;
    props.subtitleText.font = fui::GfxRendererTarget::FONT_SMALL;
    props.subtitleText.bold = false;

    lineHeight_ = renderer.getLineHeight(kSmallFontId);
    const int labelHeight = renderer.getLineHeight(uiScaleSpec().bodyFontId);
    int rowHeight = labelHeight + lineHeight_ + kRowPadding;
    if (input.hasTouchHardware()) {
      rowHeight = std::max(rowHeight, static_cast<int>(uiListRowHeight(theme, UiListRowType::WithSubtitle)));
    }
    props.rowHeight = static_cast<int16_t>(rowHeight);
    props.rowGap = static_cast<int16_t>(lineHeight_ + kRowPadding);

    // No background on any state: drawSelection() inverts the entry instead.
    fui::StyleSet styles;
    styles.normal.background = fui::Paint::none();
    styles.normal.foreground = fui::Paint::solid(fui::Color::Black);
    styles.selected = styles.normal;
    styles.focused = styles.normal;
    styles.active = styles.normal;
    styles.disabled = styles.normal;
    styles.explicitlySet = true;
    props.rowStyles = styles;

    const uint16_t rows = configureUiList(props, theme, bounds, UiListRowType::WithSubtitle);
    rowHeight_ = props.rowHeight;
    rowStep_ = props.rowHeight + props.rowGap;
    top_ = bounds.y;
    bottom_ = bounds.bottom();
    visibleRows_ = rows > 0 ? rows : 1;

    // Mirror FreeInkApp::list's own row geometry. The scroll track only takes
    // width while the list overflows, so that condition is mirrored too.
    const int rowInset = theme.listInset < 0 ? 0 : theme.listInset;
    const int sidePad = theme.listSidePadding < 0 ? 8 : theme.listSidePadding;
    const int scrollWidth = theme.listScrollWidth < 0 ? 3 : theme.listScrollWidth;
    const int scrollInset = theme.listScrollInset < 0 ? 0 : theme.listScrollInset;
    const bool scrollLeft = theme.listScrollSide == 1;
    int areaX = static_cast<int>(bounds.x) + rowInset;
    int areaWidth = static_cast<int>(bounds.width) - rowInset * 2;
    if (props.scrollIndicator && itemCount > static_cast<int>(visibleRows_) && scrollWidth > 0) {
      const int needed = scrollWidth + scrollInset + 2;
      if (rowInset < needed) {
        const int cut = needed - rowInset;
        areaWidth -= cut;
        if (scrollLeft) areaX += cut;
      }
    }
    fillLeft_ = areaX;
    fillWidth_ = areaWidth;
    textLeft_ = areaX + sidePad;
    textWidth_ = std::max(0, areaWidth - sidePad * 2);
    return rows;
  }

  int lineHeight() const { return lineHeight_; }
  int textWidth() const { return textWidth_; }
  int visibleRows() const { return static_cast<int>(visibleRows_); }
  bool ready() const { return rowStep_ > 0 && lineHeight_ > 0; }

  // Top of the given on-screen row (0 = first visible).
  int rowTop(const int slot) const { return top_ + slot * rowStep_; }

  // The third line, drawn in the gap below its row. Truncated to the row width.
  void drawThirdLine(const GfxRenderer& renderer, const int slot, const std::string& text) const {
    if (!ready() || text.empty()) return;
    const int y = rowTop(slot) + rowHeight_ + 2;
    if (y + lineHeight_ > bottom_) return;
    const std::string line = renderer.truncatedText(kSmallFontId, text.c_str(), textWidth_);
    renderer.drawText(kSmallFontId, textLeft_, y, line.c_str(), true);
  }

  // A line drawn at the top of its row, with a rule beneath — used for the tag
  // filter, which is one short control in a row sized for three lines.
  void drawHeaderLine(const GfxRenderer& renderer, const int slot, const std::string& text) const {
    if (!ready()) return;
    const int y = rowTop(slot) + 2;
    if (y + lineHeight_ > bottom_) return;
    const std::string line = renderer.truncatedText(kSmallFontId, text.c_str(), textWidth_);
    renderer.drawText(kSmallFontId, textLeft_, y, line.c_str(), true);
    const int ruleY = y + lineHeight_ + 3;
    if (ruleY < bottom_) renderer.drawLine(fillLeft_, ruleY, fillLeft_ + fillWidth_ - 1, ruleY);
  }

  // Height of a header row's visible band, for a matching selection.
  int headerBandHeight() const { return lineHeight_ + 4; }

  // The selection: one inverted rectangle over the whole entry, applied after
  // its text is drawn. Pass headerBandHeight() to highlight only a header line.
  void drawSelection(const GfxRenderer& renderer, const int slot, const int heightOverride = -1) const {
    if (!ready()) return;
    const int y = rowTop(slot);
    // Leave the sliver before the next entry clear so consecutive selections
    // never look merged.
    const int wanted = heightOverride > 0 ? heightOverride : rowStep_ - 2;
    const int height = std::min(wanted, bottom_ - y);
    if (height > 0) renderer.invertRect(fillLeft_, y, fillWidth_, height);
  }

 private:
  static constexpr int kRowPadding = 6;

  int lineHeight_ = 0;
  int rowHeight_ = 0;
  int rowStep_ = 0;
  int top_ = 0;
  int bottom_ = 0;
  int textLeft_ = 0;
  int textWidth_ = 0;
  int fillLeft_ = 0;
  int fillWidth_ = 0;
  uint16_t visibleRows_ = 1;
};

}  // namespace crossnotes
