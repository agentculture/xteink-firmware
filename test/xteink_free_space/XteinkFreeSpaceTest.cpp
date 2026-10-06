#include <gtest/gtest.h>

#include "src/xteink/FreeSpaceCache.h"

using namespace xteink::freespace;

namespace {
constexpr int64_t kNow = 1790000000;  // a trusted epoch in 2026
constexpr int64_t kGiB = 1024LL * 1024 * 1024;
}  // namespace

TEST(XteinkFreeSpaceTest, NoCacheMeansRealQuery) { EXPECT_FALSE(usable(Cache{}, kNow, 0)); }

TEST(XteinkFreeSpaceTest, FreshRoomyCacheIsReused) {
  const Cache c = measured(10 * kGiB, kNow - 3600);
  EXPECT_TRUE(usable(c, kNow, 0));
  EXPECT_TRUE(usable(c, kNow, 500LL * 1024 * 1024));
}

TEST(XteinkFreeSpaceTest, OlderThanADayIsRequeried) {
  EXPECT_TRUE(usable(measured(10 * kGiB, kNow - MAX_AGE_S + 1), kNow, 0));
  EXPECT_FALSE(usable(measured(10 * kGiB, kNow - MAX_AGE_S), kNow, 0));
}

TEST(XteinkFreeSpaceTest, UnknownOrBackwardClockIsRequeried) {
  const Cache c = measured(10 * kGiB, kNow);
  EXPECT_FALSE(usable(c, 0, 0));
  EXPECT_FALSE(usable(c, kNow - 10, 0));
  EXPECT_EQ(measured(10 * kGiB, 0).freeBytes, -1);
}

TEST(XteinkFreeSpaceTest, TightSpaceAfterQueueIsRequeried) {
  const Cache c = measured(TIGHT_BYTES + 100, kNow);
  EXPECT_TRUE(usable(c, kNow, 100));
  EXPECT_FALSE(usable(c, kNow, 101));
  EXPECT_FALSE(usable(measured(TIGHT_BYTES - 1, kNow), kNow, 0));
}

TEST(XteinkFreeSpaceTest, FailedQueryLeavesNoCache) {
  EXPECT_EQ(measured(0, kNow).freeBytes, -1);
  EXPECT_EQ(measured(-1, kNow).freeBytes, -1);
}

TEST(XteinkFreeSpaceTest, WritesReduceTheEstimateAndKeepTheStamp) {
  const Cache c = afterWrites(measured(1 * kGiB, kNow - 50), 300LL * 1024 * 1024);
  EXPECT_EQ(c.freeBytes, 1 * kGiB - 300LL * 1024 * 1024);
  EXPECT_EQ(c.measuredAt, kNow - 50);
  EXPECT_EQ(afterWrites(measured(100, kNow), 1000).freeBytes, 0);
  EXPECT_EQ(afterWrites(Cache{}, 1000).freeBytes, -1);
  EXPECT_EQ(afterWrites(measured(100, kNow), -5).freeBytes, 100);
}

TEST(XteinkFreeSpaceTest, RepeatedSyncsDriftDownUntilTightThenRequery) {
  Cache c = measured(TIGHT_BYTES + 30LL * 1024 * 1024, kNow);
  EXPECT_TRUE(usable(c, kNow, 0));
  c = afterWrites(c, 20LL * 1024 * 1024);
  EXPECT_TRUE(usable(c, kNow, 0));
  c = afterWrites(c, 20LL * 1024 * 1024);
  EXPECT_FALSE(usable(c, kNow, 0));
}
