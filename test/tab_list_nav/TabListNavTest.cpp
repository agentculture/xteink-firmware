#include <gtest/gtest.h>

#include "src/activities/TabListNav.h"

using tab_list_nav::Key;
using tab_list_nav::Move;
using tab_list_nav::moveFor;
using tab_list_nav::ringAfterTabSwitch;
using tab_list_nav::ringPage;
using tab_list_nav::ringStep;

TEST(TabListNavTest, PortraitLeftRightSwitchTabsUpDownMoveRows) {
  EXPECT_EQ(moveFor(Key::Left, false), Move::TabPrevious);
  EXPECT_EQ(moveFor(Key::Right, false), Move::TabNext);
  EXPECT_EQ(moveFor(Key::Up, false), Move::RowPrevious);
  EXPECT_EQ(moveFor(Key::Down, false), Move::RowNext);
}

TEST(TabListNavTest, SwappedOrientationFlipsBothAxesLikeNavNext) {
  // NavNext = side Up + front Left when swapped (MappedInputManager.cpp).
  EXPECT_EQ(moveFor(Key::Left, true), Move::TabNext);
  EXPECT_EQ(moveFor(Key::Right, true), Move::TabPrevious);
  EXPECT_EQ(moveFor(Key::Up, true), Move::RowNext);
  EXPECT_EQ(moveFor(Key::Down, true), Move::RowPrevious);
}

TEST(TabListNavTest, MoveKindAndDirection) {
  EXPECT_TRUE(tab_list_nav::isTabMove(Move::TabNext));
  EXPECT_TRUE(tab_list_nav::isTabMove(Move::TabPrevious));
  EXPECT_FALSE(tab_list_nav::isTabMove(Move::RowNext));
  EXPECT_EQ(tab_list_nav::direction(Move::TabNext), 1);
  EXPECT_EQ(tab_list_nav::direction(Move::RowPrevious), -1);
}

TEST(TabListNavTest, RingStepWalksBandAndRowsWithWrap) {
  // 3 rows: ring 0 (band), 1..3.
  EXPECT_EQ(ringStep(0, 3, 1), 1);
  EXPECT_EQ(ringStep(1, 3, 1), 2);
  EXPECT_EQ(ringStep(3, 3, 1), 0);
  EXPECT_EQ(ringStep(0, 3, -1), 3);
  EXPECT_EQ(ringStep(1, 3, -1), 0);
}

TEST(TabListNavTest, RingStepEmptyTabStaysOnBand) {
  EXPECT_EQ(ringStep(0, 0, 1), 0);
  EXPECT_EQ(ringStep(0, 0, -1), 0);
  EXPECT_EQ(ringStep(5, 0, 1), 0);
}

TEST(TabListNavTest, RingStepClampsOutOfRangeRing) {
  EXPECT_EQ(ringStep(9, 3, 1), 0);  // clamped to 3, then wraps to the band
  EXPECT_EQ(ringStep(-2, 3, 1), 1);
}

TEST(TabListNavTest, RingPageFromBandEntersRows) {
  EXPECT_EQ(ringPage(0, 20, 5, 1), 1);
  EXPECT_EQ(ringPage(0, 20, 5, -1), 20);
  EXPECT_EQ(ringPage(0, 0, 5, 1), 0);
}

TEST(TabListNavTest, RingPageJumpsByPagesAndWraps) {
  // rows 0..19 (ring 1..20), 5 per page.
  EXPECT_EQ(ringPage(1, 20, 5, 1), 6);
  EXPECT_EQ(ringPage(8, 20, 5, 1), 11);
  EXPECT_EQ(ringPage(17, 20, 5, 1), 1);  // last page wraps to the first row
  EXPECT_EQ(ringPage(8, 20, 5, -1), 1);
  EXPECT_EQ(ringPage(3, 20, 5, -1), 16);  // first page wraps to the last page
}

TEST(TabListNavTest, RingPageSingleStepsWhenRowsFit) {
  EXPECT_EQ(ringPage(1, 4, 10, 1), 2);
  EXPECT_EQ(ringPage(4, 4, 10, 1), 1);
  EXPECT_EQ(ringPage(1, 4, 10, -1), 4);
  EXPECT_EQ(ringPage(2, 4, 0, -1), 1);  // unknown page size
}

TEST(TabListNavTest, TabSwitchKeepsFocusKind) {
  EXPECT_EQ(ringAfterTabSwitch(true, 3, 5), 0);   // band stays on band
  EXPECT_EQ(ringAfterTabSwitch(false, 0, 5), 1);  // a row never lands on the band
  EXPECT_EQ(ringAfterTabSwitch(false, 3, 5), 3);  // subclass's choice kept
  EXPECT_EQ(ringAfterTabSwitch(false, 9, 5), 5);  // clamped to the new tab
  EXPECT_EQ(ringAfterTabSwitch(false, 2, 0), 0);  // empty tab: only the band
}
