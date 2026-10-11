#include <gtest/gtest.h>

#include <map>

#include "xteink/WifiJoin.h"

using namespace xteink::wifijoin;

namespace {
struct FakePort : WifiPort {
  std::string last;
  std::map<std::string, std::string> saved;
  std::vector<ScanResult> visible;      // returned by scan()
  std::map<std::string, bool> accepts;  // ssid -> join works
  std::vector<std::string> beginCalls;
  uint32_t now = 1000;
  uint32_t scanCostMs = 2000;
  std::string pending;
  bool up = false;

  std::string lastConnectedSsid() override { return last; }
  std::optional<Credential> findCredential(const std::string& s) override {
    auto it = saved.find(s);
    if (it == saved.end()) return std::nullopt;
    return Credential{s, it->second};
  }
  size_t savedCount() override { return saved.size(); }
  std::vector<ScanResult> scan(uint32_t maxMs) override {
    now += std::min(scanCostMs, maxMs);
    return visible;
  }
  void begin(const std::string& s, const std::string&) override {
    beginCalls.push_back(s);
    pending = s;
    up = false;
  }
  bool connected() override {
    auto it = accepts.find(pending);
    if (it != accepts.end() && it->second) up = true;
    return up;
  }
  void disconnect() override { up = false; }
  void sleepMs(uint32_t ms) override { now += ms; }
  uint32_t millis() override { return now; }
};
}  // namespace

TEST(WifiJoin, LastOutOfRangeSecondSavedInRangeJoinsSecond) {
  FakePort p;
  p.last = "home";
  p.saved = {{"home", "a"}, {"office", "b"}};
  p.visible = {{"office", -60}, {"other", -40}};
  p.accepts = {{"office", true}};
  const auto r = joinAnySaved(p, 30000);
  EXPECT_TRUE(r.ok());
  EXPECT_EQ(r.ssid, "office");
  // home was tried first (and timed out), office once.
  ASSERT_EQ(p.beginCalls.size(), 2u);
  EXPECT_EQ(p.beginCalls[0], "home");
  EXPECT_EQ(p.beginCalls[1], "office");
}

TEST(WifiJoin, LastSsidJoinsImmediatelyWithoutScan) {
  FakePort p;
  p.last = "home";
  p.saved = {{"home", "a"}, {"office", "b"}};
  p.accepts = {{"home", true}};
  const auto r = joinAnySaved(p, 30000);
  EXPECT_TRUE(r.ok());
  EXPECT_EQ(r.ssid, "home");
  EXPECT_EQ(p.beginCalls.size(), 1u);
}

TEST(WifiJoin, NothingInRangeFailsWithinDeadline) {
  FakePort p;
  p.last = "home";
  p.saved = {{"home", "a"}, {"office", "b"}};
  p.visible = {{"stranger", -50}};
  const uint32_t t0 = p.now;
  const auto r = joinAnySaved(p, 20000);
  EXPECT_FALSE(r.ok());
  EXPECT_LE(p.now - t0, 20000u);
}

TEST(WifiJoin, NoSavedNetworks) {
  FakePort p;
  EXPECT_EQ(joinAnySaved(p, 5000).error, Error::NoSavedNetworks);
}

TEST(WifiJoin, NoLastSsidScanNothingSavedVisible) {
  FakePort p;
  p.saved = {{"office", "b"}};
  p.visible = {{"stranger", -50}};
  EXPECT_EQ(joinAnySaved(p, 20000).error, Error::NoneInRange);
  EXPECT_TRUE(p.beginCalls.empty());
}

TEST(WifiJoin, TriesInRssiOrderAndDoesNotRetryLast) {
  FakePort p;
  p.last = "home";
  p.saved = {{"home", "a"}, {"weak", "b"}, {"strong", "c"}};
  p.visible = {{"home", -30}, {"weak", -80}, {"strong", -50}};
  p.accepts = {{"weak", true}};
  const auto r = joinAnySaved(p, 60000);
  EXPECT_EQ(r.ssid, "weak");
  ASSERT_EQ(p.beginCalls.size(), 3u);
  EXPECT_EQ(p.beginCalls[0], "home");
  EXPECT_EQ(p.beginCalls[1], "strong");
  EXPECT_EQ(p.beginCalls[2], "weak");
}

TEST(WifiJoin, HardDeadlineCutsOffAttempts) {
  FakePort p;
  p.last = "home";
  p.saved = {{"home", "a"}, {"office", "b"}};
  p.visible = {{"office", -60}};
  const uint32_t t0 = p.now;
  const auto r = joinAnySaved(p, 5000);  // less than per-network timeout + scan
  EXPECT_EQ(r.error, Error::Timeout);
  EXPECT_LE(p.now - t0, 5000u + kPollMs);
}

TEST(WifiJoin, AllAttemptsFailReportsJoinFailed) {
  FakePort p;
  p.last = "home";
  p.saved = {{"home", "a"}, {"office", "b"}};
  p.visible = {{"office", -60}};
  EXPECT_EQ(joinAnySaved(p, 60000).error, Error::JoinFailed);
}
