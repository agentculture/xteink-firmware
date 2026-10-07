#include "XteinkSync.h"

#include <Arduino.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <I18n.h>
#include <LibraryBuilder.h>
#include <Logging.h>
#include <Memory.h>
#include <ResumableFetch.h>
#include <SecureHttpClient.h>
#include <TrustedTime.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <mbedtls/sha256.h>
#include <nvs.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <ctime>
#include <iterator>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

#include "CrossPointSettings.h"
#include "FreeSpaceCache.h"
#include "XteinkConfig.h"
#include "activities/Activity.h"  // ActivityManager + RenderLock
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"
#include "network/TlsTrust.h"
#include "network/WifiPowerSaveGuard.h"
#include "util/BookCacheUtils.h"
#include "util/TaskWatchdog.h"

namespace xteink::sync {

namespace {

constexpr const char* kTag = "XSYNC";
constexpr const char* kFirmwareVersion = "xteink-fw " CROSSPOINT_VERSION;
// Delivered books land here, apart from anything the user sideloads.
constexpr const char* kBooksDir = "/xteink";
constexpr const char* kManifestPath = "/.crosspoint/xteink-delivered.json";
constexpr const char* kManifestTmp = "/.crosspoint/xteink-delivered.json.tmp";
// One download at a time, staged here and renamed into kBooksDir once verified.
constexpr const char* kPartPath = "/.crosspoint/xteink-download.part";
constexpr const char* kNvsNamespace = "xteink";  // shared with XteinkConfig
constexpr const char* kNvsLastResult = "sync_last";
constexpr const char* kNvsFreeBytes = "fs_free";  // fix F: cached SD free space
constexpr const char* kNvsFreeAt = "fs_at";

// The LAN probe must fall through to the tunnel quickly when xteink.local or
// the provisioned address does not answer. It is both the connect timeout and
// the budget for the whole status call (connect + request + response):
// SecureHttpClient applies its timeout per phase (connect, then again to the
// headers, then per body stall), so a peer that accepts the TCP connection but
// answers slowly could otherwise hold the probe for a multiple of it (r21).
constexpr uint32_t kLanTimeoutMs = 4000;
constexpr uint32_t kTunnelTimeoutMs = 15000;
constexpr uint32_t kTransferTimeoutMs = 30000;
// A sync started by an unrelated Wi-Fi join stops picking up new items after
// this long, so it cannot hold up the screen the user was going to.
constexpr uint32_t kJoinItemBudgetMs = 60000;

// Verified TLS to xteink.culture.dev (Let's Encrypt chain with an RSA-4096
// signature) peaks around 39 KB on the host, above HttpDownloader's generic
// floor; docs/xteink/tls.md "Cost".
constexpr uint32_t kMinFreeHeapTls = HttpDownloader::MIN_TLS_FREE_HEAP + 6000;
constexpr uint32_t kMinMaxAllocTls = HttpDownloader::MIN_TLS_MAX_ALLOC + 2000;
constexpr uint32_t kMinFreeHeapPlain = 16000;
constexpr uint32_t kMinMaxAllocPlain = 8000;

constexpr size_t kHashChunk = 2048;
constexpr size_t kHashYieldBytes = 32 * 1024;

// Session::call results below zero that are not transport failures.
constexpr int kCallTooLarge = -2;
constexpr int kCallAborted = -3;

bool joinHookSuppressed = false;

// Last result as encoded in NVS, cached for the render task.
portMUX_TYPE resultMux = portMUX_INITIALIZER_UNLOCKED;
char cachedResult[40] = "";
bool cachedLoaded = false;

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

// std::string growth aborts on OOM (-fno-exceptions): probe before growing.
bool canGrow(const size_t bytes) { return heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) >= bytes + 512; }

bool cancelled(const Callbacks& cb) { return cb.shouldCancel && cb.shouldCancel(cb.ctx); }

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
    return toHex(digest, sizeof(digest));
  }

 private:
  void start() {
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, /*is224=*/0);
  }
  mbedtls_sha256_context ctx;
};

bool hashFile(const std::string& path, uint8_t* buf, std::string& out) {
  HalFile file;
  if (!Storage.openFileForRead(kTag, path, file)) return false;
  Sha256 sha;
  size_t sinceYield = 0;
  for (;;) {
    const int n = file.read(buf, kHashChunk);
    if (n < 0) return false;
    if (n == 0) break;
    sha.update(buf, static_cast<size_t>(n));
    sinceYield += static_cast<size_t>(n);
    if (sinceYield >= kHashYieldBytes) {
      sinceYield = 0;
      resetTaskWatchdogIfSubscribed();
      delay(1);  // let the idle task run during long hashes
    }
  }
  out = sha.hex();
  return true;
}

bool loadManifest(Manifest& m) {
  m.files.clear();
  if (!Storage.exists(kManifestPath)) return true;
  std::string raw;
  if (!Storage.readFileToString(kTag, kManifestPath, kMaxManifestBytes, raw) ||
      !parseManifest(raw.data(), raw.size(), m)) {
    // Fail safe: with no manifest nothing is reported or deleted, so a
    // corrupt file can only leave delivered books alone.
    LOG_ERR(kTag, "Delivered-file manifest unreadable; starting empty");
    return false;
  }
  return true;
}

bool saveManifest(const Manifest& m) {
  if (!canGrow(m.files.size() * 160 + 64)) {
    LOG_ERR(kTag, "OOM: manifest");
    return false;
  }
  const std::string json = buildManifestJson(m);
  {
    HalFile file;
    if (!Storage.openFileForWrite(kTag, kManifestTmp, file)) return false;
    if (file.write(json.data(), json.size()) != json.size()) {
      file.close();
      Storage.remove(kManifestTmp);
      return false;
    }
  }
  if (!Storage.replaceFile(kManifestTmp, kManifestPath)) {
    Storage.remove(kManifestTmp);
    LOG_ERR(kTag, "Manifest replace failed");
    return false;
  }
  return true;
}

bool contains(const std::vector<std::string>& v, const std::string& s) {
  return std::find(v.begin(), v.end(), s) != v.end();
}

std::string nowHhMm() {
  char buf[12];
  const bool use12h = SETTINGS.clockFormat == 1;
  if (halClock.formatTime(buf, sizeof(buf), use12h)) return buf;
  const int64_t now = trustedtime::trustedNow();
  if (now <= 0) return "";
  const time_t t = static_cast<time_t>(now);
  struct tm tm{};
  localtime_r(&t, &tm);
  if (strftime(buf, sizeof(buf), use12h ? "%I:%M %p" : "%H:%M", &tm) == 0) return "";
  return buf;
}

std::string readNvsResult() {
  nvs_handle_t h;
  if (nvs_open(kNvsNamespace, NVS_READONLY, &h) != ESP_OK) return "";
  char buf[sizeof(cachedResult)] = "";
  size_t len = sizeof(buf);
  if (nvs_get_str(h, kNvsLastResult, buf, &len) != ESP_OK) buf[0] = '\0';
  nvs_close(h);
  return buf;
}

void cacheResult(const std::string& encoded) {
  portENTER_CRITICAL(&resultMux);
  strncpy(cachedResult, encoded.c_str(), sizeof(cachedResult) - 1);
  cachedResult[sizeof(cachedResult) - 1] = '\0';
  cachedLoaded = true;
  portEXIT_CRITICAL(&resultMux);
}

LastResult previousResult() {
  char buf[sizeof(cachedResult)];
  bool loaded;
  portENTER_CRITICAL(&resultMux);
  loaded = cachedLoaded;
  memcpy(buf, cachedResult, sizeof(buf));
  portEXIT_CRITICAL(&resultMux);
  if (!loaded) {
    const std::string fromNvs = readNvsResult();
    cacheResult(fromNvs);
    return decodeLastResult(fromNvs);
  }
  return decodeLastResult(buf);
}

void persistResult(const Outcome& out) {
  if (out.error == Error::Cancelled || out.error == Error::NoKey) return;
  LastResult r;
  r.valid = true;
  r.ok = out.error == Error::None;
  r.newCount = out.delivered;
  r.code = errorCode(out.error);
  r.time = nowHhMm();
  const std::string encoded = encodeLastResult(r);
  cacheResult(encoded);
  nvs_handle_t h;
  if (nvs_open(kNvsNamespace, NVS_READWRITE, &h) != ESP_OK) {
    LOG_ERR(kTag, "NVS open failed; result kept for this boot only");
    return;
  }
  if (nvs_set_str(h, kNvsLastResult, encoded.c_str()) != ESP_OK || nvs_commit(h) != ESP_OK) {
    LOG_ERR(kTag, "NVS write failed");
  }
  nvs_close(h);
}

// One server origin for the whole sync. The device key and protocol header
// are only ever attached to requests for this origin: API calls never follow
// redirects (SecureHttpClient default), and downloads go through
// fetchResumable, which reports sameOrigin=false after a redirect to another
// scheme/host/port (ResumableFetch.h, fetchResumable).
class Session {
 public:
  Session(std::string originUrl, const std::string& deviceKey, const Callbacks& callbacks)
      : origin(std::move(originUrl)), https(isHttps(origin)), auth("Bearer " + deviceKey), cb(callbacks) {}

  const std::string origin;
  const bool https;
  uint32_t timeoutMs = kTransferTimeoutMs;
  // Whole-call budget for call() (0 = none). The connect itself is bounded by
  // timeoutMs; every later phase polls the abort callback, which also fires
  // once this budget is spent.
  uint32_t budgetMs = 0;

  bool begin() {
    http = makeUniqueNoThrow<freeink::SecureHttpClient>();
    return http != nullptr;
  }

  void configure(freeink::SecureHttpClient& client, const bool sameOrigin) const {
    client.setTimeout(timeoutMs);
    tls_trust::applyVerifiedTls(client);
    client.setUserAgent("CrossPoint-ESP32-" CROSSPOINT_VERSION);
    if (sameOrigin) {
      client.addHeader("Authorization", auth);
      client.addHeader(kProtocolHeader, kProtocolVersion);
    }
  }

  freeink::SecureHttpClient::AbortCallback abortCallback(const uint32_t startMs = 0) const {
    const Callbacks* c = &cb;
    const uint32_t budget = budgetMs;
    return [c, startMs, budget] { return cancelled(*c) || budgetExpired(millis(), startMs, budget); };
  }

  // HTTP status, -1 on transport failure, kCallTooLarge when the body would
  // exceed `cap`, kCallAborted when cancelled.
  int call(const char* method, const char* path, const std::string* body, std::string* response, const size_t cap) {
    WifiPowerSaveGuard psGuard;
    if (https) tls_trust::ensureClockForTls();
    const uint32_t startMs = millis();
    if (!http->begin(origin + path)) return -1;
    configure(*http, true);
    if (body) http->addHeader("Content-Type", "application/json");
    bool tooLarge = false;
    const auto sink = [response, cap, &tooLarge](const uint8_t* data, const size_t len) {
      if (!response) return true;  // drain
      if (response->size() + len > cap || !canGrow(response->size() + len)) {
        tooLarge = true;
        return false;
      }
      response->append(reinterpret_cast<const char*>(data), len);
      return true;
    };
    const int status = http->sendRequest(method, body ? reinterpret_cast<const uint8_t*>(body->data()) : nullptr,
                                         body ? body->size() : 0, sink, abortCallback(startMs));
    if (http->aborted()) {
      if (cancelled(cb)) return kCallAborted;
      // Not the user: the call budget ran out. A transport failure.
      http->end();
      return -1;
    }
    if (tooLarge) return kCallTooLarge;
    // A truncated JSON body is a transport failure, not a parse error.
    if (status >= 200 && status < 300 && response && !http->responseComplete()) return -1;
    return status;
  }

  // Drops the kept-alive connection so only one TLS session exists at a time
  // (downloads open their own).
  void close() {
    if (http) http->end();
  }

 private:
  const std::string auth;
  const Callbacks& cb;
  std::unique_ptr<freeink::SecureHttpClient> http;
};

Error callError(const int status) {
  if (status == kCallTooLarge) return Error::BadResponse;
  if (status == kCallAborted) return Error::Cancelled;
  return errorForStatus(status);
}

// GET item.url into kPartPath, hashing as it streams. A dropped connection is
// resumed with Range inside fetchResumable; a 416 discards the partial bytes
// and starts again from 0 once (docs/device-protocol.md, Errors).
Error downloadItem(const Session& session, const QueueItem& item, const Callbacks& cb, Progress& progress) {
  for (int attempt = 0; attempt < 2; ++attempt) {
    if (!heapOk(session.https)) return Error::LowMemory;
    Storage.remove(kPartPath);
    HalFile file;
    if (!Storage.openFileForWrite(kTag, kPartPath, file)) return Error::SdCard;
    Sha256 sha;
    size_t written = 0;
    bool tooLong = false;
    bool writeFailed = false;
    const size_t expected = static_cast<size_t>(item.size);

    freeink::FetchSink sink;
    sink.write = [&](const uint8_t* data, const size_t len) {
      if (written + len > expected) {
        tooLong = true;
        return false;
      }
      if (file.write(data, len) != len) {
        writeFailed = true;
        return false;
      }
      sha.update(data, len);
      written += len;
      return true;
    };
    // The server ignored Range and restarted the body: start the file and hash over.
    sink.rewind = [&] {
      file.close();
      sha.reset();
      written = 0;
      return Storage.openFileForWrite(kTag, kPartPath, file);
    };
    sink.progress = [&](const size_t bytes, size_t) {
      progress.bytes = bytes;
      if (cb.onProgress) cb.onProgress(cb.ctx, progress);
    };

    freeink::FetchResult result;
    {
      WifiPowerSaveGuard psGuard;
      if (session.https) tls_trust::ensureClockForTls();
      result = freeink::fetchResumable(
          session.origin + item.url, freeink::FetchOptions{},
          [&session](freeink::SecureHttpClient& http, const bool sameOrigin) { session.configure(http, sameOrigin); },
          sink, session.abortCallback());
    }
    // Close before remove() or rename() on the same path.
    if (file.isOpen()) file.close();

    if (result.aborted) {
      Storage.remove(kPartPath);
      return Error::Cancelled;
    }
    if (result.status == 416 && attempt == 0) {
      LOG_INF(kTag, "Item %lld: 416, restarting from 0", static_cast<long long>(item.id));
      continue;
    }
    Error err = Error::None;
    if (writeFailed) {
      err = Error::SdCard;
    } else if (result.status < 200 || result.status >= 300) {
      err = errorForStatus(result.status);
      if (err == Error::None) err = Error::Download;
    } else if (tooLong || !result.complete || written != expected) {
      err = Error::Download;
    } else if (sha.hex() != item.sha256) {
      err = Error::Hash;
    }
    if (err != Error::None) {
      LOG_ERR(kTag, "Item %lld failed: %s (status %d, %u/%u bytes)", static_cast<long long>(item.id), errorCode(err),
              result.status, static_cast<unsigned>(written), static_cast<unsigned>(expected));
      Storage.remove(kPartPath);
    }
    return err;
  }
  Storage.remove(kPartPath);
  return Error::Download;
}

bool isOurs(const Manifest& m, const std::string& path) {
  return std::any_of(m.files.begin(), m.files.end(), [&path](const DeliveredFile& f) { return f.path == path; });
}

// Moves the verified kPartPath into kBooksDir. Never overwrites a sideloaded
// or user-modified file.
bool placeFile(const QueueItem& item, const Manifest& m, const std::vector<std::string>& modified,
               std::string& outPath) {
  if (!Storage.exists(kBooksDir) && !Storage.mkdir(kBooksDir)) return false;
  const std::string dir = std::string(kBooksDir) + "/";
  std::string path = dir + bookFileName(item.title, item.id, item.format);
  bool replace = false;
  if (Storage.exists(path.c_str())) {
    path = dir + bookFileNameWithId(item.title, item.id, item.format);
    if (Storage.exists(path.c_str())) {
      if (!isOurs(m, path) || contains(modified, path)) {
        LOG_ERR(kTag, "Name taken by a file we did not deliver: %s", path.c_str());
        return false;
      }
      replace = true;
    }
  }
  const bool ok = replace ? Storage.replaceFile(kPartPath, path.c_str()) : Storage.rename(kPartPath, path.c_str());
  if (!ok) return false;
  outPath = path;
  return true;
}

std::vector<InventoryEntry> inventoryOf(const Manifest& m, const std::vector<std::string>& modified) {
  std::vector<InventoryEntry> inv;
  inv.reserve(m.files.size());
  std::transform(m.files.begin(), m.files.end(), std::back_inserter(inv),
                 [&modified](const auto& f) { return InventoryEntry{f.sha256, !contains(modified, f.path)}; });
  return inv;
}

int64_t freeSdBytes() {
  const uint64_t free = Storage.sdFreeBytes();
  return free == 0 ? -1 : static_cast<int64_t>(free);
}

// Fix F: the free-cluster count is slow (7 s on a 16 GB card), so the value
// lives in RAM and NVS with the time it was measured (FreeSpaceCache.h).
// Only sync() reads or writes it, and syncs never overlap.
freespace::Cache freeCache;
bool freeCacheLoaded = false;

freespace::Cache loadFreeCache() {
  if (freeCacheLoaded) return freeCache;
  freeCacheLoaded = true;
  nvs_handle_t h;
  if (nvs_open(kNvsNamespace, NVS_READONLY, &h) != ESP_OK) return freeCache;
  int64_t bytes = -1;
  int64_t at = 0;
  if (nvs_get_i64(h, kNvsFreeBytes, &bytes) == ESP_OK && nvs_get_i64(h, kNvsFreeAt, &at) == ESP_OK) {
    freeCache = freespace::Cache{bytes, at};
  }
  nvs_close(h);
  return freeCache;
}

void storeFreeCache(const freespace::Cache& cache) {
  freeCache = cache;
  freeCacheLoaded = true;
  nvs_handle_t h;
  if (nvs_open(kNvsNamespace, NVS_READWRITE, &h) != ESP_OK) return;
  if (nvs_set_i64(h, kNvsFreeBytes, cache.freeBytes) != ESP_OK ||
      nvs_set_i64(h, kNvsFreeAt, cache.measuredAt) != ESP_OK || nvs_commit(h) != ESP_OK) {
    LOG_ERR(kTag, "NVS write failed (free space cache)");
  }
  nvs_close(h);
}

// Free space for a status report: the cached value when the policy allows,
// else a real query that refreshes the cache. `measured` is set on a query.
int64_t reportFreeBytes(const int64_t pendingBytes, bool& measured) {
  const freespace::Cache cache = loadFreeCache();
  const int64_t now = trustedtime::trustedNow();
  if (freespace::usable(cache, now, pendingBytes)) {
    LOG_INF(kTag, "Free space: cached %lld bytes (%lld s old)", static_cast<long long>(cache.freeBytes),
            static_cast<long long>(now - cache.measuredAt));
    return cache.freeBytes;
  }
  const uint32_t startMs = millis();
  const int64_t bytes = freeSdBytes();
  measured = true;
  storeFreeCache(freespace::measured(bytes, now));
  LOG_INF(kTag, "Free space query: %lu ms", static_cast<unsigned long>(millis() - startMs));
  return bytes;
}

}  // namespace

Outcome run(const GfxRenderer* renderer, const Callbacks& cb, const uint32_t itemBudgetMs) {
  Outcome out;
  const uint32_t started = millis();
  const std::string key = config::getDeviceKey();
  if (key.empty()) {
    out.error = Error::NoKey;
    return out;
  }
  if (WiFi.status() != WL_CONNECTED) {
    out.error = Error::Network;
    persistResult(out);
    return out;
  }
  LOG_INF(kTag, "Sync start (key id %s, heap %u, max block %u)", config::getKeyId().c_str(),
          static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
  // Rebuildable SD-font caches can hold tens of KB the TLS session needs.
  if (renderer) {
    if (auto* fcm = renderer->getFontCacheManager()) fcm->releaseSdFontCaches();
  }

  // Reused for every file hash in this sync; heap, not stack (2 KB).
  auto hashBuf = makeUniqueNoThrow<uint8_t[]>(kHashChunk);
  if (!hashBuf) {
    LOG_ERR(kTag, "OOM: hash buffer");
    out.error = Error::LowMemory;
    persistResult(out);
    return out;
  }

  // 1. Inventory: re-hash every delivered file still on the card.
  uint32_t phaseMs = millis();
  Manifest manifest;
  loadManifest(manifest);
  std::vector<std::string> modified;
  {
    bool pruned = false;
    for (size_t i = 0; i < manifest.files.size();) {
      const DeliveredFile& f = manifest.files[i];
      if (!Storage.exists(f.path.c_str())) {
        // The user removed it: it is no longer on the device to report.
        manifest.files.erase(manifest.files.begin() + static_cast<std::ptrdiff_t>(i));
        pruned = true;
        continue;
      }
      std::string sha;
      if (!hashFile(f.path, hashBuf.get(), sha) || sha != f.sha256) modified.push_back(f.path);
      ++i;
    }
    if (pruned) saveManifest(manifest);
  }
  // Phase timings locate where a slow sync spends its time before the first
  // request (r21: 11 s between "Sync start" and the LAN verdict on the X3).
  LOG_INF(kTag, "Inventory: %u files hashed in %lu ms", static_cast<unsigned>(manifest.files.size()),
          static_cast<unsigned long>(millis() - phaseMs));

  const LastResult previous = previousResult();
  StatusReport report;
  bool freeMeasured = false;
  report.freeSdBytes = reportFreeBytes(0, freeMeasured);
  report.firmwareVersion = kFirmwareVersion;
  if (previous.valid) {
    report.lastSyncResult = previous.ok ? "ok" : "error";
    if (!previous.ok) report.lastError = previous.code;
  }
  report.inventory = inventoryOf(manifest, modified);

  // 2. Server order (c60): the first origin that answers the status report is
  // used for the rest of the sync.
  std::unique_ptr<Session> session;
  int status = -1;
  bool heapSkipped = false;
  {
    if (!canGrow(report.inventory.size() * 100 + 256)) {
      out.error = Error::LowMemory;
      persistResult(out);
      return out;
    }
    const std::string body = buildStatusJson(report);
    for (const std::string& origin : serverOrder(config::getLanUrl(), config::getTunnelUrl())) {
      if (cancelled(cb)) {
        out.error = Error::Cancelled;
        return out;
      }
      if (!heapOk(isHttps(origin))) {
        heapSkipped = true;
        continue;
      }
      auto candidate = makeUniqueNoThrow<Session>(origin, key, cb);
      if (!candidate || !candidate->begin()) {
        heapSkipped = true;
        continue;
      }
      candidate->timeoutMs = candidate->https ? kTunnelTimeoutMs : kLanTimeoutMs;
      candidate->budgetMs = candidate->https ? 0 : kLanTimeoutMs;
      if (cb.onProgress) cb.onProgress(cb.ctx, Progress{});
      const uint32_t probeStart = millis();
      status = candidate->call("POST", "/api/device/status", &body, nullptr, 0);
      const auto probeMs = static_cast<unsigned long>(millis() - probeStart);
      if (status == kCallAborted) {
        out.error = Error::Cancelled;
        return out;
      }
      if (status < 0) {
        LOG_INF(kTag, "No answer from %s (%lu ms)", origin.c_str(), probeMs);
        continue;
      }
      LOG_INF(kTag, "Using %s (%lu ms)", origin.c_str(), probeMs);
      session = std::move(candidate);
      break;
    }
  }
  if (!session) {
    out.error = heapSkipped ? Error::LowMemory : Error::Network;
    persistResult(out);
    return out;
  }
  session->timeoutMs = kTransferTimeoutMs;
  session->budgetMs = 0;
  out.error = callError(status);
  if (out.error != Error::None) {
    LOG_ERR(kTag, "Status report: %d (%s)", status, errorCode(out.error));
    persistResult(out);
    return out;
  }

  // 3. Queue.
  Queue queue;
  {
    std::string body;
    status = session->call("GET", "/api/device/queue", nullptr, &body, kMaxQueueBody);
    out.error = callError(status);
    if (out.error == Error::None && !parseQueue(body.data(), body.size(), queue)) out.error = Error::BadResponse;
    if (out.error != Error::None) {
      LOG_ERR(kTag, "Queue: %d (%s)", status, errorCode(out.error));
      if (out.error != Error::Cancelled) persistResult(out);
      return out;
    }
  }
  // The cached free space was only checked against an empty queue; when the
  // queue would leave it tight, measure the card before writing (sd_full).
  if (!freeMeasured) {
    const int64_t pendingBytes =
        std::accumulate(queue.items.begin(), queue.items.end(), int64_t{0},
                        [](const int64_t sum, const QueueItem& item) { return sum + (item.size > 0 ? item.size : 0); });
    report.freeSdBytes = reportFreeBytes(pendingBytes, freeMeasured);
  }
  int64_t writtenBytes = 0;
  out.skipped = static_cast<uint16_t>(queue.skipped.size());
  LOG_INF(kTag, "Queue: %u items, %u skipped, %u deletes, %u dropped", static_cast<unsigned>(queue.items.size()),
          static_cast<unsigned>(queue.skipped.size()), static_cast<unsigned>(queue.deletes.size()),
          static_cast<unsigned>(queue.droppedItems));

  // 4. Items: download, verify, place, ack.
  std::string lastError;
  Error fatal = Error::None;
  const auto noteItemError = [&](const Error e, const int64_t id) {
    ++out.failed;
    if (out.error == Error::None) out.error = e;
    if (lastError.empty()) lastError = std::string(errorCode(e)) + " item " + std::to_string(id);
  };
  Progress progress;
  progress.itemCount = static_cast<uint16_t>(queue.items.size());
  bool libraryChanged = false;
  for (size_t i = 0; i < queue.items.size() && fatal == Error::None; ++i) {
    if (itemBudgetMs > 0 && millis() - started > itemBudgetMs) {
      LOG_INF(kTag, "Item budget used; %u items left for the next sync", static_cast<unsigned>(queue.items.size() - i));
      break;
    }
    if (cancelled(cb)) {
      fatal = Error::Cancelled;
      break;
    }
    const QueueItem& item = queue.items[i];
    progress.itemIndex = static_cast<uint16_t>(i + 1);
    progress.bytes = 0;
    progress.total = static_cast<size_t>(item.size);
    if (cb.onProgress) cb.onProgress(cb.ctx, progress);

    // Already on the card from an earlier sync whose ack did not arrive:
    // ack it again instead of downloading a duplicate.
    const DeliveredFile* have = manifest.findBySha(item.sha256);
    const bool alreadyHere = have && !contains(modified, have->path) && Storage.exists(have->path.c_str());
    bool downloaded = false;
    std::string path;
    if (alreadyHere) {
      path = have->path;
    } else {
      session->close();
      const Error e = downloadItem(*session, item, cb, progress);
      if (isFatal(e)) {
        fatal = e;
        break;
      }
      if (e != Error::None) {
        noteItemError(e, item.id);
        continue;
      }
      if (!placeFile(item, manifest, modified, path)) {
        Storage.remove(kPartPath);
        noteItemError(Error::SdCard, item.id);
        continue;
      }
      if (!manifest.add(DeliveredFile{item.sha256, path, item.id}) || !saveManifest(manifest)) {
        LOG_ERR(kTag, "Manifest not updated for %s", path.c_str());
      }
      clearBookCache(path);
      libraryChanged = true;
      downloaded = true;
      writtenBytes += item.size > 0 ? item.size : 0;
    }

    const std::string ack = buildAckJson(item.id, item.sha256);
    status = session->call("POST", "/api/device/ack", &ack, nullptr, 0);
    const Error e = callError(status);
    if (status == 409) {
      // The item changed on the server: drop this copy; it is re-queued.
      Storage.remove(path.c_str());
      manifest.removePath(path);
      saveManifest(manifest);
      noteItemError(Error::Hash, item.id);
    } else if (isFatal(e)) {
      fatal = e;
    } else if (e != Error::None) {
      noteItemError(e, item.id);
    } else if (downloaded) {
      ++out.delivered;
    }
  }

  // 5. Mirror deletes: only server-delivered, unmodified files, re-hashed now.
  if (fatal == Error::None) {
    bool changed = false;
    for (const std::string& sha : queue.deletes) {
      std::vector<DeliveredFile> matches;
      std::copy_if(manifest.files.begin(), manifest.files.end(), std::back_inserter(matches),
                   [&sha](const auto& f) { return f.sha256 == sha; });
      for (const auto& f : matches) {
        std::string current;
        if (contains(modified, f.path) || !hashFile(f.path, hashBuf.get(), current) || !shouldDelete(f, sha, current)) {
          continue;
        }
        if (!Storage.remove(f.path.c_str())) {
          noteItemError(Error::SdCard, f.itemId);
          continue;
        }
        clearBookCache(f.path);
        manifest.removePath(f.path);
        ++out.deleted;
        changed = true;
      }
    }
    if (changed) {
      saveManifest(manifest);
      libraryChanged = true;
    }
  }
  if (libraryChanged) library::markLibraryIndexDirty();

  if (fatal != Error::None) out.error = fatal;

  // 6. Closing status report (skipped when the server already refused us).
  if (out.error != Error::Unauthorized && out.error != Error::Protocol && out.error != Error::Cancelled) {
    // Adjust the cached value by what this sync wrote instead of rescanning.
    const freespace::Cache cache = loadFreeCache();
    if (cache.freeBytes >= 0) {
      const freespace::Cache adjusted = freespace::afterWrites(cache, writtenBytes);
      if (writtenBytes > 0) storeFreeCache(adjusted);
      report.freeSdBytes = adjusted.freeBytes > 0 ? adjusted.freeBytes : -1;
    } else if (report.freeSdBytes > 0) {
      // No trusted clock, so nothing was cached: adjust this sync's own reading.
      report.freeSdBytes = report.freeSdBytes > writtenBytes ? report.freeSdBytes - writtenBytes : -1;
    }
    report.lastSyncResult = out.error == Error::None ? "ok" : "error";
    report.lastError = lastError.empty() && out.error != Error::None ? errorCode(out.error) : lastError;
    report.inventory = inventoryOf(manifest, modified);
    if (canGrow(report.inventory.size() * 100 + 256)) {
      const std::string body = buildStatusJson(report);
      status = session->call("POST", "/api/device/status", &body, nullptr, 0);
      if (status < 200 || status >= 300) LOG_ERR(kTag, "Closing status report: %d", status);
    }
  }
  session->close();

  LOG_INF(kTag, "Sync done: %s, %u new, %u deleted, %u skipped, %u failed", errorCode(out.error), out.delivered,
          out.deleted, out.skipped, out.failed);
  persistResult(out);
  return out;
}

void onStationJoined(GfxRenderer& renderer) {
  if (joinHookSuppressed) return;
  // Never while a book is open: the reader may be under this Wi-Fi screen.
  if (activityManager.isReaderActivity()) return;
  if (config::getDeviceKey().empty()) return;
  // Opportunistic: skip quietly (no error recorded) when the heap is short.
  if (!heapOk(false)) return;
  {
    RenderLock lock;
    GUI.drawPopup(renderer, tr(STR_XTEINK_SYNCING));
  }
  run(&renderer, Callbacks{}, kJoinItemBudgetMs);
}

void setJoinHookSuppressed(const bool suppressed) { joinHookSuppressed = suppressed; }

bool statusLine(char* buf, const size_t len) {
  const StatusTemplates t{tr(STR_XTEINK_SYNCED_TIME_NEW), tr(STR_XTEINK_SYNCED_NEW), tr(STR_XTEINK_SYNCED_TIME),
                          tr(STR_XTEINK_SYNCED), tr(STR_XTEINK_SYNC_ERROR)};
  return formatStatusLine(previousResult(), t, buf, len);
}

void drawHomeStatusLine(const GfxRenderer& renderer, const int contentBottom) {
  char line[64];
  if (!statusLine(line, sizeof(line))) return;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int y = renderer.getScreenHeight() - metrics.buttonHintsHeight - renderer.getLineHeight(SMALL_FONT_ID) - 6;
  // No free band above the hints (long menu, landscape): skip, never overlap.
  if (y < contentBottom) return;
  renderer.drawCenteredText(SMALL_FONT_ID, y, line);
}

}  // namespace xteink::sync
