#include <gtest/gtest.h>

#include <cstdint>

#include "src/activities/TabListNav.h"
#include "src/xteink/X3KeyProfile.h"

namespace x3 = xteink::x3keys;
using tab_list_nav::Key;
using tab_list_nav::Move;
using x3::PhysicalKey;

namespace {

// Effect of a slot on a tabbed list (Settings, Text Settings): the logical
// Left/Right/Up/Down slots feed tab_list_nav::moveFor; Back/Confirm do not.
bool settingsMove(const uint8_t slot, const bool swapped, Move& out) {
  switch (slot) {
    case x3::LEFT:
      out = tab_list_nav::moveFor(Key::Left, swapped);
      return true;
    case x3::RIGHT:
      out = tab_list_nav::moveFor(Key::Right, swapped);
      return true;
    case x3::UP:
      out = tab_list_nav::moveFor(Key::Up, swapped);
      return true;
    case x3::DOWN:
      out = tab_list_nav::moveFor(Key::Down, swapped);
      return true;
    default:
      return false;
  }
}

struct Row {
  PhysicalKey key;
  uint8_t slot;        // logical button the key drives under the profile
  int homeStep;        // Home / plain lists: -1 up (NavPrevious), +1 down (NavNext)
  bool settingsKnown;  // directional on a tabbed list
  Move settings;
  int readerPage;  // reader page step: -1 previous, +1 next
  int readerLine;  // reader line scroll (d3): -1 one line back, +1 one line forward
};

// Reader line step of a slot: the EPUB reader feeds logical Up/Down through
// listStep (EpubReaderActivity::loop, X3 profile).
int readerLineStep(const uint8_t slot, const bool swapped) {
  return (slot == x3::UP || slot == x3::DOWN) ? x3::listStep(slot, swapped) : 0;
}

// The X3 profile, portrait (no orientation swap). Back/Confirm keep their
// screen-specific meaning (Home: open last book / select; Settings: back /
// toggle; reader: back / menu) and never move a list.
constexpr Row kTable[] = {
    {PhysicalKey::LeftEdge, x3::LEFT, -1, true, Move::TabPrevious, -1, 0},
    {PhysicalKey::RightEdge, x3::RIGHT, +1, true, Move::TabNext, +1, 0},
    {PhysicalKey::Bottom1, x3::BACK, 0, false, Move::TabPrevious, 0, 0},
    {PhysicalKey::Bottom2, x3::CONFIRM, 0, false, Move::TabPrevious, 0, 0},
    {PhysicalKey::Bottom3, x3::UP, -1, true, Move::RowPrevious, 0, -1},
    {PhysicalKey::Bottom4, x3::DOWN, +1, true, Move::RowNext, 0, +1},
};

}  // namespace

TEST(X3KeyProfileTest, MeasuredSdkDecode) {
  // XKEY logger, 2026-10-07 (docs/test-x3.md, Key mapping diagnostic).
  EXPECT_EQ(x3::sdkIndex(PhysicalKey::LeftEdge), x3::UP);
  EXPECT_EQ(x3::sdkIndex(PhysicalKey::RightEdge), x3::DOWN);
  EXPECT_EQ(x3::sdkIndex(PhysicalKey::Bottom1), x3::BACK);
  EXPECT_EQ(x3::sdkIndex(PhysicalKey::Bottom2), x3::CONFIRM);
  EXPECT_EQ(x3::sdkIndex(PhysicalKey::Bottom3), x3::LEFT);
  EXPECT_EQ(x3::sdkIndex(PhysicalKey::Bottom4), x3::RIGHT);
}

TEST(X3KeyProfileTest, PhysicalKeyToLogicalToEffect) {
  for (const Row& row : kTable) {
    SCOPED_TRACE(static_cast<int>(row.key));
    const uint8_t slot = x3::slotFor(row.key, true);
    EXPECT_EQ(slot, row.slot);
    EXPECT_EQ(x3::listStep(slot, false), row.homeStep);
    Move move = Move::TabPrevious;
    EXPECT_EQ(settingsMove(slot, false, move), row.settingsKnown);
    if (row.settingsKnown) {
      EXPECT_EQ(move, row.settings);
    }
    EXPECT_EQ(x3::readerPageStep(slot, false), row.readerPage);
    EXPECT_EQ(readerLineStep(slot, false), row.readerLine);
  }
}

TEST(X3KeyProfileTest, HomeKeepsBottom3UpAndBottom4Down) {
  // Before the profile 4.3/4.4 were logical Left/Right and already moved the
  // Home selection up/down through NavPrevious/NavNext; as Up/Down they still do.
  EXPECT_EQ(x3::listStep(x3::slotFor(PhysicalKey::Bottom3, false), false), -1);
  EXPECT_EQ(x3::listStep(x3::slotFor(PhysicalKey::Bottom4, false), false), +1);
  EXPECT_EQ(x3::listStep(x3::slotFor(PhysicalKey::Bottom3, true), false), -1);
  EXPECT_EQ(x3::listStep(x3::slotFor(PhysicalKey::Bottom4, true), false), +1);
}

TEST(X3KeyProfileTest, SettingsEdgesSwitchTabsBottomKeysMoveRows) {
  Move move = Move::TabPrevious;
  ASSERT_TRUE(settingsMove(x3::slotFor(PhysicalKey::LeftEdge, true), false, move));
  EXPECT_TRUE(tab_list_nav::isTabMove(move));
  ASSERT_TRUE(settingsMove(x3::slotFor(PhysicalKey::RightEdge, true), false, move));
  EXPECT_TRUE(tab_list_nav::isTabMove(move));
  ASSERT_TRUE(settingsMove(x3::slotFor(PhysicalKey::Bottom3, true), false, move));
  EXPECT_EQ(move, Move::RowPrevious);
  ASSERT_TRUE(settingsMove(x3::slotFor(PhysicalKey::Bottom4, true), false, move));
  EXPECT_EQ(move, Move::RowNext);
}

TEST(X3KeyProfileTest, InactiveProfileIsIdentity) {
  for (uint8_t slot = 0; slot <= 6; ++slot) EXPECT_EQ(x3::physicalFor(slot, false), slot);
}

TEST(X3KeyProfileTest, SwapIsItsOwnInverse) {
  for (uint8_t slot = 0; slot <= 6; ++slot) EXPECT_EQ(x3::physicalFor(x3::physicalFor(slot, true), true), slot);
}

TEST(X3KeyProfileTest, OrientationSwapMirrorsBothAxes) {
  // isNavDirectionSwapped (inverted / landscape CCW) flips list and page steps
  // on top of the profile, exactly as NavNext does on the X4.
  EXPECT_EQ(x3::listStep(x3::UP, true), +1);
  EXPECT_EQ(x3::listStep(x3::DOWN, true), -1);
  EXPECT_EQ(x3::readerPageStep(x3::LEFT, true), +1);
  EXPECT_EQ(x3::readerPageStep(x3::RIGHT, true), -1);
}

TEST(X3KeyProfileTest, ListStepMatchesUpstreamNavNextComposition) {
  // Upstream NavNext = Down || Right (swapped: Up || Left); NavPrevious the mirror.
  EXPECT_EQ(x3::listStep(x3::DOWN, false), 1);
  EXPECT_EQ(x3::listStep(x3::RIGHT, false), 1);
  EXPECT_EQ(x3::listStep(x3::UP, false), -1);
  EXPECT_EQ(x3::listStep(x3::LEFT, false), -1);
  EXPECT_EQ(x3::listStep(x3::BACK, false), 0);
  EXPECT_EQ(x3::listStep(x3::CONFIRM, true), 0);
}
