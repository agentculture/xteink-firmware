// Host tests for the Home header clock's minute-tick decision.
#include <gtest/gtest.h>

#include "xteink/ClockTick.h"

using namespace xteink::clocktick;

namespace {
// Feed one reading, keep the new state, return the decision.
Decision feed(State& s, int minuteKey) {
  const Decision d = step(s, minuteKey);
  s = d.next;
  return d;
}

// Feed consecutive minutes starting after `from`, `n` times; returns the last decision.
Decision advance(State& s, int from, int n) {
  Decision d{};
  for (int i = 1; i <= n; i++) d = feed(s, (from + i) % 60);
  return d;
}
}  // namespace

TEST(ClockTick, FirstObservationDrawsNothing) {
  State s;
  const Decision d = feed(s, 42);
  EXPECT_FALSE(d.redraw);
  EXPECT_FALSE(d.half);
  EXPECT_EQ(s.ticks, 0u);
  EXPECT_TRUE(s.seen);
  EXPECT_EQ(s.lastMinute, 42);
}

TEST(ClockTick, UnchangedMinuteDoesNothing) {
  State s;
  feed(s, 10);
  for (int i = 0; i < 5; i++) {
    const Decision d = feed(s, 10);
    EXPECT_FALSE(d.redraw);
    EXPECT_FALSE(d.half);
  }
  EXPECT_EQ(s.ticks, 0u);
}

TEST(ClockTick, MinuteChangeRedrawsFast) {
  State s;
  feed(s, 10);
  const Decision d = feed(s, 11);
  EXPECT_TRUE(d.redraw);
  EXPECT_FALSE(d.half);
  EXPECT_EQ(s.ticks, 1u);
  EXPECT_EQ(s.lastMinute, 11);
}

TEST(ClockTick, HourOrDayWrapIsAChange) {
  State s;
  feed(s, 59);
  const Decision d = feed(s, 0);
  EXPECT_TRUE(d.redraw);
  EXPECT_FALSE(d.half);
  EXPECT_EQ(s.lastMinute, 0);
}

TEST(ClockTick, DayWrapOnMinuteOfDayKey) {
  State s;
  feed(s, 23 * 60 + 59);
  EXPECT_TRUE(feed(s, 0).redraw);
}

TEST(ClockTick, Tick14IsFast) {
  State s;
  feed(s, 0);
  const Decision d = advance(s, 0, 14);
  EXPECT_TRUE(d.redraw);
  EXPECT_FALSE(d.half);
  EXPECT_EQ(s.ticks, 14u);
}

TEST(ClockTick, Tick15IsHalf) {
  State s;
  feed(s, 0);
  const Decision d = advance(s, 0, 15);
  EXPECT_TRUE(d.redraw);
  EXPECT_TRUE(d.half);
  EXPECT_EQ(s.ticks, 15u);
}

TEST(ClockTick, Tick16IsFastAgain) {
  State s;
  feed(s, 0);
  const Decision d = advance(s, 0, 16);
  EXPECT_TRUE(d.redraw);
  EXPECT_FALSE(d.half);
}

TEST(ClockTick, Tick30IsHalf) {
  State s;
  feed(s, 0);
  int halves = 0;
  Decision d{};
  for (int i = 1; i <= 30; i++) {
    d = feed(s, i % 60);
    if (d.half) halves++;
  }
  EXPECT_TRUE(d.half);
  EXPECT_EQ(halves, 2);  // ticks 15 and 30 only
  EXPECT_EQ(s.ticks, 30u);
}

TEST(ClockTick, ResetOnReentryRestartsCounting) {
  State s;
  feed(s, 0);
  advance(s, 0, 14);
  s = State{};  // Home onEnter
  EXPECT_FALSE(feed(s, 30).redraw);
  EXPECT_EQ(s.ticks, 0u);
  // A fresh cycle: 14 fast ticks, then the 15th is half.
  const Decision d14 = advance(s, 30, 14);
  EXPECT_FALSE(d14.half);
  EXPECT_TRUE(feed(s, (30 + 15) % 60).half);
}
