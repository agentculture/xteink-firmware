#pragma once

#include <cstdint>

// xteink fork: when the Home header clock redraws, and with which refresh.
// Pure (no Arduino/HAL), so the host tests can pin it down
// (test/xteink_clock_tick). HomeActivity keeps a State, resets it in onEnter
// (State{}), and feeds it the local time on every loop iteration:
//
//   Decision d = clocktick::step(state, minuteKey);
//   state = d.next;
//   if (d.redraw) requestUpdate();   // render uses HALF_REFRESH when d.half
//
// Contract:
//  - minuteKey is any value that differs whenever the displayed minute does,
//    e.g. the minute 0-59 (59 -> 0 is a change) or minute-of-day.
//  - The first reading after reset only records the minute: no redraw, no tick
//    (entering Home already rendered once).
//  - A later reading that differs from the last one is one tick: redraw, and
//    ticks increments.
//  - half is set on every 15th tick (15, 30, ...) to clear e-ink ghosting; all
//    other redraws are fast.
namespace xteink::clocktick {

struct State {
  bool seen = false;   // a reading has been recorded since reset
  int lastMinute = 0;  // minuteKey of the last reading (valid when seen)
  uint32_t ticks = 0;  // minute changes observed since reset
};

constexpr uint32_t HALF_EVERY_TICKS = 15;

struct Decision {
  bool redraw = false;
  bool half = false;  // only ever true when redraw is true
  State next;         // state to store for the next call
};

constexpr Decision step(const State& s, int minuteKey) {
  Decision d;
  d.next = s;
  if (!s.seen) {
    d.next.seen = true;
    d.next.lastMinute = minuteKey;
    return d;
  }
  if (minuteKey == s.lastMinute) return d;
  d.next.lastMinute = minuteKey;
  d.next.ticks = s.ticks + 1;
  d.redraw = true;
  d.half = (d.next.ticks % HALF_EVERY_TICKS) == 0;
  return d;
}

}  // namespace xteink::clocktick
