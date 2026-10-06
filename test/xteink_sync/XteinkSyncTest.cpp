// Host tests for the xteink sync client's protocol logic. Expectations follow
// the xteink server's reference device (tests/server/fake_device.py) and
// docs/device-protocol.md v1.
#include <ArduinoJson.h>
#include <gtest/gtest.h>

#include <string>

#include "network/SntpPolicy.h"
#include "util/StringUtils.h"
#include "xteink/SyncProtocol.h"

using namespace xteink::sync;

namespace {
const std::string kShaA(64, 'a');
const std::string kShaB(64, 'b');
const std::string kShaC = "9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08";

JsonDocument parse(const std::string& s) {
  JsonDocument doc;
  EXPECT_EQ(deserializeJson(doc, s), DeserializationError::Ok) << s;
  return doc;
}

bool parseQ(const std::string& s, Queue& q) { return parseQueue(s.data(), s.size(), q); }
}  // namespace

// --- status report -------------------------------------------------------

TEST(XteinkSyncStatus, MatchesFakeDeviceShape) {
  StatusReport r;
  r.freeSdBytes = 734003200;
  r.firmwareVersion = "xteink-fw 0.1.0";
  r.inventory.push_back({kShaC, true});
  r.inventory.push_back({kShaA, false});
  const std::string json = buildStatusJson(r);
  const JsonDocument doc = parse(json);
  EXPECT_EQ(doc["free_sd_bytes"].as<int64_t>(), 734003200);
  EXPECT_EQ(doc["firmware_version"].as<std::string>(), "xteink-fw 0.1.0");
  // fake_device sends null for an unset last_error / last_sync_result.
  EXPECT_TRUE(doc["last_error"].isNull());
  EXPECT_TRUE(doc["last_sync_result"].isNull());
  EXPECT_NE(json.find("\"last_error\":null"), std::string::npos);
  EXPECT_NE(json.find("\"last_sync_result\":null"), std::string::npos);
  ASSERT_EQ(doc["inventory"].size(), 2u);
  EXPECT_EQ(doc["inventory"][0]["sha256"].as<std::string>(), kShaC);
  EXPECT_TRUE(doc["inventory"][0]["unmodified"].as<bool>());
  EXPECT_FALSE(doc["inventory"][1]["unmodified"].as<bool>());
}

TEST(XteinkSyncStatus, FinalReportCarriesResultAndError) {
  StatusReport r;
  r.firmwareVersion = "v";
  r.lastSyncResult = "ok";
  r.lastError = "ESHA item 42";
  const JsonDocument doc = parse(buildStatusJson(r));
  EXPECT_EQ(doc["last_sync_result"].as<std::string>(), "ok");
  EXPECT_EQ(doc["last_error"].as<std::string>(), "ESHA item 42");
  // Unknown free space is omitted, never sent negative (server: ge=0).
  EXPECT_FALSE(doc.as<JsonObjectConst>()["free_sd_bytes"].is<int64_t>());
  // An empty inventory is still sent: it replaces the previous one.
  EXPECT_TRUE(doc["inventory"].is<JsonArrayConst>());
  EXPECT_EQ(doc["inventory"].size(), 0u);
}

TEST(XteinkSyncAck, Body) {
  const JsonDocument doc = parse(buildAckJson(42, kShaC));
  EXPECT_EQ(doc["item_id"].as<int64_t>(), 42);
  EXPECT_EQ(doc["sha256"].as<std::string>(), kShaC);
}

// --- queue ---------------------------------------------------------------

TEST(XteinkSyncQueue, ParsesDocExample) {
  const std::string body =
      R"({"protocol":"1","items":[{"id":42,"title":"Dune","size":812345,"sha256":")" + kShaC +
      R"(","format":"epub","url":"/api/device/items/42"}],"skipped":[{"id":43,"reason":"sd_full"}],)"
      R"("deletes":[{"sha256":")" +
      kShaA + R"("}]})";
  Queue q;
  ASSERT_TRUE(parseQ(body, q));
  ASSERT_EQ(q.items.size(), 1u);
  EXPECT_EQ(q.items[0].id, 42);
  EXPECT_EQ(q.items[0].title, "Dune");
  EXPECT_EQ(q.items[0].size, 812345);
  EXPECT_EQ(q.items[0].sha256, kShaC);
  EXPECT_EQ(q.items[0].format, "epub");
  EXPECT_EQ(q.items[0].url, "/api/device/items/42");
  ASSERT_EQ(q.skipped.size(), 1u);
  EXPECT_EQ(q.skipped[0].id, 43);
  EXPECT_EQ(q.skipped[0].reason, "sd_full");
  ASSERT_EQ(q.deletes.size(), 1u);
  EXPECT_EQ(q.deletes[0], kShaA);
  EXPECT_EQ(q.droppedItems, 0u);
}

TEST(XteinkSyncQueue, EmptyQueue) {
  Queue q;
  ASSERT_TRUE(parseQ(R"({"protocol":"1","items":[],"skipped":[],"deletes":[]})", q));
  EXPECT_TRUE(q.items.empty());
  EXPECT_TRUE(q.skipped.empty());
  EXPECT_TRUE(q.deletes.empty());
}

TEST(XteinkSyncQueue, RejectsOtherProtocolAndGarbage) {
  Queue q;
  EXPECT_FALSE(parseQ(R"({"protocol":"2","items":[]})", q));
  EXPECT_FALSE(parseQ(R"({"detail":"nope"})", q));
  EXPECT_FALSE(parseQ("not json", q));
  EXPECT_FALSE(parseQ("[]", q));
}

TEST(XteinkSyncQueue, DropsUnsafeOrMalformedItems) {
  const std::string ok =
      R"({"id":1,"size":10,"sha256":")" + kShaA + R"(","format":"epub","url":"/api/device/items/1"})";
  const std::string body = R"({"protocol":"1","items":[)" + ok +
                           // absolute URL to another host: the key must never follow it
                           R"(,{"id":2,"size":10,"sha256":")" + kShaA + R"(","url":"https://evil.example/x"})" +
                           // protocol-relative URL
                           R"(,{"id":3,"size":10,"sha256":")" + kShaA + R"(","url":"//evil.example/x"})" +
                           // uppercase / short sha
                           R"(,{"id":4,"size":10,"sha256":"ABC","url":"/api/device/items/4"})" +
                           // negative size
                           R"(,{"id":5,"size":-1,"sha256":")" + kShaA + R"(","url":"/api/device/items/5"})" +
                           // missing id
                           R"(,{"size":10,"sha256":")" + kShaA + R"(","url":"/api/device/items/6"})" +
                           R"(],"skipped":[],"deletes":[{"sha256":"zz"}]})";
  Queue q;
  ASSERT_TRUE(parseQ(body, q));
  ASSERT_EQ(q.items.size(), 1u);
  EXPECT_EQ(q.items[0].id, 1);
  EXPECT_EQ(q.droppedItems, 5u);
  EXPECT_TRUE(q.deletes.empty());
}

TEST(XteinkSyncQueue, CapsItemsPerSync) {
  std::string body = R"({"protocol":"1","items":[)";
  for (size_t i = 0; i < kMaxItems + 3; ++i) {
    if (i) body += ",";
    body += R"({"id":)" + std::to_string(i) + R"(,"size":1,"sha256":")" + kShaA + R"(","url":"/api/device/items/)" +
            std::to_string(i) + R"("})";
  }
  body += "]}";
  Queue q;
  ASSERT_TRUE(parseQ(body, q));
  EXPECT_EQ(q.items.size(), kMaxItems);
  EXPECT_EQ(q.droppedItems, 3u);
  EXPECT_EQ(q.items.front().id, 0);  // queue order kept
}

TEST(XteinkSyncQueue, RootRelativePath) {
  EXPECT_TRUE(isRootRelativePath("/api/device/items/42"));
  EXPECT_FALSE(isRootRelativePath("api/device/items/42"));
  EXPECT_FALSE(isRootRelativePath("//host/x"));
  EXPECT_FALSE(isRootRelativePath("/\\host"));
  EXPECT_FALSE(isRootRelativePath("http://host/x"));
  EXPECT_FALSE(isRootRelativePath("/a b"));
  EXPECT_FALSE(isRootRelativePath("/"));
}

// --- server order (decision c60) -------------------------------------------

TEST(XteinkSyncOrder, DefaultsWhenUnprovisioned) {
  const auto o = serverOrder("", "");
  ASSERT_EQ(o.size(), 2u);
  EXPECT_EQ(o[0], "http://xteink.local:8781");
  EXPECT_EQ(o[1], "https://xteink.culture.dev");
}

TEST(XteinkSyncOrder, ProvisionedLanFirstThenTunnel) {
  const auto o = serverOrder("http://192.168.1.20:8781/", "https://Books.Example.org/");
  ASSERT_EQ(o.size(), 2u);
  EXPECT_EQ(o[0], "http://192.168.1.20:8781");
  EXPECT_EQ(o[1], "https://books.example.org");
}

TEST(XteinkSyncOrder, TunnelMustBeHttps) {
  const auto o = serverOrder("http://10.0.0.2:8781", "http://xteink.culture.dev");
  ASSERT_EQ(o.size(), 1u);
  EXPECT_EQ(o[0], "http://10.0.0.2:8781");
}

TEST(XteinkSyncOrder, DropsBadAndDuplicateEntries) {
  EXPECT_EQ(serverOrder("ftp://x", "").size(), 1u);
  const auto dup = serverOrder("https://xteink.culture.dev:443", "https://xteink.culture.dev");
  ASSERT_EQ(dup.size(), 1u);
  EXPECT_EQ(dup[0], "https://xteink.culture.dev");
}

TEST(XteinkSyncOrder, NormalizeOrigin) {
  EXPECT_EQ(normalizeOrigin("HTTP://Host:80/path?q"), "http://host");
  EXPECT_EQ(normalizeOrigin("https://h:8443"), "https://h:8443");
  EXPECT_EQ(normalizeOrigin("https://user:pw@h"), "");
  EXPECT_EQ(normalizeOrigin("https://h:99999"), "");
  EXPECT_EQ(normalizeOrigin("https://h:"), "");
  EXPECT_EQ(normalizeOrigin("h:8781"), "");
  EXPECT_TRUE(isHttps("https://h"));
  EXPECT_FALSE(isHttps("http://h"));
}

// --- error codes ----------------------------------------------------------

TEST(XteinkSyncErrors, StatusMapping) {
  EXPECT_EQ(errorForStatus(200), Error::None);
  EXPECT_EQ(errorForStatus(206), Error::None);
  EXPECT_EQ(errorForStatus(-1), Error::Network);
  EXPECT_EQ(errorForStatus(401), Error::Unauthorized);
  EXPECT_EQ(errorForStatus(426), Error::Protocol);
  EXPECT_EQ(errorForStatus(422), Error::BadRequest);
  EXPECT_EQ(errorForStatus(500), Error::Http);
  EXPECT_EQ(errorForStatus(302), Error::Http);  // redirects are never followed with the key
  EXPECT_STREQ(errorCode(Error::Unauthorized), "E401");
  EXPECT_STREQ(errorCode(Error::Protocol), "E426");
  EXPECT_STREQ(errorCode(Error::LowMemory), "ELOWMEM");
}

TEST(XteinkSyncErrors, FatalStopsSync) {
  EXPECT_TRUE(isFatal(Error::Unauthorized));  // doc: stop syncing, re-pair
  EXPECT_TRUE(isFatal(Error::Protocol));      // doc: firmware update needed
  EXPECT_TRUE(isFatal(Error::Network));
  EXPECT_FALSE(isFatal(Error::Hash));  // one bad item does not stop the rest
  EXPECT_FALSE(isFatal(Error::Http));
}

// --- manifest and deletes -------------------------------------------------

TEST(XteinkSyncManifest, RoundTrip) {
  Manifest m;
  ASSERT_TRUE(m.add({kShaA, "/xteink/Dune.epub", 42}));
  ASSERT_TRUE(m.add({kShaB, "/xteink/Emma.epub", 43}));
  const std::string json = buildManifestJson(m);
  Manifest back;
  ASSERT_TRUE(parseManifest(json.data(), json.size(), back));
  ASSERT_EQ(back.files.size(), 2u);
  EXPECT_EQ(back.files[1].path, "/xteink/Emma.epub");
  EXPECT_EQ(back.files[1].itemId, 43);
  ASSERT_NE(back.findBySha(kShaA), nullptr);
  EXPECT_EQ(back.findBySha(kShaA)->path, "/xteink/Dune.epub");
  EXPECT_EQ(back.findBySha(kShaC), nullptr);
}

TEST(XteinkSyncManifest, AddReplacesSamePathAndRemoves) {
  Manifest m;
  m.add({kShaA, "/xteink/Dune.epub", 42});
  m.add({kShaB, "/xteink/Dune.epub", 44});
  ASSERT_EQ(m.files.size(), 1u);
  EXPECT_EQ(m.files[0].sha256, kShaB);
  EXPECT_TRUE(m.removePath("/xteink/Dune.epub"));
  EXPECT_FALSE(m.removePath("/xteink/Dune.epub"));
  EXPECT_TRUE(m.files.empty());
}

TEST(XteinkSyncManifest, RejectsBadEntriesAndVersions) {
  Manifest m;
  const std::string wrongVersion = R"({"v":2,"files":[]})";
  EXPECT_FALSE(parseManifest(wrongVersion.data(), wrongVersion.size(), m));
  const std::string bad = R"({"v":1,"files":[{"sha256":"x","path":"/a"},{"sha256":")" + kShaA +
                          R"(","path":"relative"},{"sha256":")" + kShaA + R"(","path":"/ok.epub","id":7}]})";
  ASSERT_TRUE(parseManifest(bad.data(), bad.size(), m));
  ASSERT_EQ(m.files.size(), 1u);
  EXPECT_EQ(m.files[0].path, "/ok.epub");
}

TEST(XteinkSyncManifest, Capped) {
  Manifest m;
  for (size_t i = 0; i < kMaxManifestEntries; ++i) ASSERT_TRUE(m.add({kShaA, "/b/" + std::to_string(i), 1}));
  EXPECT_FALSE(m.add({kShaA, "/b/overflow", 1}));
}

TEST(XteinkSyncDeletes, OnlyUnmodifiedDeliveredFiles) {
  const DeliveredFile delivered{kShaA, "/xteink/Dune.epub", 42};
  // Unmodified copy of the listed sha: deleted.
  EXPECT_TRUE(shouldDelete(delivered, kShaA, kShaA));
  // Annotated / modified copy (hash changed): kept, like fake_device's f.unmodified.
  EXPECT_FALSE(shouldDelete(delivered, kShaA, kShaB));
  // Delete for a different sha: not this file.
  EXPECT_FALSE(shouldDelete(delivered, kShaB, kShaB));
  // File could not be hashed (missing/unreadable): kept.
  EXPECT_FALSE(shouldDelete(delivered, kShaA, ""));
}

TEST(XteinkSyncDeletes, SideloadedFilesAreNeverCandidates) {
  // Sideloaded files never enter the manifest, so a delete for their hash
  // finds no entry to act on.
  Manifest m;
  m.add({kShaA, "/xteink/Dune.epub", 42});
  EXPECT_EQ(m.findBySha(kShaB), nullptr);
}

// --- downloads -----------------------------------------------------------

TEST(XteinkSyncDownload, ResumeOffset) {
  EXPECT_EQ(resumeOffset(0, 1000), 0u);
  EXPECT_EQ(resumeOffset(300, 1000), 300u);
  EXPECT_EQ(resumeOffset(1000, 1000), 0u);  // would be 416: restart
  EXPECT_EQ(resumeOffset(1200, 1000), 0u);
  EXPECT_EQ(resumeOffset(5, 0), 0u);
}

TEST(XteinkSyncDownload, FileNames) {
  EXPECT_EQ(bookFileName("Dune", 42, "epub"), "Dune.epub");
  EXPECT_EQ(bookFileName("", 42, "epub"), "book-42.epub");
  EXPECT_EQ(bookFileName("A/B:C", 1, "pdf"), StringUtils::sanitizeFilename("A/B:C") + ".pdf");
  EXPECT_EQ(bookFileName("Dune", 42, "../x"), "Dune.bin");
  EXPECT_EQ(bookFileName("Dune", 42, "EPUB"), "Dune.bin");  // queue parse lowercases first
  EXPECT_EQ(bookFileNameWithId("Dune", 42, "epub"), "Dune (42).epub");
  EXPECT_EQ(bookFileNameWithId("", 42, "txt"), "book-42.txt");
  EXPECT_EQ(bookFileName("Dune", 1, "epub").find('/'), std::string::npos);
}

TEST(XteinkSyncDownload, Hex) {
  const uint8_t d[3] = {0x00, 0xab, 0xff};
  EXPECT_EQ(toHex(d, 3), "00abff");
  EXPECT_TRUE(isSha256Hex(kShaC));
  EXPECT_FALSE(isSha256Hex(std::string(64, 'A')));
  EXPECT_FALSE(isSha256Hex(std::string(63, 'a')));
}

// --- persisted result / status line ---------------------------------------

TEST(XteinkSyncResult, EncodeDecode) {
  LastResult ok;
  ok.valid = true;
  ok.ok = true;
  ok.newCount = 2;
  ok.time = "14:02";
  EXPECT_EQ(encodeLastResult(ok), "ok|2|14:02");
  const LastResult okBack = decodeLastResult("ok|2|14:02");
  EXPECT_TRUE(okBack.valid);
  EXPECT_TRUE(okBack.ok);
  EXPECT_EQ(okBack.newCount, 2);
  EXPECT_EQ(okBack.time, "14:02");

  const LastResult err = decodeLastResult("err|E401|");
  EXPECT_TRUE(err.valid);
  EXPECT_FALSE(err.ok);
  EXPECT_EQ(err.code, "E401");
  EXPECT_TRUE(err.time.empty());

  EXPECT_FALSE(decodeLastResult("").valid);
  EXPECT_FALSE(decodeLastResult("ok|x|").valid);
  EXPECT_FALSE(decodeLastResult("maybe|1|").valid);
  EXPECT_EQ(encodeLastResult(LastResult{}), "");
}

TEST(XteinkSyncResult, StatusLine) {
  const StatusTemplates t{"Synced %s \xC2\xB7 %d new", "Synced \xC2\xB7 %d new", "Synced %s", "Synced",
                          "Sync error %s"};
  char buf[64];
  EXPECT_TRUE(formatStatusLine(decodeLastResult("ok|2|14:02"), t, buf, sizeof(buf)));
  EXPECT_STREQ(buf, "Synced 14:02 \xC2\xB7 2 new");
  EXPECT_TRUE(formatStatusLine(decodeLastResult("ok|0|14:02"), t, buf, sizeof(buf)));
  EXPECT_STREQ(buf, "Synced 14:02");
  EXPECT_TRUE(formatStatusLine(decodeLastResult("ok|3|"), t, buf, sizeof(buf)));
  EXPECT_STREQ(buf, "Synced \xC2\xB7 3 new");
  EXPECT_TRUE(formatStatusLine(decodeLastResult("ok|0|"), t, buf, sizeof(buf)));
  EXPECT_STREQ(buf, "Synced");
  EXPECT_TRUE(formatStatusLine(decodeLastResult("err|E401|09:15"), t, buf, sizeof(buf)));
  EXPECT_STREQ(buf, "Sync error E401");
  EXPECT_FALSE(formatStatusLine(LastResult{}, t, buf, sizeof(buf)));
  EXPECT_STREQ(buf, "");
}

// --- r21: LAN probe budget and the SNTP attempt policy ----------------------

TEST(XteinkSyncBudget, ZeroBudgetNeverExpires) {
  EXPECT_FALSE(budgetExpired(0, 0, 0));
  EXPECT_FALSE(budgetExpired(0xFFFFFFFFu, 0, 0));
}

TEST(XteinkSyncBudget, ExpiresAtTheBudget) {
  EXPECT_FALSE(budgetExpired(1000 + 3999, 1000, 4000));
  EXPECT_TRUE(budgetExpired(1000 + 4000, 1000, 4000));
  EXPECT_TRUE(budgetExpired(1000 + 11000, 1000, 4000));
}

TEST(XteinkSyncBudget, SurvivesMillisWrap) {
  const uint32_t start = 0xFFFFFC00u;                     // 1024 ms before millis() wraps
  EXPECT_FALSE(budgetExpired(0x00000300u, start, 4000));  // 1024 + 768 = 1792 ms elapsed
  EXPECT_TRUE(budgetExpired(0x00000C00u, start, 4000));   // 1024 + 3072 = 4096 ms elapsed
}

namespace {
using sntp_policy::Action;
sntp_policy::Inputs inputs(bool synced, bool running, bool trusted, bool waited = false, uint32_t since = 0) {
  return sntp_policy::Inputs{synced, running, trusted, waited, since};
}
}  // namespace

TEST(XteinkSntpPolicy, SyncedClockNeverWaits) {
  EXPECT_EQ(sntp_policy::actionFor(inputs(true, false, false)), Action::Skip);
  EXPECT_EQ(sntp_policy::actionFor(inputs(true, true, true, true, 0)), Action::Skip);
}

TEST(XteinkSntpPolicy, ReusesRunningAttemptWithRestoredClock) {
  // The X3 log: the Wi-Fi join's NTP attempt timed out but SNTP keeps
  // running and the floor restored a plausible clock: no second attempt.
  EXPECT_EQ(sntp_policy::actionFor(inputs(false, true, true)), Action::Skip);
}

TEST(XteinkSntpPolicy, FirstRequestWithNothingRunningStartsOneBoundedSync) {
  EXPECT_EQ(sntp_policy::actionFor(inputs(false, false, true)), Action::StartAndWait);
  EXPECT_EQ(sntp_policy::actionFor(inputs(false, false, false)), Action::StartAndWait);
}

TEST(XteinkSntpPolicy, UntrustedClockWaitsOnRunningSntpWithoutRestart) {
  EXPECT_EQ(sntp_policy::actionFor(inputs(false, true, false)), Action::Wait);
}

TEST(XteinkSntpPolicy, AfterAFailedWaitNoRetryInsideTheWindow) {
  // Every later https request of the same sync session (seconds apart).
  EXPECT_EQ(sntp_policy::actionFor(inputs(false, true, false, true, 3000)), Action::Skip);
  EXPECT_EQ(sntp_policy::actionFor(inputs(false, false, true, true, 26000)), Action::Skip);
  EXPECT_EQ(sntp_policy::actionFor(inputs(false, true, false, true, sntp_policy::RETRY_AFTER_MS - 1)), Action::Skip);
}

TEST(XteinkSntpPolicy, OneRetryAfterTheWindow) {
  EXPECT_EQ(sntp_policy::actionFor(inputs(false, true, false, true, sntp_policy::RETRY_AFTER_MS)), Action::Wait);
  EXPECT_EQ(sntp_policy::actionFor(inputs(false, false, true, true, sntp_policy::RETRY_AFTER_MS)),
            Action::StartAndWait);
}
