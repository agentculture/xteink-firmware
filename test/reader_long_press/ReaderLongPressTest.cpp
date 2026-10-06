#include <gtest/gtest.h>

#include <cstdint>

#include "src/activities/reader/ReaderLongPress.h"

using namespace reader_long_press;

namespace {
// CrossPointSettings::LONG_PRESS_MENU_FUNCTION (static_assert'ed against
// LP_MENU_DISABLED in EpubReaderActivity.cpp).
constexpr uint8_t kKosync = 0;
constexpr uint8_t kBookmark = 2;
constexpr uint8_t kDictionary = 3;
constexpr uint8_t kReaderMenu = 4;
}  // namespace

TEST(ReaderLongPressTest, X3DefaultLongPressConfirmZooms) { EXPECT_TRUE(confirmHoldZooms(true, LP_MENU_DISABLED)); }

TEST(ReaderLongPressTest, ExplicitLongPressFunctionWinsOnX3) {
  for (const uint8_t fn : {kKosync, kBookmark, kDictionary, kReaderMenu}) {
    EXPECT_FALSE(confirmHoldZooms(true, fn)) << static_cast<int>(fn);
  }
}

TEST(ReaderLongPressTest, X4NeverZoomsOnLongPressConfirm) {
  for (uint8_t fn = 0; fn < 5; ++fn) EXPECT_FALSE(confirmHoldZooms(false, fn));
}

TEST(ReaderLongPressTest, LongPressBackRotatesOnlyOnX3) {
  EXPECT_TRUE(backHoldRotates(true));
  EXPECT_FALSE(backHoldRotates(false));
}

TEST(ReaderLongPressTest, HoldIsSevenHundredMs) { EXPECT_EQ(HOLD_MS, 700UL); }

TEST(ReaderLongPressTest, ToggleSwitchesPortraitAndLandscape) {
  EXPECT_EQ(toggledOrientation(0), 3);  // portrait -> landscape CCW
  EXPECT_EQ(toggledOrientation(3), 0);
  EXPECT_EQ(toggledOrientation(2), 1);  // inverted -> landscape CW
  EXPECT_EQ(toggledOrientation(1), 2);
}

TEST(ReaderLongPressTest, ToggleTwiceIsIdentityAndAlwaysChangesAxis) {
  for (uint8_t o = 0; o < 4; ++o) {
    EXPECT_EQ(toggledOrientation(toggledOrientation(o)), o);
    const bool portrait = o == 0 || o == 2;
    const uint8_t t = toggledOrientation(o);
    EXPECT_NE(portrait, t == 0 || t == 2);
  }
}

TEST(ReaderLongPressTest, OutOfRangeOrientationFallsBackToPortrait) { EXPECT_EQ(toggledOrientation(9), 0); }
