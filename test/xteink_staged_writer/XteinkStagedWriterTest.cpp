#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <utility>
#include <vector>

#include "src/xteink/StagedWriter.h"

using namespace xteink::staged;

namespace {

// A file that records every write and can be told to fail chosen attempts.
// Like SdFat, a failed write may already have changed bytes past `offset`.
struct FakeFile {
  std::vector<uint8_t> bytes;
  std::vector<std::pair<size_t, size_t>> writes;  // (offset, len) per attempt
  std::vector<bool> failPlan;                     // attempt index -> fail
  std::vector<uint32_t> pauses;

  Io io() {
    return Io{[this](const size_t offset, const uint8_t* data, const size_t len) {
                const size_t n = writes.size();
                writes.emplace_back(offset, len);
                if (offset > bytes.size()) return false;  // a seek past the end fails
                bytes.resize(offset);                     // reposition (and drop any torn write)
                const bool fail = n < failPlan.size() && failPlan[n];
                // A failing write leaves half a block of garbage behind.
                if (fail) {
                  bytes.insert(bytes.end(), len / 2, 0xEE);
                  return false;
                }
                bytes.insert(bytes.end(), data, data + len);
                return true;
              },
              [this](const uint32_t ms) { pauses.push_back(ms); }};
  }
};

std::vector<uint8_t> stream(const size_t n) {
  std::vector<uint8_t> v(n);
  std::iota(v.begin(), v.end(), static_cast<uint8_t>(1));
  return v;
}

// Feeds `data` in the irregular chunk sizes a socket delivers.
bool feed(Writer& w, const std::vector<uint8_t>& data, const std::vector<size_t>& chunks) {
  size_t pos = 0;
  size_t i = 0;
  while (pos < data.size()) {
    const size_t n = std::min(chunks[i++ % chunks.size()], data.size() - pos);
    if (!w.append(data.data() + pos, n)) return false;
    pos += n;
  }
  return true;
}

}  // namespace

TEST(XteinkStagedWriterTest, WritesOnlyWholeAlignedBlocksThenTheTail) {
  FakeFile f;
  std::vector<uint8_t> buf(4096);
  Writer w(buf.data(), buf.size(), f.io());
  const auto data = stream(3 * 4096 + 777);
  ASSERT_TRUE(feed(w, data, {1436, 2048, 13, 1460, 511}));
  // Before finish(): only full blocks, each at a block-aligned offset.
  ASSERT_EQ(f.writes.size(), 3u);
  for (size_t i = 0; i < f.writes.size(); ++i) {
    EXPECT_EQ(f.writes[i].first, i * 4096);
    EXPECT_EQ(f.writes[i].second, 4096u);
  }
  EXPECT_EQ(w.durable(), 3u * 4096);
  EXPECT_EQ(w.accepted(), data.size());
  ASSERT_TRUE(w.finish());
  ASSERT_EQ(f.writes.size(), 4u);
  EXPECT_EQ(f.writes.back().first, 3u * 4096);
  EXPECT_EQ(f.writes.back().second, 777u);
  EXPECT_EQ(f.bytes, data);
  EXPECT_EQ(w.retries(), 0u);
  EXPECT_TRUE(f.pauses.empty());
}

TEST(XteinkStagedWriterTest, ExactMultipleHasNoTailWrite) {
  FakeFile f;
  std::vector<uint8_t> buf(1024);
  Writer w(buf.data(), buf.size(), f.io());
  const auto data = stream(4 * 1024);
  ASSERT_TRUE(feed(w, data, {300}));
  ASSERT_TRUE(w.finish());
  EXPECT_EQ(f.writes.size(), 4u);
  EXPECT_EQ(f.bytes, data);
}

TEST(XteinkStagedWriterTest, FailedBlockIsRewrittenFromItsOwnOffset) {
  FakeFile f;
  f.failPlan = {false, true, true};  // block 2 fails twice, then lands
  std::vector<uint8_t> buf(512);
  Writer w(buf.data(), buf.size(), f.io());
  const auto data = stream(3 * 512);
  ASSERT_TRUE(feed(w, data, {1460}));
  ASSERT_TRUE(w.finish());
  EXPECT_EQ(f.bytes, data);  // the torn half-blocks were overwritten
  EXPECT_EQ(w.retries(), 2u);
  ASSERT_GE(f.writes.size(), 4u);
  EXPECT_EQ(f.writes[1].first, 512u);
  EXPECT_EQ(f.writes[2].first, 512u);
  EXPECT_EQ(f.writes[3].first, 512u);
  EXPECT_EQ(f.pauses, (std::vector<uint32_t>{kRetryDelaysMs[0], kRetryDelaysMs[1]}));
}

TEST(XteinkStagedWriterTest, GivesUpAfterEveryAttemptFails) {
  FakeFile f;
  f.failPlan = std::vector<bool>(1 + kMaxAttempts, true);
  f.failPlan[0] = false;
  std::vector<uint8_t> buf(512);
  Writer w(buf.data(), buf.size(), f.io());
  const auto data = stream(2 * 512);
  EXPECT_FALSE(feed(w, data, {512}));
  EXPECT_TRUE(w.failed());
  EXPECT_EQ(w.durable(), 512u);
  EXPECT_EQ(f.writes.size(), 1u + kMaxAttempts);
  EXPECT_EQ(w.retries(), static_cast<uint32_t>(kMaxAttempts - 1));
  // Total pause stays well inside the 30 s HTTP stall timeout.
  uint32_t total = 0;
  for (const uint32_t ms : f.pauses) total += ms;
  EXPECT_LT(total, 5000u);
  // A failed writer refuses more data and does not flush.
  const uint8_t more[4] = {1, 2, 3, 4};
  EXPECT_FALSE(w.append(more, sizeof(more)));
  EXPECT_FALSE(w.finish());
  EXPECT_EQ(f.writes.size(), 1u + kMaxAttempts);
}

TEST(XteinkStagedWriterTest, FailedTailIsRetriedToo) {
  FakeFile f;
  f.failPlan = {false, true};
  std::vector<uint8_t> buf(512);
  Writer w(buf.data(), buf.size(), f.io());
  const auto data = stream(512 + 100);
  ASSERT_TRUE(feed(w, data, {612}));
  ASSERT_TRUE(w.finish());
  EXPECT_EQ(f.bytes, data);
  EXPECT_EQ(w.retries(), 1u);
}

TEST(XteinkStagedWriterTest, RestartDropsBufferedBytesAndStartsAtZero) {
  FakeFile f;
  std::vector<uint8_t> buf(512);
  Writer w(buf.data(), buf.size(), f.io());
  const auto first = stream(700);
  ASSERT_TRUE(feed(w, first, {700}));
  // The caller truncates the file when the server ignores Range.
  f.bytes.clear();
  w.restart();
  EXPECT_EQ(w.accepted(), 0u);
  EXPECT_EQ(w.durable(), 0u);
  const auto second = stream(1000);
  ASSERT_TRUE(feed(w, second, {333}));
  ASSERT_TRUE(w.finish());
  EXPECT_EQ(f.bytes, second);
  EXPECT_EQ(f.writes[1].first, 0u);
}

TEST(XteinkStagedWriterTest, EmptyStreamWritesNothing) {
  FakeFile f;
  std::vector<uint8_t> buf(512);
  Writer w(buf.data(), buf.size(), f.io());
  EXPECT_TRUE(w.finish());
  EXPECT_TRUE(f.writes.empty());
}

TEST(XteinkStagedWriterTest, NoBufferRefusesData) {
  FakeFile f;
  Writer w(nullptr, 0, f.io());
  const uint8_t b = 7;
  EXPECT_FALSE(w.append(&b, 1));
  EXPECT_TRUE(f.writes.empty());
}
