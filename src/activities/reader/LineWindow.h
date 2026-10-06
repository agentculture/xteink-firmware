#pragma once

#include <cstddef>
#include <cstdint>

// xteink fork (d3): paragraph scrolling over the cached pagination. A window is
// (page P, offset N): page P with its first N lines scrolled off the top and
// the first lines of page P+1 filling the gap at the bottom. Layout and
// pagination are untouched; the reader only moves the already laid-out
// elements of the two cached pages. Pure (no Page/renderer types) so the host
// tests (test/line_window) can check it.
//
// A "line" (unit) is a run of consecutive elements that share one yPos, so a
// text line is one unit and an image or rule is one unit of its own: an image
// scrolls off the top or comes in at the bottom whole, never cut.
namespace line_window {

struct Element {
  int16_t y;       // PageElement::yPos
  int16_t height;  // drawn extent below y (image height, rule thickness, 0 for text)
};

// Number of units on a page.
constexpr int unitCount(const Element* els, const size_t count) {
  int units = 0;
  for (size_t i = 0; i < count; ++i) {
    if (i == 0 || els[i].y != els[i - 1].y) ++units;
  }
  return units;
}

// Index of the first element of unit `unit` (count when out of range).
constexpr size_t unitStart(const Element* els, const size_t count, const int unit) {
  int seen = -1;
  for (size_t i = 0; i < count; ++i) {
    if (i == 0 || els[i].y != els[i - 1].y) {
      if (++seen == unit) return i;
    }
  }
  return count;
}

// A window: page P with its first `offset` units scrolled off.
struct Position {
  int page;
  int offset;
};

// Line pitch of a page: the smallest gap between two consecutive units (plain
// text lines sit exactly one pitch apart; paragraph spacing, headings and
// images only widen a gap). `fallback` when the page has fewer than two units.
constexpr int linePitch(const Element* els, const size_t count, const int fallback) {
  int pitch = 0;
  size_t prev = 0;
  for (size_t i = 1; i < count; ++i) {
    if (els[i].y == els[i - 1].y) continue;
    const int gap = els[i].y - els[prev].y;
    if (gap > 0 && (pitch == 0 || gap < pitch)) pitch = gap;
    prev = i;
  }
  return pitch > 0 ? pitch : fallback;
}

// Paragraph scroll (xteink d3, operator request 2026-10-07): a paragraph starts
// at a unit whose gap from the unit above is wider than a line pitch, i.e. a
// line with (part of) an empty line before it. "Extra paragraph spacing" (on by
// default) adds half a line; CSS margins and images widen gaps too. Unit 0 is
// treated as a start: whether P begins a paragraph or continues one from P-1
// cannot be read from the layout (the paginator resets y at a page break), and
// treating it as a start never skips text.
constexpr bool isParagraphStart(const Element* els, const size_t count, const int unit, const int pitch) {
  if (unit <= 0) return true;
  const size_t at = unitStart(els, count, unit);
  if (at >= count) return false;
  const size_t above = unitStart(els, count, unit - 1);
  return (els[at].y - els[above].y) * 4 > pitch * 5;  // more than 1.25 pitch
}

// First paragraph start after unit `offset` on the page, or -1 when the rest of
// the page belongs to the paragraph at `offset`.
constexpr int nextParagraph(const Element* els, const size_t count, const int offset, const int fallbackPitch) {
  const int units = unitCount(els, count);
  const int pitch = linePitch(els, count, fallbackPitch);
  for (int u = (offset < 0 ? 0 : offset) + 1; u < units; ++u) {
    if (isParagraphStart(els, count, u, pitch)) return u;
  }
  return -1;
}

// Last paragraph start before unit `before` (pass unitCount() for "the last
// paragraph on the page"); 0 at worst, -1 when `before` <= 0.
constexpr int prevParagraph(const Element* els, const size_t count, const int before, const int fallbackPitch) {
  if (before <= 0) return -1;
  const int units = unitCount(els, count);
  const int pitch = linePitch(els, count, fallbackPitch);
  for (int u = (before > units ? units : before) - 1; u > 0; --u) {
    if (isParagraphStart(els, count, u, pitch)) return u;
  }
  return 0;
}

// Window position after a one-paragraph step forward from (P, offset): the next
// paragraph start on P, else the top of P+1. No P+1 in the section: unchanged.
constexpr Position paragraphForward(const Position at, const int nextOnPage, const bool hasNext) {
  if (!hasNext) return at;
  if (nextOnPage > at.offset) return {at.page, nextOnPage};
  return {at.page + 1, 0};
}

struct Plan {
  bool valid = false;    // false: render the page as is
  size_t firstKept = 0;  // elements [firstKept, curCount) of P stay
  int curShift = 0;      // added to their y
  size_t nextTaken = 0;  // elements [0, nextTaken) of P+1 are appended
  int nextShift = 0;     // added to their y
};

// Plan the window (P, offset). `fallbackPitch` is used when neither page has
// two distinct lines to measure the line pitch from. Appended elements must
// end at or above the bottom of P's own content, so nothing spills into the
// footer or status bar.
constexpr Plan plan(const Element* cur, const size_t curCount, const Element* next, const size_t nextCount,
                    const int offset, const int fallbackPitch) {
  Plan out;
  if (offset <= 0 || curCount == 0 || nextCount == 0) return out;
  const int units = unitCount(cur, curCount);
  if (offset >= units) return out;

  // Line pitch: the smallest line-to-line gap on P (a paragraph gap is wider),
  // else on P+1.
  const int pitch = linePitch(cur, curCount, linePitch(next, nextCount, fallbackPitch));
  const size_t lastStart = unitStart(cur, curCount, units - 1);

  int bottom = 0;  // lowest extent of P's content (its usable area)
  for (size_t i = 0; i < curCount; ++i) {
    const int end = cur[i].y + (cur[i].height > 0 ? cur[i].height : 0);
    if (end > bottom) bottom = end;
  }

  out.firstKept = unitStart(cur, curCount, offset);
  out.curShift = cur[0].y - cur[out.firstKept].y;
  // P+1's first line goes one pitch below P's last line, after the shift.
  out.nextShift = cur[lastStart].y + out.curShift + pitch - next[0].y;
  size_t taken = 0;
  while (taken < nextCount) {
    // Take whole units only.
    size_t end = taken + 1;
    while (end < nextCount && next[end].y == next[taken].y) ++end;
    bool fits = true;
    for (size_t i = taken; i < end; ++i) {
      const int top = next[i].y + out.nextShift;
      const int extent = top + (next[i].height > 0 ? next[i].height : 0);
      if (top > cur[lastStart].y || extent > bottom) fits = false;
    }
    if (!fits) break;
    taken = end;
  }
  out.nextTaken = taken;
  out.valid = true;
  return out;
}

// Offset carried across a page turn: kept, but clamped to the new page, and
// dropped when the new page is the last of its section (no P+1 to fill from;
// showing it plain repeats a few lines rather than losing any).
constexpr int carriedOffset(const int offset, const int newPageUnits, const bool newPageHasNext) {
  if (!newPageHasNext || offset <= 0 || newPageUnits <= 1) return 0;
  return offset < newPageUnits ? offset : newPageUnits - 1;
}

}  // namespace line_window
