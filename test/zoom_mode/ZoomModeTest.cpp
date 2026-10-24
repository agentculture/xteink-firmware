#include <gtest/gtest.h>

#include <vector>

#include "src/activities/reader/ZoomMode.h"

namespace {
const std::vector<uint8_t> kBuiltin{12, 14, 16, 18};
const std::vector<uint8_t> kVector{8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22};
}  // namespace

TEST(ZoomModeTest, InactiveByDefaultAndEmptyListRefused) {
  ZoomMode z;
  EXPECT_FALSE(z.active());
  EXPECT_FALSE(z.enter({}, 14));
  EXPECT_FALSE(z.active());
  EXPECT_FALSE(z.stepLarger());
  EXPECT_FALSE(z.stepSmaller());
}

TEST(ZoomModeTest, EnterSelectsCurrentSize) {
  ZoomMode z;
  ASSERT_TRUE(z.enter(kBuiltin, 16));
  EXPECT_TRUE(z.active());
  EXPECT_EQ(z.selectedPt(), 16);
}

TEST(ZoomModeTest, EnterSnapsToNearestAndTiesGoSmaller) {
  ZoomMode z;
  ASSERT_TRUE(z.enter(kBuiltin, 15));  // 14 and 16 equidistant
  EXPECT_EQ(z.selectedPt(), 14);
  ASSERT_TRUE(z.enter(kBuiltin, 30));
  EXPECT_EQ(z.selectedPt(), 18);
  ASSERT_TRUE(z.enter(kBuiltin, 4));
  EXPECT_EQ(z.selectedPt(), 12);
}

TEST(ZoomModeTest, BuiltinStepsAcrossFourSizesAndClampsAtEnds) {
  ZoomMode z;
  ASSERT_TRUE(z.enter(kBuiltin, 12));
  EXPECT_FALSE(z.stepSmaller());
  EXPECT_EQ(z.selectedPt(), 12);
  EXPECT_TRUE(z.stepLarger());
  EXPECT_EQ(z.selectedPt(), 14);
  EXPECT_TRUE(z.stepLarger());
  EXPECT_TRUE(z.stepLarger());
  EXPECT_EQ(z.selectedPt(), 18);
  EXPECT_FALSE(z.stepLarger());
  EXPECT_EQ(z.selectedPt(), 18);
}

TEST(ZoomModeTest, VectorStepsOnePointAtATime) {
  ZoomMode z;
  ASSERT_TRUE(z.enter(kVector, 14));
  EXPECT_TRUE(z.stepLarger());
  EXPECT_EQ(z.selectedPt(), 15);
  EXPECT_TRUE(z.stepSmaller());
  EXPECT_TRUE(z.stepSmaller());
  EXPECT_EQ(z.selectedPt(), 13);
  for (int i = 0; i < 20; ++i) z.stepLarger();
  EXPECT_EQ(z.selectedPt(), 22);
  for (int i = 0; i < 20; ++i) z.stepSmaller();
  EXPECT_EQ(z.selectedPt(), 8);
}

TEST(ZoomModeTest, CommitReportsChangeOnce) {
  ZoomMode z;
  ASSERT_TRUE(z.enter(kBuiltin, 14));
  z.stepLarger();
  const auto out = z.commit();
  EXPECT_TRUE(out.changed);
  EXPECT_EQ(out.pt, 16);
  EXPECT_FALSE(z.active());
}

TEST(ZoomModeTest, CommitAfterReturningToStartIsNotAChange) {
  ZoomMode z;
  ASSERT_TRUE(z.enter(kBuiltin, 14));
  z.stepLarger();
  z.stepSmaller();
  const auto out = z.commit();
  EXPECT_FALSE(out.changed);  // no reflow when nothing differs
  EXPECT_EQ(out.pt, 14);
}

TEST(ZoomModeTest, CommitWithoutStepsIsNotAChange) {
  ZoomMode z;
  ASSERT_TRUE(z.enter(kBuiltin, 16));
  EXPECT_FALSE(z.commit().changed);
}

TEST(ZoomModeTest, CancelRestoresOriginalAndNeverReflows) {
  ZoomMode z;
  ASSERT_TRUE(z.enter(kVector, 12));
  z.stepLarger();
  z.stepLarger();
  const auto out = z.cancel();
  EXPECT_FALSE(out.changed);
  EXPECT_EQ(out.pt, 12);
  EXPECT_FALSE(z.active());
}

TEST(ZoomModeTest, ReenterAfterExitStartsFresh) {
  ZoomMode z;
  ASSERT_TRUE(z.enter(kBuiltin, 12));
  z.stepLarger();
  z.cancel();
  ASSERT_TRUE(z.enter(kVector, 20));
  EXPECT_EQ(z.selectedPt(), 20);
  EXPECT_EQ(z.sizes().size(), kVector.size());
  EXPECT_FALSE(z.commit().changed);
}

TEST(ZoomModeTest, SingleSizeFamilyHasNothingToStep) {
  ZoomMode z;
  ASSERT_TRUE(z.enter({12}, 12));
  EXPECT_FALSE(z.stepLarger());
  EXPECT_FALSE(z.stepSmaller());
  EXPECT_FALSE(z.commit().changed);
}
