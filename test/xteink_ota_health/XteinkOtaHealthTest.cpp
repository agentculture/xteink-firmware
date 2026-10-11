// Host tests for the post-update health check (src/xteink/OtaHealth.h): the
// verdict over stubbed display/SD/status checks with a fake clock, the boot
// flow (mark valid only on the pass path, rollback otherwise, nothing on a
// normal boot), the rolled_back_version bookkeeping, and the optional
// rolled_back_version field in the status bodies (SyncProtocol).
#include <ArduinoJson.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "xteink/OtaHealth.h"
#include "xteink/SyncProtocol.h"

using namespace xteink::otahealth;

namespace {

constexpr uint32_t kWindow = 10000;
constexpr uint32_t kDelay = 1000;

class FakePort : public HealthPort {
 public:
  // Inputs.
  bool pending = true;
  bool display = true;
  bool sd = true;
  // Result of each postStatus() call in order; past the end -> postDefault.
  std::vector<bool> posts;
  bool postDefault = false;
  uint32_t postCostMs = 500;  // fake time one attempt takes
  bool invalidPartition = false;
  std::string version = "xteink-fw 1.2.0-new";

  // Observations.
  uint32_t now = 0xFFFFF000u;  // start near the wrap on purpose
  int postCalls = 0;
  int sleepCalls = 0;
  int markValidCalls = 0;
  int rollbackCalls = 0;
  std::vector<uint32_t> postBudgets;
  std::string cand;
  std::string rolledBack;
  std::string rolledBackList;  // NVS rb_list: never cleared by a status POST
  std::vector<std::string> events;

  bool pendingVerify() override { return pending; }
  bool displayOk() override { return display; }
  bool sdOk() override { return sd; }
  bool postStatus(const uint32_t remainingMs) override {
    postBudgets.push_back(remainingMs);
    const bool ok = postCalls < static_cast<int>(posts.size()) ? posts[postCalls] : postDefault;
    ++postCalls;
    now += postCostMs;
    events.push_back("post");
    return ok;
  }
  uint32_t millis() override { return now; }
  void sleepMs(const uint32_t ms) override {
    ++sleepCalls;
    now += ms;
  }
  void markValid() override {
    ++markValidCalls;
    events.push_back("valid");
  }
  void markInvalidAndReboot() override {
    ++rollbackCalls;
    events.push_back("rollback");
  }
  std::string runningVersion() override { return version; }
  bool hasInvalidPartition() override { return invalidPartition; }
  std::string candidate() override { return cand; }
  void setCandidate(const std::string& v) override {
    cand = v;
    events.push_back("cand:" + v);
  }
  void clearCandidate() override {
    cand.clear();
    events.push_back("cand-clear");
  }
  void setRolledBack(const std::string& v) override { rolledBack = v; }
  void rememberRolledBack(const std::string& v) override { rolledBackList = xteink::sync::rolledBackListAdd(rolledBackList, v); }
};

// --- decideHealth ---------------------------------------------------------

TEST(OtaHealthDecide, AllPassSelectsValid) {
  FakePort p;
  p.posts = {true};
  const Decision d = decideHealth(p, kWindow, kDelay);
  EXPECT_EQ(d.verdict, Verdict::Valid);
  EXPECT_EQ(d.reason, Reason::None);
  EXPECT_EQ(d.attempts, 1);
  EXPECT_EQ(p.sleepCalls, 0);
  ASSERT_EQ(p.postBudgets.size(), 1u);
  EXPECT_EQ(p.postBudgets[0], kWindow);
}

TEST(OtaHealthDecide, DisplayFailureSelectsRollbackWithoutNetwork) {
  FakePort p;
  p.display = false;
  p.postDefault = true;
  const Decision d = decideHealth(p, kWindow, kDelay);
  EXPECT_EQ(d.verdict, Verdict::Rollback);
  EXPECT_EQ(d.reason, Reason::Display);
  EXPECT_EQ(p.postCalls, 0);
}

TEST(OtaHealthDecide, SdFailureSelectsRollbackWithoutNetwork) {
  FakePort p;
  p.sd = false;
  p.postDefault = true;
  const Decision d = decideHealth(p, kWindow, kDelay);
  EXPECT_EQ(d.verdict, Verdict::Rollback);
  EXPECT_EQ(d.reason, Reason::SdCard);
  EXPECT_EQ(p.postCalls, 0);
}

TEST(OtaHealthDecide, RetriesUntilServerAnswersInsideWindow) {
  FakePort p;
  p.posts = {false, false, true};
  const Decision d = decideHealth(p, kWindow, kDelay);
  EXPECT_EQ(d.verdict, Verdict::Valid);
  EXPECT_EQ(d.attempts, 3);
  EXPECT_EQ(p.sleepCalls, 2);
  // Each attempt gets what is left of the window.
  ASSERT_EQ(p.postBudgets.size(), 3u);
  EXPECT_EQ(p.postBudgets[0], kWindow);
  EXPECT_EQ(p.postBudgets[1], kWindow - 500 - kDelay);
  EXPECT_EQ(p.postBudgets[2], kWindow - 2 * (500 + kDelay));
}

TEST(OtaHealthDecide, ServerUnreachablePastWindowSelectsRollback) {
  FakePort p;
  p.postDefault = false;
  const Decision d = decideHealth(p, kWindow, kDelay);
  EXPECT_EQ(d.verdict, Verdict::Rollback);
  EXPECT_EQ(d.reason, Reason::Server);
  // 500 ms attempt + 1000 ms pause per round inside 10 s: 7 attempts start
  // (at 0, 1.5, 3, 4.5, 6, 7.5, 9 s); none starts once the window is spent.
  EXPECT_EQ(d.attempts, 7);
  EXPECT_EQ(p.postCalls, 7);
}

TEST(OtaHealthDecide, ServerAnswerAfterWindowIsNotUsed) {
  FakePort p;
  // A single attempt eats the whole window and fails; the success queued after
  // it is never reached.
  p.postCostMs = kWindow;
  p.posts = {false, true};
  const Decision d = decideHealth(p, kWindow, kDelay);
  EXPECT_EQ(d.verdict, Verdict::Rollback);
  EXPECT_EQ(d.reason, Reason::Server);
  EXPECT_EQ(p.postCalls, 1);
  EXPECT_EQ(p.sleepCalls, 0);
}

TEST(OtaHealthDecide, ZeroWindowSelectsRollback) {
  FakePort p;
  p.postDefault = true;
  const Decision d = decideHealth(p, 0, kDelay);
  EXPECT_EQ(d.verdict, Verdict::Rollback);
  EXPECT_EQ(d.reason, Reason::Server);
  EXPECT_EQ(p.postCalls, 0);
}

TEST(OtaHealthDecide, WindowConstantIsShortButAllowsAJoin) {
  // A Wi-Fi join (8 s per network) plus a LAN probe and a tunnel attempt fit,
  // and a dead server cannot hold boot for more than a minute.
  EXPECT_GE(kHealthServerWindowMs, 30000u);
  EXPECT_LE(kHealthServerWindowMs, 60000u);
  EXPECT_LT(kHealthRetryDelayMs, kHealthServerWindowMs);
}

// --- runOnBoot ------------------------------------------------------------

TEST(OtaHealthBoot, NormalBootDoesNothing) {
  FakePort p;
  p.pending = false;
  EXPECT_EQ(runOnBoot(p, kWindow, kDelay), BootOutcome::NotPending);
  EXPECT_EQ(p.postCalls, 0);
  EXPECT_EQ(p.sleepCalls, 0);
  EXPECT_EQ(p.markValidCalls, 0);
  EXPECT_EQ(p.rollbackCalls, 0);
  EXPECT_TRUE(p.events.empty());
}

TEST(OtaHealthBoot, PassMarksValidOnlyOnce) {
  FakePort p;
  p.posts = {false, true};
  Decision d;
  EXPECT_EQ(runOnBoot(p, kWindow, kDelay, &d), BootOutcome::Valid);
  EXPECT_EQ(d.verdict, Verdict::Valid);
  EXPECT_EQ(p.markValidCalls, 1);
  EXPECT_EQ(p.rollbackCalls, 0);
  EXPECT_TRUE(p.cand.empty());
  // Candidate recorded before the checks, cleared before the image is marked valid.
  const std::vector<std::string> want = {"cand:xteink-fw 1.2.0-new", "post", "post", "cand-clear", "valid"};
  EXPECT_EQ(p.events, want);
}

TEST(OtaHealthBoot, ServerUnreachableRollsBackAndNeverMarksValid) {
  FakePort p;
  p.postDefault = false;
  EXPECT_EQ(runOnBoot(p, kWindow, kDelay), BootOutcome::RolledBack);
  EXPECT_EQ(p.markValidCalls, 0);
  EXPECT_EQ(p.rollbackCalls, 1);
  // The candidate survives the reboot so the previous image can report it.
  EXPECT_EQ(p.cand, "xteink-fw 1.2.0-new");
}

TEST(OtaHealthBoot, DisplayFailureRollsBack) {
  FakePort p;
  p.display = false;
  p.postDefault = true;
  EXPECT_EQ(runOnBoot(p, kWindow, kDelay), BootOutcome::RolledBack);
  EXPECT_EQ(p.markValidCalls, 0);
  EXPECT_EQ(p.rollbackCalls, 1);
  EXPECT_EQ(p.postCalls, 0);
}

TEST(OtaHealthBoot, SdFailureRollsBack) {
  FakePort p;
  p.sd = false;
  p.postDefault = true;
  EXPECT_EQ(runOnBoot(p, kWindow, kDelay), BootOutcome::RolledBack);
  EXPECT_EQ(p.markValidCalls, 0);
  EXPECT_EQ(p.rollbackCalls, 1);
  EXPECT_EQ(p.postCalls, 0);
}

TEST(OtaHealthBoot, PreviousImageRecordsTheRolledBackVersion) {
  // Boot 1: the candidate fails.
  FakePort p;
  p.postDefault = false;
  ASSERT_EQ(runOnBoot(p, kWindow, kDelay), BootOutcome::RolledBack);
  // Boot 2: the bootloader runs the previous image; the candidate's slot is invalid.
  p.pending = false;
  p.invalidPartition = true;
  p.version = "xteink-fw 1.1.0-old";
  p.events.clear();
  p.postCalls = 0;
  EXPECT_EQ(runOnBoot(p, kWindow, kDelay), BootOutcome::NotPending);
  EXPECT_EQ(p.rolledBack, "xteink-fw 1.2.0-new");
  EXPECT_TRUE(p.cand.empty());
  EXPECT_EQ(p.postCalls, 0);
  EXPECT_EQ(p.markValidCalls, 0);
  // Boot 3: nothing left to record.
  p.rolledBack.clear();
  EXPECT_EQ(runOnBoot(p, kWindow, kDelay), BootOutcome::NotPending);
  EXPECT_TRUE(p.rolledBack.empty());
}

TEST(OtaHealthBoot, ACrashBeforeTheHealthCheckIsStillReported) {
  // The installer wrote rb_cand before switching the boot partition (the
  // offered build, in whatever form the server named it). The new image
  // crashed before onBoot(); the bootloader aborted it and booted the old one.
  FakePort p;
  p.pending = false;
  p.invalidPartition = true;
  p.version = "xteink-fw 1.1.0-old";
  p.cand = xteink::sync::firmwareReportVersion("1.2.0-new");
  EXPECT_EQ(runOnBoot(p, kWindow, kDelay), BootOutcome::NotPending);
  EXPECT_EQ(p.rolledBack, "xteink-fw 1.2.0-new");
  EXPECT_TRUE(xteink::sync::rolledBackListContains(p.rolledBackList, "1.2.0-new"));
  EXPECT_TRUE(p.cand.empty());
}

TEST(OtaHealthBoot, TheRolledBackRecordOutlivesTheReport) {
  FakePort p;
  p.postDefault = false;
  ASSERT_EQ(runOnBoot(p, kWindow, kDelay), BootOutcome::RolledBack);
  p.pending = false;
  p.invalidPartition = true;
  p.version = "xteink-fw 1.1.0-old";
  runOnBoot(p, kWindow, kDelay);
  ASSERT_EQ(p.rolledBack, "xteink-fw 1.2.0-new");
  // The server acknowledged rolled_back_version (2xx clears rb_version) ...
  p.rolledBack.clear();
  runOnBoot(p, kWindow, kDelay);
  // ... but the device still refuses that build when it is offered again.
  xteink::sync::FirmwareOffer offer;
  offer.present = true;
  offer.version = "1.2.0-new";
  EXPECT_EQ(xteink::sync::decideOta(offer, "1.1.0-old", false, false, p.rolledBackList),
            xteink::sync::OtaDecision::RolledBack);
}

TEST(OtaHealthBoot, PendingBootKeepsTheInstallerCandidateWithoutRewritingIt) {
  FakePort p;
  p.cand = p.version;  // written by the installer
  p.posts = {true};
  EXPECT_EQ(runOnBoot(p, kWindow, kDelay), BootOutcome::Valid);
  EXPECT_EQ(p.events, (std::vector<std::string>{"post", "cand-clear", "valid"}));
}

TEST(OtaHealthBoot, StaleCandidateWithoutInvalidPartitionIsNotReported) {
  FakePort p;
  p.pending = false;
  p.cand = "xteink-fw 1.2.0-new";
  p.invalidPartition = false;
  runOnBoot(p, kWindow, kDelay);
  EXPECT_TRUE(p.rolledBack.empty());
  EXPECT_TRUE(p.cand.empty());
}

TEST(OtaHealthBoot, CandidateNamingTheRunningImageIsNotReported) {
  // The image passed but clearing the candidate failed; an older rollback
  // left a partition invalid. The running image is not a rollback.
  FakePort p;
  p.pending = false;
  p.cand = p.version;
  p.invalidPartition = true;
  runOnBoot(p, kWindow, kDelay);
  EXPECT_TRUE(p.rolledBack.empty());
  EXPECT_TRUE(p.cand.empty());
}

TEST(OtaHealth, ReasonNames) {
  EXPECT_STREQ(reasonName(Reason::None), "ok");
  EXPECT_STREQ(reasonName(Reason::Display), "display");
  EXPECT_STREQ(reasonName(Reason::SdCard), "sd");
  EXPECT_STREQ(reasonName(Reason::Server), "server");
}

// --- rolled_back_version in the status bodies ----------------------------

JsonDocument parse(const std::string& s) {
  JsonDocument doc;
  EXPECT_FALSE(deserializeJson(doc, s));
  return doc;
}

TEST(OtaHealthStatusJson, FullReportOmitsFieldWhenUnset) {
  xteink::sync::StatusReport r;
  r.firmwareVersion = "xteink-fw 1.1.0-old";
  const JsonDocument doc = parse(xteink::sync::buildStatusJson(r));
  EXPECT_FALSE(doc["rolled_back_version"].is<JsonVariantConst>());
}

TEST(OtaHealthStatusJson, FullReportCarriesFieldWhenSet) {
  xteink::sync::StatusReport r;
  r.firmwareVersion = "xteink-fw 1.1.0-old";
  r.rolledBackVersion = "xteink-fw 1.2.0-new";
  const JsonDocument doc = parse(xteink::sync::buildStatusJson(r));
  EXPECT_EQ(doc["rolled_back_version"].as<std::string>(), "xteink-fw 1.2.0-new");
  EXPECT_EQ(doc["firmware_version"].as<std::string>(), "xteink-fw 1.1.0-old");
}

TEST(OtaHealthStatusJson, FirmwareCheckOmitsFieldWhenUnset) {
  const JsonDocument doc = parse(xteink::sync::buildFirmwareCheckJson("xteink-fw 1.1.0-old"));
  EXPECT_FALSE(doc["rolled_back_version"].is<JsonVariantConst>());
  EXPECT_EQ(doc.as<JsonObjectConst>().size(), 1u);
}

TEST(OtaHealthStatusJson, FirmwareCheckCarriesFieldWhenSet) {
  const JsonDocument doc = parse(xteink::sync::buildFirmwareCheckJson("xteink-fw 1.1.0-old", "xteink-fw 1.2.0-new"));
  EXPECT_EQ(doc["rolled_back_version"].as<std::string>(), "xteink-fw 1.2.0-new");
  EXPECT_EQ(doc["firmware_version"].as<std::string>(), "xteink-fw 1.1.0-old");
}

}  // namespace
