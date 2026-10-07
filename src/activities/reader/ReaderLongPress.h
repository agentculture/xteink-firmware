#pragma once

#include <cstdint>

// xteink fork (d5): reader long-press policy for the X3 key profile. Pure, so
// the host tests (test/reader_long_press) can check it. The press/hold
// arbitration itself is MappedInputManager::wasLongPressed(): it fires once at
// the threshold while the key is still down and marks the release suppressed,
// and ActivityManager::loop() swallows that release frame, so the short-press
// action (reader menu, Back navigation) never fires after a long press.
namespace reader_long_press {

// Hold time for both X3 long-press actions (same as ReaderUtils::SKIP_HOLD_MS).
inline constexpr unsigned long HOLD_MS = 700;

// CrossPointSettings::LONG_PRESS_MENU_FUNCTION value meaning "no function".
inline constexpr uint8_t LP_MENU_DISABLED = 1;

// Long-press Confirm opens zoom mode on the X3 profile while the Long-Press Menu
// setting is at its default (Disabled, shown as "Zoom" there). An explicit
// choice (bookmark, dictionary, KOReader sync) keeps its upstream meaning, and
// the X4 keeps its side zoom key.
constexpr bool confirmHoldZooms(const bool x3Profile, const uint8_t longPressMenuFunction) {
  return x3Profile && longPressMenuFunction == LP_MENU_DISABLED;
}

// Long-press Back rotates on the X3 profile (it replaces upstream's long-press
// Back to the file browser there; short Back is unchanged).
constexpr bool backHoldRotates(const bool x3Profile) { return x3Profile; }

// Portrait <-> landscape, keeping the pairs that differ by one quarter turn:
// Portrait <-> Landscape CCW, Inverted <-> Landscape CW (CrossPointSettings::
// ORIENTATION values 0..3; the operator preferred this direction on the X3,
// 2026-10-07). Toggling twice returns to the start.
constexpr uint8_t toggledOrientation(const uint8_t orientation) {
  switch (orientation) {
    case 0:  // PORTRAIT
      return 3;
    case 1:  // LANDSCAPE_CW
      return 2;
    case 2:  // INVERTED
      return 1;
    case 3:  // LANDSCAPE_CCW
      return 0;
    default:
      return 0;
  }
}

}  // namespace reader_long_press
