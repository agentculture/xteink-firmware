#pragma once

#include <cstddef>
#include <cstdint>

// xteink fork (d3): line scrolling over the cached pagination. A window is
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

  // Line pitch: the gap between P's last two lines, else P+1's first two.
  int pitch = fallbackPitch;
  const size_t lastStart = unitStart(cur, curCount, units - 1);
  if (units >= 2) {
    pitch = cur[lastStart].y - cur[unitStart(cur, curCount, units - 2)].y;
  } else if (unitCount(next, nextCount) >= 2) {
    pitch = next[unitStart(next, nextCount, 1)].y - next[0].y;
  }
  if (pitch <= 0) pitch = fallbackPitch;

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

// Window position after a one-line step. `units` is unitCount() of page P
// (the page of the current window); `hasNext` whether P+1 exists in this
// section. Backward from offset 0 needs P-1's unit count (prevUnits, < 0 when
// there is no P-1 in this section).
struct Position {
  int page;
  int offset;
};

constexpr Position stepForward(const Position at, const int units, const bool hasNext) {
  if (!hasNext) return at;  // last page of the section: nothing below
  if (at.offset + 1 < units) return {at.page, at.offset + 1};
  return {at.page + 1, 0};
}

constexpr Position stepBackward(const Position at, const int prevUnits) {
  if (at.offset > 0) return {at.page, at.offset - 1};
  if (prevUnits <= 0) return at;  // first page of the section
  return {at.page - 1, prevUnits - 1};
}

// Offset carried across a page turn: kept, but clamped to the new page, and
// dropped when the new page is the last of its section (no P+1 to fill from;
// showing it plain repeats a few lines rather than losing any).
constexpr int carriedOffset(const int offset, const int newPageUnits, const bool newPageHasNext) {
  if (!newPageHasNext || offset <= 0 || newPageUnits <= 1) return 0;
  return offset < newPageUnits ? offset : newPageUnits - 1;
}

}  // namespace line_window
