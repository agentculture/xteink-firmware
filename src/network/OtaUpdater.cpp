#include "OtaUpdater.h"

// clang-format off
// HttpDownloader.h pulls Arduino/SdFat, whose macros collide with lwip's
// ip4_addr.h unless seen first. Pin this order; clang-format would otherwise sort
// the local header last and break the build.
#include "HttpDownloader.h"
#include <Arduino.h>
#include <Logging.h>
#include <Memory.h>
#include <ResumableFetch.h>
#include <SecureHttpClient.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>
// clang-format on

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

#include "FirmwareBoardTag.h"
#include "FirmwareFlasher.h"
#include "TlsTrust.h"
#include "WifiPowerSaveGuard.h"
#include "xteink/OtaHealth.h"
#include "xteink/XteinkConfig.h"

namespace xs = xteink::sync;

namespace {

constexpr const char* kTag = "OTA";
// Same string the sync's status report sends, so the server sees one version.
constexpr const char* kFirmwareVersion = "xteink-fw " CROSSPOINT_VERSION;

// Mirrors the sync client (XteinkSync.cpp): a quick LAN probe, a longer tunnel
// connect, and a generous per-phase timeout for the image transfer.
constexpr uint32_t kLanTimeoutMs = 4000;
constexpr uint32_t kTunnelTimeoutMs = 15000;
constexpr uint32_t kTransferTimeoutMs = 30000;

constexpr uint32_t kMinFreeHeapTls = HttpDownloader::MIN_TLS_FREE_HEAP + 6000;
constexpr uint32_t kMinMaxAllocTls = HttpDownloader::MIN_TLS_MAX_ALLOC + 2000;
constexpr uint32_t kMinFreeHeapPlain = 16000;
constexpr uint32_t kMinMaxAllocPlain = 8000;

bool heapOk(const bool tls) {
  const uint32_t freeHeap = ESP.getFreeHeap();
  const uint32_t maxAlloc = ESP.getMaxAllocHeap();
  const bool ok = tls ? (freeHeap >= kMinFreeHeapTls && maxAlloc >= kMinMaxAllocTls)
                      : (freeHeap >= kMinFreeHeapPlain && maxAlloc >= kMinMaxAllocPlain);
  if (!ok) {
    LOG_ERR(kTag, "Low heap for %s: %u free, %u max block", tls ? "https" : "http", static_cast<unsigned>(freeHeap),
            static_cast<unsigned>(maxAlloc));
  }
  return ok;
}

// Request headers for the xteink origin. The device key is attached only while
// the request stays on that origin (fetchResumable reports sameOrigin=false
// after a redirect elsewhere).
void configureClient(freeink::SecureHttpClient& client, const std::string& auth, const uint32_t timeoutMs,
                     const bool sameOrigin) {
  client.setTimeout(timeoutMs);
  tls_trust::applyVerifiedTls(client);
  client.setUserAgent("CrossPoint-ESP32-" CROSSPOINT_VERSION);
  if (sameOrigin) {
    client.addHeader("Authorization", auth);
    client.addHeader(xs::kProtocolHeader, xs::kProtocolVersion);
  }
}

// One status POST. HTTP status, or -1 on a transport failure, an over-long
// body, a truncated body, or when the whole-call budget ran out.
int postStatus(const std::string& origin, const std::string& auth, const std::string& body, std::string& response) {
  auto http = makeUniqueNoThrow<freeink::SecureHttpClient>();
  if (!http) return -1;
  const bool https = xs::isHttps(origin);
  const uint32_t timeoutMs = https ? kTunnelTimeoutMs : kLanTimeoutMs;
  WifiPowerSaveGuard psGuard;
  if (https) tls_trust::ensureClockForTls();
  if (!http->begin(origin + "/api/device/status")) return -1;
  configureClient(*http, auth, timeoutMs, true);
  http->addHeader("Content-Type", "application/json");
  bool tooLarge = false;
  const auto sink = [&response, &tooLarge](const uint8_t* data, const size_t len) {
    if (response.size() + len > xs::kMaxStatusResponse) {
      tooLarge = true;
      return false;
    }
    response.append(reinterpret_cast<const char*>(data), len);
    return true;
  };
  // The LAN probe has a whole-call budget so a slow peer cannot hold it (r21).
  const uint32_t startMs = millis();
  const uint32_t budgetMs = https ? 0 : kLanTimeoutMs;
  const int status = http->sendRequest("POST", reinterpret_cast<const uint8_t*>(body.data()), body.size(), sink,
                                       [startMs, budgetMs] { return xs::budgetExpired(millis(), startMs, budgetMs); });
  const bool complete = http->responseComplete();
  const bool aborted = http->aborted();
  http->end();
  if (aborted || tooLarge) return -1;
  if (status >= 200 && status < 300 && !complete) return -1;
  return status;
}

class Sha256 {
 public:
  Sha256() { start(); }
  ~Sha256() { mbedtls_sha256_free(&ctx); }
  Sha256(const Sha256&) = delete;
  Sha256& operator=(const Sha256&) = delete;
  void reset() {
    mbedtls_sha256_free(&ctx);
    start();
  }
  void update(const uint8_t* data, const size_t len) { mbedtls_sha256_update(&ctx, data, len); }
  std::string hex() {
    uint8_t digest[32];
    mbedtls_sha256_finish(&ctx, digest);
    return xs::toHex(digest, sizeof(digest));
  }

 private:
  void start() {
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, /*is224=*/0);
  }
  mbedtls_sha256_context ctx;
};

// The device side of xs::OtaPort: verified HTTP(S) download from the xteink
// origin, the inactive OTA slot, mbedTLS sha256, and the chip-id and board-tag
// checks that keep another device's image out of the slot.
class DevicePort final : public xs::OtaPort {
 public:
  DevicePort(std::string imageUrl, std::string authHeader, std::string offeredVersion, const esp_partition_t* slot,
             size_t& processed, const size_t total, OtaUpdater::ProgressCallback cb, void* cbCtx, const uint32_t budget)
      : url(std::move(imageUrl)),
        auth(std::move(authHeader)),
        version(std::move(offeredVersion)),
        partition(slot),
        processedSize(processed),
        totalSize(total),
        onProgress(cb),
        ctx(cbCtx),
        budgetMs(budget),
        startMs(millis()) {}

  freeink::SecureHttpClient::AbortCallback abortCallback() const {
    if (budgetMs == 0) return nullptr;
    const uint32_t start = startMs;
    const uint32_t budget = budgetMs;
    return [start, budget] { return xs::budgetExpired(millis(), start, budget); };
  }

  xs::OtaFetchResult fetch(const xs::OtaSink& sink) override {
    freeink::FetchSink fetchSink;
    fetchSink.write = sink.write;
    fetchSink.rewind = sink.rewind;
    freeink::FetchResult r;
    {
      WifiPowerSaveGuard psGuard;
      if (tls_trust::isHttpsUrl(url)) tls_trust::ensureClockForTls();
      const std::string& authRef = auth;
      r = freeink::fetchResumable(
          url, freeink::FetchOptions{},
          [&authRef](freeink::SecureHttpClient& http, const bool sameOrigin) {
            configureClient(http, authRef, kTransferTimeoutMs, sameOrigin);
          },
          fetchSink, abortCallback());
    }
    xs::OtaFetchResult out;
    out.status = r.status;
    out.complete = r.complete;
    out.aborted = r.aborted;
    return out;
  }

  bool otaBegin() override {
    // The offer's size: esp_ota_begin erases only the sectors the image needs,
    // not the whole 6.4 MB slot (OTA_SIZE_UNKNOWN), inside the install budget.
    const esp_err_t err = esp_ota_begin(partition, totalSize > 0 ? totalSize : OTA_SIZE_UNKNOWN, &handle);
    if (err != ESP_OK) {
      LOG_ERR(kTag, "esp_ota_begin failed: %s", esp_err_to_name(err));
      open = false;
      return false;
    }
    open = true;
    return true;
  }

  bool otaWrite(const uint8_t* data, const size_t len) override {
    const esp_err_t err = esp_ota_write(handle, data, len);
    if (err != ESP_OK) {
      LOG_ERR(kTag, "esp_ota_write failed: %s", esp_err_to_name(err));
      return false;
    }
    return true;
  }

  bool otaEnd() override {
    open = false;
    const esp_err_t err = esp_ota_end(handle);  // validates the written image
    if (err != ESP_OK) {
      LOG_ERR(kTag, "esp_ota_end failed: %s", esp_err_to_name(err));
      return false;
    }
    return true;
  }

  void otaAbort() override {
    if (!open) return;
    esp_ota_abort(handle);
    open = false;
  }

  bool setBootPartition() override {
    const esp_err_t err = esp_ota_set_boot_partition(partition);
    if (err != ESP_OK) {
      LOG_ERR(kTag, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
      return false;
    }
    return true;
  }

  void recordCandidate() override { xteink::otahealth::recordInstallCandidate(version); }
  void clearCandidate() override { xteink::otahealth::clearInstallCandidate(); }

  void hashReset() override { sha.reset(); }
  void hashUpdate(const uint8_t* data, const size_t len) override { sha.update(data, len); }
  std::string hashHex() override { return sha.hex(); }

  void guardReset() override {
    hdrLen = 0;
    tagScanner = board_tag::Scanner{};
  }

  bool guardFeed(const uint8_t* data, const size_t len) override {
    // Only the first bytes carry the image header: buffer 14 so chip_id
    // (esp_image_header_t offset 12) is checked before the slot is written.
    if (hdrLen < sizeof(hdr)) {
      const size_t take = std::min(len, sizeof(hdr) - hdrLen);
      std::memcpy(hdr + hdrLen, data, take);
      hdrLen += take;
      if (hdrLen == sizeof(hdr)) {
        uint16_t imageChip;
        std::memcpy(&imageChip, hdr + 12, sizeof(imageChip));
        const uint16_t deviceChip = firmware_flash::runningPartitionChipId();
        if (deviceChip != 0xFFFF && imageChip != deviceChip) {
          LOG_ERR(kTag, "wrong chip: image=0x%04X device=0x%04X", imageChip, deviceChip);
          return false;
        }
      }
    }
    // All S3 boards share a chip_id, so also scan for the embedded board tag
    // (FirmwareBoardTag.h). An untagged image passes; another board's tag fails.
    tagScanner.feed(data, len);
    if (tagScanner.mismatch()) {
      LOG_ERR(kTag, "wrong board: image=%s device=%.*s", tagScanner.foundName(),
              static_cast<int>(board_tag::boardNameLen()), board_tag::boardName());
      return false;
    }
    return true;
  }

  void progress(const size_t written, size_t) override {
    processedSize = written;
    // Fire only on whole-percent change: per-chunk updates wake the render
    // task, whose framebuffer work contends with TLS for the heap.
    if (onProgress && totalSize > 0) {
      const int pct = static_cast<int>(static_cast<uint64_t>(written) * 100 / totalSize);
      if (pct != lastPct) {
        lastPct = pct;
        onProgress(ctx);
      }
    }
  }

 private:
  const std::string url;
  const std::string auth;
  const std::string version;
  const esp_partition_t* partition;
  size_t& processedSize;
  const size_t totalSize;
  OtaUpdater::ProgressCallback onProgress;
  void* ctx;
  const uint32_t budgetMs;  // 0 = no wall-clock budget
  const uint32_t startMs;
  esp_ota_handle_t handle = 0;
  bool open = false;
  Sha256 sha;
  uint8_t hdr[14] = {};
  size_t hdrLen = 0;
  board_tag::Scanner tagScanner;
  int lastPct = -1;
};

}  // namespace

void OtaUpdater::setOffer(const xs::FirmwareOffer& newOffer, const std::string& fromOrigin) {
  offer = newOffer;
  origin = fromOrigin;
  totalSize = offer.present ? static_cast<size_t>(offer.size) : 0;
  processedSize = 0;
  const bool viaTunnel = !xs::isLanOrigin(origin, xteink::config::getLanUrl());
  decision = xs::decideOta(offer, CROSSPOINT_VERSION, viaTunnel, XTEINK_OTA_TUNNEL != 0,
                           xteink::otahealth::rolledBackBuilds());
  LOG_INF(kTag, "Offer from %s: %s (%s, running %s)", origin.c_str(), xs::otaDecisionName(decision),
          offer.present ? offer.version.c_str() : "-", CROSSPOINT_VERSION);
}

OtaUpdater::OtaUpdaterError OtaUpdater::checkForUpdate() {
  offer = xs::FirmwareOffer{};
  decision = xs::OtaDecision::None;
  origin.clear();
  lastError = xs::Error::None;
  serverAnswered = false;

  const std::string key = xteink::config::getDeviceKey();
  if (key.empty()) {
    // The xteink server is the only update source; unprovisioned devices have none.
    LOG_INF(kTag, "No device key: no update source");
    lastError = xs::Error::NoKey;
    return NOT_PROVISIONED;
  }
  const std::string auth = "Bearer " + key;
  const std::string rolledBack = xteink::otahealth::rolledBackVersion();
  const std::string body = xs::buildFirmwareCheckJson(kFirmwareVersion, rolledBack);

  bool heapSkipped = false;
  for (const std::string& candidate : xs::serverOrder(xteink::config::getLanUrl(), xteink::config::getTunnelUrl())) {
    if (!heapOk(xs::isHttps(candidate))) {
      heapSkipped = true;
      continue;
    }
    std::string response;
    const int status = postStatus(candidate, auth, body, response);
    if (status < 0) {
      LOG_INF(kTag, "No answer from %s", candidate.c_str());
      continue;
    }
    serverAnswered = true;
    lastError = xs::errorForStatus(status);
    if (lastError != xs::Error::None) {
      LOG_ERR(kTag, "Status from %s: %d (%s)", candidate.c_str(), status, xs::errorCode(lastError));
      return HTTP_ERROR;
    }
    xteink::otahealth::clearRolledBackVersion(rolledBack);
    xs::FirmwareOffer parsed;
    if (!xs::parseStatusResponse(response.data(), response.size(), parsed)) {
      LOG_ERR(kTag, "Status response from %s did not parse", candidate.c_str());
      lastError = xs::Error::BadResponse;
      return JSON_PARSE_ERROR;
    }
    setOffer(parsed, candidate);
    return decision == xs::OtaDecision::Install ? OK : NO_UPDATE;
  }
  lastError = heapSkipped ? xs::Error::LowMemory : xs::Error::Network;
  return heapSkipped ? OOM_ERROR : HTTP_ERROR;
}

OtaUpdater::OtaUpdaterError OtaUpdater::installUpdate(ProgressCallback onProgress, void* ctx, const uint32_t budgetMs) {
  if (decision != xs::OtaDecision::Install) {
    return UPDATE_OLDER_ERROR;
  }
  const std::string key = xteink::config::getDeviceKey();
  if (key.empty()) {
    lastError = xs::Error::NoKey;
    return NOT_PROVISIONED;
  }
  if (!heapOk(xs::isHttps(origin))) {
    lastError = xs::Error::LowMemory;
    return OOM_ERROR;
  }
  const esp_partition_t* updatePartition = esp_ota_get_next_update_partition(nullptr);
  if (!updatePartition) {
    LOG_ERR(kTag, "No OTA partition available");
    lastError = xs::Error::Flash;
    return INTERNAL_UPDATE_ERROR;
  }
  // 2 KB of members (sha context, scanner): heap, not the caller's stack.
  auto port = makeUniqueNoThrow<DevicePort>(origin + offer.url, "Bearer " + key, offer.version, updatePartition,
                                            processedSize, totalSize, onProgress, ctx, budgetMs);
  if (!port) {
    lastError = xs::Error::LowMemory;
    return OOM_ERROR;
  }
  processedSize = 0;
  LOG_INF(kTag, "Installing %s (%lld B) from %s", offer.version.c_str(), static_cast<long long>(offer.size),
          origin.c_str());
  lastError = xs::installOffer(offer, *port);
  if (lastError != xs::Error::None) {
    LOG_ERR(kTag, "Firmware install failed: %s", xs::errorCode(lastError));
  }
  switch (lastError) {
    case xs::Error::None:
      LOG_INF(kTag, "Update completed");
      return OK;
    case xs::Error::WrongDevice:
      return WRONG_DEVICE_ERROR;
    case xs::Error::Hash:
      return HASH_ERROR;
    case xs::Error::Flash:
      return INTERNAL_UPDATE_ERROR;
    case xs::Error::LowMemory:
      return OOM_ERROR;
    default:
      return HTTP_ERROR;
  }
}
