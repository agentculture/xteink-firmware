// Host tests for OTA from the xteink server: the status-response offer parser,
// the install decision (the offer decides, not version ordering), and the
// verify-then-commit flow of installOffer() over a fake OtaPort that counts the
// ESP-IDF OTA calls.
#include <ArduinoJson.h>
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "network/OtaUpdater.h"
#include "xteink/SyncProtocol.h"

using namespace xteink::sync;

namespace {

// --- reference SHA-256 (FIPS 180-4), so the fake hashes for real ----------

class Sha256 {
 public:
  Sha256() { reset(); }
  void reset() {
    h = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    bufLen = 0;
    total = 0;
  }
  void update(const uint8_t* data, size_t len) {
    total += len;
    while (len > 0) {
      const size_t take = std::min(len, sizeof(buf) - bufLen);
      std::memcpy(buf + bufLen, data, take);
      bufLen += take;
      data += take;
      len -= take;
      if (bufLen == sizeof(buf)) {
        block(buf);
        bufLen = 0;
      }
    }
  }
  std::string hex() {
    const uint64_t bits = total * 8;
    const uint8_t one = 0x80;
    const uint8_t zero = 0;
    update(&one, 1);
    while (bufLen != 56) update(&zero, 1);
    uint8_t lenBytes[8];
    for (int i = 0; i < 8; ++i) lenBytes[i] = static_cast<uint8_t>(bits >> (56 - 8 * i));
    update(lenBytes, 8);
    uint8_t digest[32];
    for (int i = 0; i < 8; ++i) {
      for (int j = 0; j < 4; ++j) digest[i * 4 + j] = static_cast<uint8_t>(h[i] >> (24 - 8 * j));
    }
    return toHex(digest, sizeof(digest));
  }

 private:
  static uint32_t rotr(const uint32_t x, const int n) { return (x >> n) | (x << (32 - n)); }
  void block(const uint8_t* p) {
    static constexpr uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
      w[i] = (uint32_t{p[i * 4]} << 24) | (uint32_t{p[i * 4 + 1]} << 16) | (uint32_t{p[i * 4 + 2]} << 8) |
             uint32_t{p[i * 4 + 3]};
    }
    for (int i = 16; i < 64; ++i) {
      const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    auto v = h;
    for (int i = 0; i < 64; ++i) {
      const uint32_t s1 = rotr(v[4], 6) ^ rotr(v[4], 11) ^ rotr(v[4], 25);
      const uint32_t ch = (v[4] & v[5]) ^ (~v[4] & v[6]);
      const uint32_t t1 = v[7] + s1 + ch + k[i] + w[i];
      const uint32_t s0 = rotr(v[0], 2) ^ rotr(v[0], 13) ^ rotr(v[0], 22);
      const uint32_t maj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
      const uint32_t t2 = s0 + maj;
      v = {t1 + t2, v[0], v[1], v[2], v[3] + t1, v[4], v[5], v[6]};
    }
    for (int i = 0; i < 8; ++i) h[i] += v[i];
  }
  std::array<uint32_t, 8> h{};
  uint8_t buf[64] = {};
  size_t bufLen = 0;
  uint64_t total = 0;
};

std::string sha256Hex(const std::vector<uint8_t>& data) {
  Sha256 s;
  s.update(data.data(), data.size());
  return s.hex();
}

std::vector<uint8_t> makeImage(const size_t size) {
  std::vector<uint8_t> img(size);
  for (size_t i = 0; i < size; ++i) img[i] = static_cast<uint8_t>((i * 31 + 7) & 0xff);
  img[0] = 0xE9;  // ESP image magic
  return img;
}

FirmwareOffer offerFor(const std::vector<uint8_t>& img, const std::string& version = "1.6.5-dev-ota-abc12345") {
  FirmwareOffer o;
  o.present = true;
  o.version = version;
  o.size = static_cast<int64_t>(img.size());
  o.sha256 = sha256Hex(img);
  o.url = "/api/device/firmware/3";
  return o;
}

// Fake device: serves `body` in chunks, records every OTA call.
class FakePort : public OtaPort {
 public:
  std::vector<uint8_t> body;
  size_t chunk = 1000;
  int status = 200;
  bool complete = true;
  // Stop delivering after this many bytes (a dropped connection).
  size_t cutAfter = SIZE_MAX;
  // Deliver this many bytes, then rewind and deliver the whole body.
  size_t rewindAfter = 0;
  bool failWrite = false;
  bool failEnd = false;
  bool wrongDevice = false;

  int beginCalls = 0;
  int writeCalls = 0;
  int endCalls = 0;
  int abortCalls = 0;
  int setBootCalls = 0;
  std::vector<uint8_t> slot;  // bytes in the inactive slot
  std::vector<std::pair<size_t, size_t>> progressCalls;

  OtaFetchResult fetch(const OtaSink& sink) override {
    OtaFetchResult r;
    r.status = status;
    if (status < 200 || status >= 300) return r;
    if (rewindAfter > 0) {
      if (!deliver(sink, std::min(rewindAfter, body.size()))) return r;
      if (!sink.rewind()) return r;
    }
    const size_t n = std::min(cutAfter, body.size());
    if (!deliver(sink, n)) return r;
    r.complete = complete && n == body.size();
    return r;
  }
  bool otaBegin() override {
    ++beginCalls;
    slot.clear();
    return true;
  }
  bool otaWrite(const uint8_t* data, const size_t len) override {
    ++writeCalls;
    if (failWrite) return false;
    slot.insert(slot.end(), data, data + len);
    return true;
  }
  bool otaEnd() override {
    ++endCalls;
    return !failEnd;
  }
  void otaAbort() override { ++abortCalls; }
  bool setBootPartition() override {
    ++setBootCalls;
    return true;
  }
  void hashReset() override { sha.reset(); }
  void hashUpdate(const uint8_t* data, const size_t len) override { sha.update(data, len); }
  std::string hashHex() override { return sha.hex(); }
  void guardReset() override {}
  bool guardFeed(const uint8_t*, size_t) override { return !wrongDevice; }
  void progress(const size_t written, const size_t total) override { progressCalls.emplace_back(written, total); }

 private:
  bool deliver(const OtaSink& sink, const size_t n) {
    for (size_t off = 0; off < n; off += chunk) {
      if (!sink.write(body.data() + off, std::min(chunk, n - off))) return false;
    }
    return true;
  }
  Sha256 sha;
};

FirmwareOffer parseOffer(const std::string& json) {
  FirmwareOffer o;
  EXPECT_TRUE(parseStatusResponse(json.data(), json.size(), o)) << json;
  return o;
}

const std::string kSha(64, 'a');
const char* kRunning = "1.6.5-dev-feat/issue-1-e0aec33f";

}  // namespace

// --- reference hash sanity --------------------------------------------------

TEST(XteinkOtaSha, KnownVectors) {
  EXPECT_EQ(sha256Hex({}), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  const std::string abc = "abc";
  EXPECT_EQ(sha256Hex(std::vector<uint8_t>(abc.begin(), abc.end())),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

// --- offer parsing ----------------------------------------------------------

TEST(XteinkOtaOffer, ParsesOffer) {
  const FirmwareOffer o =
      parseOffer(R"({"protocol":"1","device":{"id":1},"firmware":{"version":"1.6.5-dev-ota-1","size":5891200,)"
                 R"("sha256":")" +
                 kSha + R"(","url":"/api/device/firmware/3"}})");
  EXPECT_TRUE(o.present);
  EXPECT_EQ(o.version, "1.6.5-dev-ota-1");
  EXPECT_EQ(o.size, 5891200);
  EXPECT_EQ(o.sha256, kSha);
  EXPECT_EQ(o.url, "/api/device/firmware/3");
}

TEST(XteinkOtaOffer, UppercaseShaIsLowered) {
  const FirmwareOffer o = parseOffer(R"({"firmware":{"version":"v","size":10,"sha256":")" + std::string(64, 'A') +
                                     R"(","url":"/api/device/firmware/1"}})");
  EXPECT_TRUE(o.present);
  EXPECT_EQ(o.sha256, kSha);
}

TEST(XteinkOtaOffer, MissingFirmwareFieldIsNone) {
  // An issue-1 server answers status without any offer.
  const FirmwareOffer o = parseOffer(R"({"protocol":"1","device":{"id":1,"firmware_version":"xteink-fw 1.6.5"}})");
  EXPECT_FALSE(o.present);
  EXPECT_EQ(decideOta(o, kRunning, false, false), OtaDecision::None);
  EXPECT_STREQ(otaDecisionName(decideOta(o, kRunning, false, false)), "none");
}

TEST(XteinkOtaOffer, NullFirmwareIsNone) {
  EXPECT_FALSE(parseOffer(R"({"protocol":"1","firmware":null})").present);
  EXPECT_FALSE(parseOffer(R"({})").present);
}

TEST(XteinkOtaOffer, MalformedOfferIsNone) {
  const std::string sha = kSha;
  const std::vector<std::string> bad = {
      R"({"size":10,"sha256":")" + sha + R"(","url":"/a/b"})",                         // no version
      R"({"version":"","size":10,"sha256":")" + sha + R"(","url":"/a/b"})",            // empty version
      R"({"version":"v","sha256":")" + sha + R"(","url":"/a/b"})",                     // no size
      R"({"version":"v","size":0,"sha256":")" + sha + R"(","url":"/a/b"})",            // zero size
      R"({"version":"v","size":-1,"sha256":")" + sha + R"(","url":"/a/b"})",           // negative
      R"({"version":"v","size":10,"sha256":"abc","url":"/a/b"})",                      // short sha
      R"({"version":"v","size":10,"sha256":")" + sha + R"("})",                        // no url
      R"({"version":"v","size":10,"sha256":")" + sha + R"(","url":"http://evil/x"})",  // absolute
      R"({"version":"v","size":10,"sha256":")" + sha + R"(","url":"//evil/x"})",       // protocol-relative
      std::string(R"("1.7.0")"),
  };
  for (const std::string& fw : bad) {
    EXPECT_FALSE(parseOffer(R"({"protocol":"1","firmware":)" + fw + "}").present) << fw;
  }
}

TEST(XteinkOtaOffer, NonObjectBodyIsParseError) {
  FirmwareOffer o;
  for (const std::string& body : {std::string("not json"), std::string("[1,2]"), std::string("")}) {
    EXPECT_FALSE(parseStatusResponse(body.data(), body.size(), o)) << body;
    EXPECT_FALSE(o.present);
  }
}

TEST(XteinkOtaOffer, CheckBodyCarriesOnlyFirmwareVersion) {
  JsonDocument doc;
  ASSERT_EQ(deserializeJson(doc, buildFirmwareCheckJson("xteink-fw 1.6.5")), DeserializationError::Ok);
  EXPECT_EQ(doc["firmware_version"].as<std::string>(), "xteink-fw 1.6.5");
  // Omitted fields keep the server's stored inventory and last_* values.
  EXPECT_EQ(doc.as<JsonObjectConst>().size(), 1u);
}

// --- install decision -------------------------------------------------------

TEST(XteinkOtaDecision, DifferentVersionInstalls) {
  FirmwareOffer o = offerFor(makeImage(64), "1.7.0");
  EXPECT_EQ(decideOta(o, kRunning, false, false), OtaDecision::Install);
}

TEST(XteinkOtaDecision, OlderVersionInstalls) {
  FirmwareOffer o = offerFor(makeImage(64), "1.6.4");
  EXPECT_EQ(decideOta(o, "1.6.5", false, false), OtaDecision::Install);
}

TEST(XteinkOtaDecision, DevSuffixDifferenceInstalls) {
  // Same X.Y.Z, different -dev branch/sha: isUpdateNewer said no; the offer says yes.
  FirmwareOffer o = offerFor(makeImage(64), "1.6.5-dev-ota/daily-sync-9a1b2c3d");
  EXPECT_EQ(decideOta(o, kRunning, false, false), OtaDecision::Install);
  EXPECT_STREQ(otaDecisionName(OtaDecision::Install), "install");
}

TEST(XteinkOtaDecision, SameVersionIsNoOp) {
  FirmwareOffer o = offerFor(makeImage(64), kRunning);
  EXPECT_EQ(decideOta(o, kRunning, false, false), OtaDecision::Current);
  // The status report's "xteink-fw " form names the same build.
  o.version = std::string("xteink-fw ") + kRunning;
  EXPECT_EQ(decideOta(o, kRunning, false, false), OtaDecision::Current);
  EXPECT_EQ(decideOta(o, kRunning, true, false), OtaDecision::Current);
  EXPECT_STREQ(otaDecisionName(OtaDecision::Current), "current");
}

TEST(XteinkOtaDecision, NoOpOfferNeverReachesTheSlot) {
  // The caller installs only on Install; a Current decision leaves every OTA
  // call at zero.
  const auto img = makeImage(4096);
  FakePort port;
  port.body = img;
  const FirmwareOffer o = offerFor(img, kRunning);
  if (decideOta(o, kRunning, false, false) == OtaDecision::Install) installOffer(o, port);
  EXPECT_EQ(port.beginCalls, 0);
  EXPECT_EQ(port.setBootCalls, 0);
}

TEST(XteinkOtaDecision, TunnelOnlyOfferIsDeferredByDefault) {
  EXPECT_EQ(XTEINK_OTA_TUNNEL, 0);
  FirmwareOffer o = offerFor(makeImage(64), "1.7.0");
  EXPECT_EQ(decideOta(o, kRunning, true, XTEINK_OTA_TUNNEL != 0), OtaDecision::Deferred);
  EXPECT_STREQ(otaDecisionName(OtaDecision::Deferred), "deferred:tunnel");
  EXPECT_EQ(decideOta(o, kRunning, true, true), OtaDecision::Install);
  EXPECT_EQ(decideOta(o, kRunning, false, false), OtaDecision::Install);
}

TEST(XteinkOtaDecision, LanOriginMatchesServerOrder) {
  EXPECT_TRUE(isLanOrigin("http://xteink.local:8781", ""));
  EXPECT_TRUE(isLanOrigin("http://192.168.1.20:8781", "HTTP://192.168.1.20:8781/"));
  EXPECT_FALSE(isLanOrigin("https://xteink.culture.dev", ""));
  EXPECT_FALSE(isLanOrigin("https://xteink.culture.dev", "http://192.168.1.20:8781"));
  // Every LAN entry of serverOrder() is recognised as LAN, the tunnel is not.
  const auto order = serverOrder("http://10.0.0.5:8781", "");
  ASSERT_EQ(order.size(), 2u);
  EXPECT_TRUE(isLanOrigin(order[0], "http://10.0.0.5:8781"));
  EXPECT_FALSE(isLanOrigin(order[1], "http://10.0.0.5:8781"));
}

// --- verify-then-commit -------------------------------------------------------

TEST(XteinkOtaInstall, MatchingImageSwitchesBootPartition) {
  const auto img = makeImage(10007);
  FakePort port;
  port.body = img;
  EXPECT_EQ(installOffer(offerFor(img), port), Error::None);
  EXPECT_EQ(port.slot, img);
  EXPECT_EQ(port.beginCalls, 1);
  EXPECT_EQ(port.endCalls, 1);
  EXPECT_EQ(port.abortCalls, 0);
  EXPECT_EQ(port.setBootCalls, 1);
  ASSERT_FALSE(port.progressCalls.empty());
  EXPECT_EQ(port.progressCalls.back(), std::make_pair(img.size(), img.size()));
}

TEST(XteinkOtaInstall, FlippedByteIsRefused) {
  const auto img = makeImage(10007);
  const FirmwareOffer offer = offerFor(img);
  for (const size_t at : {size_t{0}, size_t{5000}, img.size() - 1}) {
    FakePort port;
    port.body = img;
    port.body[at] ^= 0x01;
    const Error e = installOffer(offer, port);
    EXPECT_EQ(e, Error::Hash) << at;
    EXPECT_STREQ(errorCode(e), "ESHA");
    EXPECT_EQ(port.setBootCalls, 0) << at;
    EXPECT_EQ(port.endCalls, 0) << at;
    EXPECT_EQ(port.abortCalls, 1) << at;
  }
}

TEST(XteinkOtaInstall, TruncatedDownloadIsRefused) {
  const auto img = makeImage(10007);
  {
    // Connection dropped: the transfer reports incomplete.
    FakePort port;
    port.body = img;
    port.cutAfter = 6000;
    const Error e = installOffer(offerFor(img), port);
    EXPECT_EQ(e, Error::Download);
    EXPECT_STREQ(errorCode(e), "EDL");
    EXPECT_EQ(port.setBootCalls, 0);
    EXPECT_EQ(port.endCalls, 0);
    EXPECT_EQ(port.abortCalls, 1);
  }
  {
    // A short body the transport called complete (server sent a smaller file).
    FakePort port;
    port.body.assign(img.begin(), img.begin() + 6000);
    const Error e = installOffer(offerFor(img), port);
    EXPECT_EQ(e, Error::Download);
    EXPECT_STREQ(errorCode(e), "EDL");
    EXPECT_EQ(port.setBootCalls, 0);
    EXPECT_EQ(port.abortCalls, 1);
  }
}

TEST(XteinkOtaInstall, OversizedBodyIsRefused) {
  const auto img = makeImage(4000);
  FakePort port;
  port.body = img;
  port.body.push_back(0);
  EXPECT_EQ(installOffer(offerFor(img), port), Error::Download);
  EXPECT_EQ(port.setBootCalls, 0);
  EXPECT_LE(port.slot.size(), img.size());
}

TEST(XteinkOtaInstall, HttpErrorsAreRecorded) {
  const auto img = makeImage(100);
  const std::pair<int, const char*> cases[] = {{404, "EHTTP"}, {401, "E401"}, {-1, "ENET"}, {500, "EHTTP"}};
  for (const auto& [status, code] : cases) {
    FakePort port;
    port.body = img;
    port.status = status;
    const Error e = installOffer(offerFor(img), port);
    EXPECT_STREQ(errorCode(e), code) << status;
    EXPECT_EQ(port.setBootCalls, 0);
    EXPECT_EQ(port.abortCalls, 1);
  }
}

TEST(XteinkOtaInstall, WrongDeviceIsRefused) {
  const auto img = makeImage(100);
  FakePort port;
  port.body = img;
  port.wrongDevice = true;
  const Error e = installOffer(offerFor(img), port);
  EXPECT_EQ(e, Error::WrongDevice);
  EXPECT_STREQ(errorCode(e), "EDEV");
  EXPECT_EQ(port.writeCalls, 0);
  EXPECT_EQ(port.setBootCalls, 0);
}

TEST(XteinkOtaInstall, FlashFailuresNeverSwitchBoot) {
  const auto img = makeImage(3000);
  {
    FakePort port;
    port.body = img;
    port.failWrite = true;
    EXPECT_EQ(installOffer(offerFor(img), port), Error::Flash);
    EXPECT_EQ(port.setBootCalls, 0);
    EXPECT_EQ(port.abortCalls, 1);
  }
  {
    FakePort port;
    port.body = img;
    port.failEnd = true;  // esp_ota_end rejected the image
    const Error e = installOffer(offerFor(img), port);
    EXPECT_EQ(e, Error::Flash);
    EXPECT_STREQ(errorCode(e), "EFLASH");
    EXPECT_EQ(port.setBootCalls, 0);
  }
}

TEST(XteinkOtaInstall, RestartedBodyStartsTheSlotOver) {
  const auto img = makeImage(10007);
  FakePort port;
  port.body = img;
  port.rewindAfter = 4000;
  EXPECT_EQ(installOffer(offerFor(img), port), Error::None);
  EXPECT_EQ(port.beginCalls, 2);
  EXPECT_EQ(port.abortCalls, 1);
  EXPECT_EQ(port.slot, img);
  EXPECT_EQ(port.setBootCalls, 1);
}

TEST(XteinkOtaInstall, AbsentOfferInstallsNothing) {
  FakePort port;
  port.body = makeImage(10);
  EXPECT_EQ(installOffer(FirmwareOffer{}, port), Error::BadResponse);
  EXPECT_EQ(port.beginCalls, 0);
  EXPECT_EQ(port.setBootCalls, 0);
}
