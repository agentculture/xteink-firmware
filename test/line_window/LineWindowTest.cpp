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

TEST(LineWindowTest, OffsetCarriesAcrossPageTurns) {
  EXPECT_EQ(line_window::carriedOffset(2, 20, true), 2);    // Right after 2 lines: next window offset by 2
  EXPECT_EQ(line_window::carriedOffset(25, 20, true), 19);  // clamped to the new page
  EXPECT_EQ(line_window::carriedOffset(2, 20, false), 0);   // last page of the chapter: plain
  EXPECT_EQ(line_window::carriedOffset(0, 20, true), 0);
}

// Paragraph pages: pitch 30, a paragraph gap adds half a line (15), as the
// default "Extra paragraph spacing" does. Paragraphs of 3, 4 and 2 lines:
// units 0-2, 3-6, 7-8.
namespace {
std::vector<Element> paragraphPage() {
  std::vector<Element> out;
  int y = 10;
  for (const int lines : {3, 4, 2}) {
    for (int i = 0; i < lines; ++i) {
      out.push_back({static_cast<int16_t>(y), 0});
      y += 30;
    }
    y += 15;
  }
  return out;
}
}  // namespace

TEST(LineWindowTest, LinePitchIgnoresParagraphGaps) {
  const auto page = paragraphPage();
  EXPECT_EQ(line_window::linePitch(page.data(), page.size(), 99), 30);
  const std::vector<Element> one = {{10, 0}, {10, 0}};
  EXPECT_EQ(line_window::linePitch(one.data(), one.size(), 99), 99);
}

TEST(LineWindowTest, ParagraphStartsAreLinesAfterAWiderGap) {
  const auto page = paragraphPage();
  const int pitch = 30;
  EXPECT_TRUE(line_window::isParagraphStart(page.data(), page.size(), 0, pitch));  // page top counts
  EXPECT_FALSE(line_window::isParagraphStart(page.data(), page.size(), 1, pitch));
  EXPECT_TRUE(line_window::isParagraphStart(page.data(), page.size(), 3, pitch));
  EXPECT_FALSE(line_window::isParagraphStart(page.data(), page.size(), 5, pitch));
  EXPECT_TRUE(line_window::isParagraphStart(page.data(), page.size(), 7, pitch));
}

TEST(LineWindowTest, NextParagraphSkipsTheRestOfTheCurrentOne) {
  const auto page = paragraphPage();
  EXPECT_EQ(line_window::nextParagraph(page.data(), page.size(), 0, 30), 3);
  EXPECT_EQ(line_window::nextParagraph(page.data(), page.size(), 1, 30), 3);  // mid-paragraph (carried offset)
  EXPECT_EQ(line_window::nextParagraph(page.data(), page.size(), 3, 30), 7);
  EXPECT_EQ(line_window::nextParagraph(page.data(), page.size(), 7, 30), -1);  // last paragraph on the page
}

TEST(LineWindowTest, PrevParagraphGoesToTheStartOfTheOneAbove) {
  const auto page = paragraphPage();
  EXPECT_EQ(line_window::prevParagraph(page.data(), page.size(), 7, 30), 3);
  EXPECT_EQ(line_window::prevParagraph(page.data(), page.size(), 5, 30), 3);  // mid-paragraph: its own start
  EXPECT_EQ(line_window::prevParagraph(page.data(), page.size(), 3, 30), 0);
  EXPECT_EQ(line_window::prevParagraph(page.data(), page.size(), 0, 30), -1);
  // "The last paragraph of the previous page" (backward from offset 0).
  EXPECT_EQ(line_window::prevParagraph(page.data(), page.size(), 9, 30), 7);
}

TEST(LineWindowTest, ParagraphForwardFallsThroughToTheNextPageTop) {
  const Position mid = line_window::paragraphForward({4, 0}, 3, true);
  EXPECT_EQ(mid.page, 4);
  EXPECT_EQ(mid.offset, 3);
  const Position over = line_window::paragraphForward({4, 7}, -1, true);
  EXPECT_EQ(over.page, 5);
  EXPECT_EQ(over.offset, 0);
  const Position stuck = line_window::paragraphForward({9, 0}, 3, false);  // last page of the chapter
  EXPECT_EQ(stuck.page, 9);
  EXPECT_EQ(stuck.offset, 0);
}

TEST(LineWindowTest, WithoutParagraphSpacingStepsBecomePageTurns) {
  // "Extra paragraph spacing" off: no wider gaps, so the whole page reads as one
  // paragraph and a step forward lands on the next page's top.
  const auto page = textPage(20);
  EXPECT_EQ(line_window::nextParagraph(page.data(), page.size(), 0, 30), -1);
  EXPECT_EQ(line_window::prevParagraph(page.data(), page.size(), 20, 30), 0);
}

TEST(LineWindowTest, FillFromNextPageUsesTheLinePitchNotAParagraphGap) {
  // P's last two lines straddle a paragraph gap; P+1's first line still goes
  // one line pitch (30) below P's last line, not 45.
  const auto cur = paragraphPage();  // 9 units, last at y=10+8*30+2*15=280
  const auto next = textPage(20);
  const auto p = plan(cur.data(), cur.size(), next.data(), next.size(), 3, 30);
  ASSERT_TRUE(p.valid);
  EXPECT_EQ(280 + p.curShift + 30, 10 + p.nextShift);
}
