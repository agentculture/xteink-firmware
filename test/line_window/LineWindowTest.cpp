#include <gtest/gtest.h>

#include <vector>

#include "src/activities/reader/LineWindow.h"

using line_window::Element;
using line_window::plan;
using line_window::Position;

namespace {
// A text page: `lines` lines, pitch 30, first baseline at y=10.
std::vector<Element> textPage(const int lines, const int firstY = 10, const int pitch = 30) {
  std::vector<Element> out;
  for (int i = 0; i < lines; ++i) out.push_back({static_cast<int16_t>(firstY + i * pitch), 0});
  return out;
}
}  // namespace

TEST(LineWindowTest, UnitsGroupElementsOnOneLine) {
  const std::vector<Element> els = {{10, 0}, {10, 0}, {40, 0}, {70, 100}, {200, 0}};
  EXPECT_EQ(line_window::unitCount(els.data(), els.size()), 4);
  EXPECT_EQ(line_window::unitStart(els.data(), els.size(), 1), 2u);
  EXPECT_EQ(line_window::unitStart(els.data(), els.size(), 4), els.size());
}

TEST(LineWindowTest, OffsetZeroRendersPageAsIs) {
  const auto cur = textPage(20);
  const auto next = textPage(20);
  EXPECT_FALSE(plan(cur.data(), cur.size(), next.data(), next.size(), 0, 30).valid);
}

TEST(LineWindowTest, TwoLineOffsetShiftsUpAndFillsFromNextPage) {
  const auto cur = textPage(20);
  const auto next = textPage(20);
  const auto p = plan(cur.data(), cur.size(), next.data(), next.size(), 2, 30);
  ASSERT_TRUE(p.valid);
  EXPECT_EQ(p.firstKept, 2u);
  EXPECT_EQ(p.curShift, -60);
  EXPECT_EQ(p.nextTaken, 2u);
  // Last kept line of P ends at 10 + 19*30 - 60 = 520; next page line 0 goes one pitch below.
  EXPECT_EQ(next[0].y + p.nextShift, 550);
  EXPECT_EQ(next[1].y + p.nextShift, 580);  // == P's last line position: still fits
}

TEST(LineWindowTest, ImageFromNextPageComesInWholeOrNotAtAll) {
  const auto cur = textPage(20);
  std::vector<Element> next = {{10, 200}, {220, 0}};  // tall image first
  const auto p = plan(cur.data(), cur.size(), next.data(), next.size(), 1, 30);
  ASSERT_TRUE(p.valid);
  EXPECT_EQ(p.nextTaken, 0u);  // would spill below the page: gap stays blank
}

TEST(LineWindowTest, ImageScrollsOffTheTopAsOneUnit) {
  std::vector<Element> cur = {{10, 150}, {170, 0}, {200, 0}, {230, 0}};
  const auto next = textPage(5);
  const auto p = plan(cur.data(), cur.size(), next.data(), next.size(), 1, 30);
  ASSERT_TRUE(p.valid);
  EXPECT_EQ(p.firstKept, 1u);
  EXPECT_EQ(p.curShift, -160);
}

TEST(LineWindowTest, OffsetAtOrBeyondPageUnitsIsInvalid) {
  const auto cur = textPage(3);
  const auto next = textPage(3);
  EXPECT_FALSE(plan(cur.data(), cur.size(), next.data(), next.size(), 3, 30).valid);
  EXPECT_FALSE(plan(cur.data(), cur.size(), nullptr, 0, 1, 30).valid);
}

TEST(LineWindowTest, StepForwardWalksLinesThenPages) {
  EXPECT_EQ(line_window::stepForward({4, 0}, 20, true).offset, 1);
  const Position wrap = line_window::stepForward({4, 19}, 20, true);
  EXPECT_EQ(wrap.page, 5);
  EXPECT_EQ(wrap.offset, 0);
  const Position stuck = line_window::stepForward({9, 0}, 20, false);
  EXPECT_EQ(stuck.page, 9);
  EXPECT_EQ(stuck.offset, 0);
}

TEST(LineWindowTest, StepBackwardEntersPreviousPageAtItsLastLine) {
  EXPECT_EQ(line_window::stepBackward({4, 2}, 20).offset, 1);
  const Position back = line_window::stepBackward({4, 0}, 18);
  EXPECT_EQ(back.page, 3);
  EXPECT_EQ(back.offset, 17);
  const Position first = line_window::stepBackward({0, 0}, -1);
  EXPECT_EQ(first.page, 0);
  EXPECT_EQ(first.offset, 0);
}

TEST(LineWindowTest, OffsetCarriesAcrossPageTurns) {
  EXPECT_EQ(line_window::carriedOffset(2, 20, true), 2);    // Right after 2 lines: next window offset by 2
  EXPECT_EQ(line_window::carriedOffset(25, 20, true), 19);  // clamped to the new page
  EXPECT_EQ(line_window::carriedOffset(2, 20, false), 0);   // last page of the chapter: plain
  EXPECT_EQ(line_window::carriedOffset(0, 20, true), 0);
}

TEST(LineWindowTest, ForwardThroughAWholePageMatchesAPageTurn) {
  // 20 single steps from (4,0) land on (5,0): the same view a page turn gives.
  Position at{4, 0};
  for (int i = 0; i < 20; ++i) at = line_window::stepForward(at, 20, true);
  EXPECT_EQ(at.page, 5);
  EXPECT_EQ(at.offset, 0);
}
