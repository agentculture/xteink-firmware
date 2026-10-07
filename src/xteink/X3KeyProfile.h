#pragma once

#include <cstdint>

// xteink fork (d5): X3 key profile. Pure table, no hardware types, so the host
// tests (test/x3_key_profile) can check it.
//
// freeink-sdk's X3 ladder decode reports the left edge key as BTN_UP, the right
// edge key as BTN_DOWN and the four bottom keys as BTN_BACK, BTN_CONFIRM,
// BTN_LEFT, BTN_RIGHT (measured with the XKEY logger, docs/test-x3.md). The
// profile gives the edges the logical Left/Right roles and the two right-hand
// bottom keys the Up/Down roles. MappedInputManager reads every HalGPIO index
// through physicalFor(), so the front-button remap, NavNext/NavPrevious and the
// orientation swap all operate on these slots unchanged.
namespace xteink::x3keys {

// Same values as HalGPIO::BTN_* (static_assert'ed in MappedInputManager.cpp).
inline constexpr uint8_t BACK = 0;
inline constexpr uint8_t CONFIRM = 1;
inline constexpr uint8_t LEFT = 2;
inline constexpr uint8_t RIGHT = 3;
inline constexpr uint8_t UP = 4;
inline constexpr uint8_t DOWN = 5;

// Physical HalGPIO index that serves a logical slot. The swap is its own
// inverse, so the same function maps a physical key back to its slot.
constexpr uint8_t physicalFor(const uint8_t slot, const bool active) {
  if (!active) return slot;
  switch (slot) {
    case LEFT:
      return UP;  // left edge key
    case RIGHT:
      return DOWN;  // right edge key
    case UP:
      return LEFT;  // bottom key 3
    case DOWN:
      return RIGHT;  // bottom key 4
    default:
      return slot;  // Back, Confirm, Power
  }
}

// The X3's physical keys that reach the firmware (the top regular key is the
// chip RESET and cannot be bound), and the HalGPIO index the SDK decode reports
// for each.
enum class PhysicalKey : uint8_t { LeftEdge, RightEdge, Bottom1, Bottom2, Bottom3, Bottom4 };

constexpr uint8_t sdkIndex(const PhysicalKey key) {
  switch (key) {
    case PhysicalKey::LeftEdge:
      return UP;  // adc2 ~2222
    case PhysicalKey::RightEdge:
      return DOWN;  // adc2 ~2
    case PhysicalKey::Bottom1:
      return BACK;  // adc1 ~3515
    case PhysicalKey::Bottom2:
      return CONFIRM;  // adc1 ~2686
    case PhysicalKey::Bottom3:
      return LEFT;  // adc1 ~1486
    case PhysicalKey::Bottom4:
    default:
      return RIGHT;  // adc1 ~0
  }
}

// Logical slot a physical key drives (default front-button remap).
constexpr uint8_t slotFor(const PhysicalKey key, const bool active) { return physicalFor(sdkIndex(key), active); }

// List step of a directional slot as NavPrevious (-1) / NavNext (+1) resolve
// it, 0 for the rest. `swapped` is MappedInputManager::isNavDirectionSwapped().
constexpr int listStep(const uint8_t slot, const bool swapped) {
  const int step = (slot == LEFT || slot == UP) ? -1 : (slot == RIGHT || slot == DOWN) ? 1 : 0;
  return swapped ? -step : step;
}

// Reader page step (-1 previous, +1 next, 0 none) of a front slot: logical
// Left/Right, mirrored like ReaderUtils::detectPageTurn().
constexpr int readerPageStep(const uint8_t slot, const bool swapped) {
  return (slot == LEFT || slot == RIGHT) ? listStep(slot, swapped) : 0;
}

}  // namespace xteink::x3keys
