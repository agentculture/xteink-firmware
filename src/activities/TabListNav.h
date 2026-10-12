#pragma once

#include <cstdint>

// xteink fork (d2): pure navigation policy for tabbed lists (UiTabListActivity).
// Front Left/Right switch tabs; side Up/Down walk the ring of the current tab
// (position 0 = the tab band, 1..rowCount = the rows). Kept free of hardware
// and Arduino types so the host tests can exercise it (test/tab_list_nav).
namespace tab_list_nav {

// The four directional logical buttons, as MappedInputManager resolves them
// (front Left/Right honour the user's front-button remap; Up/Down are the side
// keys).
enum class Key : uint8_t { Left, Right, Up, Down };

enum class Move : uint8_t { TabPrevious, TabNext, RowPrevious, RowNext };

// What a key does on a tabbed list. `swapped` is
// MappedInputManager::isNavDirectionSwapped(): the same orientations in which
// NavNext becomes side Up + front Left (MappedInputManager.cpp, NavNext), so
// "next" keeps pointing the way the rotated hint labels say.
constexpr Move moveFor(const Key key, const bool swapped) {
  switch (key) {
    case Key::Left:
      return swapped ? Move::TabNext : Move::TabPrevious;
    case Key::Right:
      return swapped ? Move::TabPrevious : Move::TabNext;
    case Key::Up:
      return swapped ? Move::RowNext : Move::RowPrevious;
    case Key::Down:
    default:
      return swapped ? Move::RowPrevious : Move::RowNext;
  }
}

constexpr bool isTabMove(const Move move) { return move == Move::TabPrevious || move == Move::TabNext; }

constexpr int direction(const Move move) { return (move == Move::TabNext || move == Move::RowNext) ? 1 : -1; }

// One press: step the ring (band + rows) with wrap-around, exactly as the
// pre-d2 ring walk did.
constexpr int ringStep(const int ring, const int rowCount, const int dir) {
  const int size = (rowCount > 0 ? rowCount : 0) + 1;
  if (size <= 1) return 0;
  const int cur = ring < 0 ? 0 : (ring >= size ? size - 1 : ring);
  return dir > 0 ? (cur + 1) % size : (cur + size - 1) % size;
}

// Held key: page through the rows like a plain list (UiListActivity's
// continuous navigation, ButtonNavigator::next/previousPageIndex). From the
// band, a hold enters the rows at the first (down) or last (up) row.
constexpr int ringPage(const int ring, const int rowCount, const int pageRows, const int dir) {
  if (rowCount <= 0) return 0;
  if (ring <= 0) return dir > 0 ? 1 : rowCount;
  const int row = (ring > rowCount ? rowCount : ring) - 1;
  if (pageRows <= 0 || rowCount <= pageRows) {
    // Everything fits: behave like single steps within the rows.
    return dir > 0 ? (row + 1) % rowCount + 1 : (row + rowCount - 1) % rowCount + 1;
  }
  const int lastPage = (rowCount - 1) / pageRows;
  const int page = row / pageRows;
  if (dir > 0) return (page < lastPage ? (page + 1) * pageRows : 0) + 1;
  return (page > 0 ? (page - 1) * pageRows : lastPage * pageRows) + 1;
}

// Ring position after a tab switch. Focus keeps its kind: the band stays on
// the band, a row selection stays in the rows (the subclass's stepTab may
// already have picked a row, e.g. Settings' first row or Text Settings'
// remembered row; this only repairs a landing on the band or past the end).
constexpr int ringAfterTabSwitch(const bool wasOnBand, const int ringInNewTab, const int newRowCount) {
  if (wasOnBand || newRowCount <= 0) return 0;
  if (ringInNewTab < 1) return 1;
  return ringInNewTab > newRowCount ? newRowCount : ringInNewTab;
}

}  // namespace tab_list_nav
