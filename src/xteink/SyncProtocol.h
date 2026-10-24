#pragma once
// xteink device protocol v1 (docs/device-protocol.md in the xteink repo): the
// pure half of the sync client. JSON build/parse, server order, delivered-file
// manifest bookkeeping, delete decisions, error codes and the home status line.
// No Arduino or SD dependencies, so it is unit-tested on the host
// (test/xteink_sync). The device half is XteinkSync.cpp.
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace xteink::sync {

constexpr const char* kProtocolHeader = "X-Xteink-Protocol";
constexpr const char* kProtocolVersion = "1";
constexpr const char* kDefaultLanUrl = "http://xteink.local:8781";
constexpr const char* kDefaultTunnelUrl = "https://xteink.culture.dev";

// Caps that keep one sync's heap use bounded. Items beyond kMaxItems stay
// queued on the server and arrive on the next sync.
constexpr size_t kMaxItems = 32;
constexpr size_t kMaxQueueBody = 24 * 1024;
constexpr size_t kMaxManifestEntries = 300;
constexpr size_t kMaxManifestBytes = 64 * 1024;
constexpr size_t kMaxTitleLen = 200;

// Stable codes shown on the home screen ("Sync error E401") and sent to the
// server in last_error.
enum class Error : uint8_t {
  None,
  NoKey,         // ENOKEY: no device key provisioned
  Network,       // ENET: no server answered
  LowMemory,     // ELOWMEM: not enough heap for the connection
  Unauthorized,  // E401: key missing, rotated or revoked: re-pair
  Protocol,      // E426: server does not speak protocol v1: update firmware
  BadRequest,    // E422: server rejected our body (firmware bug)
  Http,          // EHTTP: other unexpected HTTP status
  BadResponse,   // EPARSE: response JSON did not parse or validate
  SdCard,        // ESD: SD read/write/rename failed
  Hash,          // ESHA: downloaded bytes did not match the queued sha256
  Download,      // EDL: download did not complete
  Cancelled,     // ECANCEL: stopped by the user or the time budget
  WrongDevice,   // EDEV: firmware image built for another chip or board
  Flash,         // EFLASH: OTA partition begin/write/end/boot switch failed
};

const char* errorCode(Error e);

// HTTP status of a protocol call -> error. 2xx -> None; a negative status
// (transport failure) -> Network.
Error errorForStatus(int httpStatus);

// True for errors that must stop the whole sync (the next request would fail
// the same way).
bool isFatal(Error e);

// --- server order --------------------------------------------------------

// Origins to try, in order (decision c60): the provisioned LAN URL, or
// kDefaultLanUrl when none is set, then the provisioned tunnel URL, or
// kDefaultTunnelUrl. Each is normalized ("scheme://host[:port]", lowercase
// scheme and host, no path or trailing slash); unusable or duplicate entries
// are dropped. Tunnel origins must be https.
std::vector<std::string> serverOrder(const std::string& lanUrl, const std::string& tunnelUrl);

// "scheme://host[:port]" for an http(s) URL, or "" when unusable.
std::string normalizeOrigin(const std::string& url);

bool isHttps(const std::string& origin);

// True when a non-zero budget started at startMs has run out at nowMs
// (millis() values; unsigned subtraction keeps it right across the 49-day
// wrap). A zero budget never expires. Bounds the whole LAN probe (risk r21).
constexpr bool budgetExpired(const uint32_t nowMs, const uint32_t startMs, const uint32_t budgetMs) {
  return budgetMs > 0 && nowMs - startMs >= budgetMs;
}

// The download url from the queue must be a root-relative path on the same
// server ("/api/device/items/42"). Rejects absolute and protocol-relative
// URLs, so the device key is never sent to another host.
bool isRootRelativePath(const std::string& path);

// --- status report -------------------------------------------------------

struct InventoryEntry {
  std::string sha256;
  bool unmodified = false;
};

// Daily-routine fields (src/xteink/DailyRoutine.h). Every field is optional:
// "" = omit, so a manual "Sync now" never sends stale run fields and the
// server keeps the values of the last run.
struct RunFields {
  std::string startedAt;       // run_started_at, ISO-8601 UTC ("2026-10-11T03:00:05Z")
  std::string trigger;         // run_trigger: "power_on" | "wake" | "manual"
  std::string fwOutcome;       // fw_outcome: none | installed | failed:<x> | deferred:<x>
  std::string contentOutcome;  // content_outcome: free text ("ok", "error:ENET", ...)
};

struct StatusReport {
  // Negative = omit (free space unknown).
  int64_t freeSdBytes = -1;
  std::string firmwareVersion;
  std::string lastError;       // "" = null
  std::string lastSyncResult;  // "" = null
  std::vector<InventoryEntry> inventory;
  // Image the device rolled back from after a failed post-update health check
  // (OtaHealth.h), in the firmware_version form. "" = omit the field.
  std::string rolledBackVersion;
  RunFields run;
};

std::string buildStatusJson(const StatusReport& report);

// --- queue ---------------------------------------------------------------

// What the server says an item is. Decides the SD folder it lands in. An
// absent, unknown or non-string "kind" is Book (an older server sends none).
enum class ItemKind : uint8_t { Book, Article, Transcript, Newspaper };

// "book" | "article" | "transcript" | "newspaper" (case-insensitive) -> kind;
// anything else -> Book.
ItemKind parseItemKind(const std::string& s);

// SD folder (no trailing slash) for a kind: "/xteink" for Book,
// "/xteink/articles", "/xteink/transcripts", "/xteink/newspapers". An
// out-of-range value gets the Book folder.
const char* kindFolder(ItemKind kind);

struct QueueItem {
  int64_t id = 0;
  ItemKind kind = ItemKind::Book;
  std::string title;
  int64_t size = 0;
  std::string sha256;
  std::string format;
  std::string url;
};

struct SkippedItem {
  int64_t id = 0;
  std::string reason;
};

struct Queue {
  std::vector<QueueItem> items;
  std::vector<SkippedItem> skipped;
  std::vector<std::string> deletes;  // sha256 values
  size_t droppedItems = 0;           // malformed or over kMaxItems
};

// Parses a GET /api/device/queue body. False when the body is not a protocol
// v1 queue object. Individual malformed items are dropped (droppedItems), not
// fatal, so one bad entry cannot block the rest of the queue.
bool parseQueue(const char* json, size_t len, Queue& out);

std::string buildAckJson(int64_t itemId, const std::string& sha256);

// Lowercase 64-hex sha256.
bool isSha256Hex(const std::string& s);

// Lowercase hex of a 32-byte digest.
std::string toHex(const uint8_t* digest, size_t len);

// --- delivered-file manifest ----------------------------------------------

// Files the server delivered: the only files ever reported in the inventory
// or deleted on the server's request. Sideloaded files are never in here.
struct DeliveredFile {
  std::string sha256;
  std::string path;
  int64_t itemId = 0;
};

struct Manifest {
  std::vector<DeliveredFile> files;

  const DeliveredFile* findBySha(const std::string& sha256) const;
  // Replaces any entry with the same path.
  bool add(const DeliveredFile& f);
  bool removePath(const std::string& path);
};

bool parseManifest(const char* json, size_t len, Manifest& out);
std::string buildManifestJson(const Manifest& m);

// A delete instruction applies to a file only when the server delivered it
// (it is in the manifest under that sha256) and its current hash still equals
// that sha256, i.e. the user has not modified it.
bool shouldDelete(const DeliveredFile& file, const std::string& deleteSha, const std::string& currentSha);

// --- downloads -----------------------------------------------------------

// Byte offset to resume a download from, given a partial file of partSize
// bytes. A partial that is not smaller than the item restarts from 0 (a range
// starting at or past the end would get 416).
size_t resumeOffset(size_t partSize, int64_t itemSize);

// "<sanitized title>.<format>"; falls back to "book-<id>" for an empty title.
// `format` must be a short lowercase alphanumeric extension, else "bin".
std::string bookFileName(const std::string& title, int64_t id, const std::string& format);

// Second-choice name when bookFileName() is taken: "<title> (<id>).<format>".
std::string bookFileNameWithId(const std::string& title, int64_t id, const std::string& format);

// --- persisted result and home status line --------------------------------

struct LastResult {
  bool valid = false;  // false = never synced
  bool ok = false;
  uint16_t newCount = 0;
  std::string code;  // error code when !ok
  std::string time;  // "HH:MM" local time of the sync, "" when unknown
};

// Compact NVS string: "ok|<n>|<time>" or "err|<code>|<time>".
std::string encodeLastResult(const LastResult& r);
LastResult decodeLastResult(const std::string& s);

// UI templates (from tr()) so the format logic stays host-testable:
//   okNew      "Synced %s · %d new"   okNewNoTime  "Synced · %d new"
//   ok         "Synced %s"            okNoTime     "Synced"
//   error      "Sync error %s"
struct StatusTemplates {
  const char* okNew;
  const char* okNewNoTime;
  const char* ok;
  const char* okNoTime;
  const char* error;
};

// Writes the one-line status into buf. Returns false (buf = "") when there is
// nothing to show (never synced).
bool formatStatusLine(const LastResult& r, const StatusTemplates& t, char* buf, size_t len);

// --- firmware offer (OTA from the xteink server) ---------------------------
//
// The POST /api/device/status response may carry an optional offer:
//   "firmware": {"version": str, "size": int, "sha256": hex, "url": "/api/..."}
// Absent or null means no offer ("none"); an issue-1 server never sends one.
// The install decision belongs to the offer: any build other than the running
// one is installed, older builds included (no version ordering).

constexpr size_t kMaxStatusResponse = 8 * 1024;
constexpr size_t kMaxFirmwareVersionLen = 96;

struct FirmwareOffer {
  bool present = false;
  std::string version;
  int64_t size = 0;
  std::string sha256;  // lowercase hex
  std::string url;     // root-relative path on the origin that answered
};

// Parses a POST /api/device/status response body. False when the body is not
// a JSON object (EPARSE). A missing, null or malformed "firmware" field is not
// an error: `offer.present` stays false.
bool parseStatusResponse(const char* json, size_t len, FirmwareOffer& offer);

// Status body for an offer check: only firmware_version, plus
// rolled_back_version when non-empty. Every status field is optional, so the
// stored inventory, last_error and last_sync_result are kept.
std::string buildFirmwareCheckJson(const std::string& firmwareVersion, const std::string& rolledBackVersion = "");

// Offered and running versions name the same build. A "xteink-fw " prefix (the
// form the status report sends) is ignored on either side.
bool sameFirmwareVersion(const std::string& offered, const std::string& running);

// `version` in the status report's form: "xteink-fw <version>" (an existing
// prefix is not doubled).
std::string firmwareReportVersion(const std::string& version);

// --- builds this device rolled back from -----------------------------------
//
// A persistent device-side record (NVS "rb_list") of the last few builds whose
// post-update health check failed or that the bootloader aborted. Unlike
// rolled_back_version (cleared once a status POST carried it), it is never
// cleared by the server, so an offer naming such a build is never installed
// again unattended. Stored as the versions in report form, newest first, one
// per line.
constexpr size_t kMaxRolledBackBuilds = 4;

// True when `stored` lists the build `version` names (prefix-insensitive).
bool rolledBackListContains(const std::string& stored, const std::string& version);

// `stored` with `version` (report form) moved to the front, keeping at most
// kMaxRolledBackBuilds entries. Empty or over-long versions leave it as is.
std::string rolledBackListAdd(const std::string& stored, const std::string& version);

enum class OtaDecision : uint8_t {
  None,        // no offer
  Current,     // the offer names the running build: nothing to do
  Install,     // the offer names another build (newer or older)
  Deferred,    // only reachable over the tunnel, and tunnel OTA is off
  RolledBack,  // this device already rolled back from the offered build
};

// "none", "current", "install", "deferred:tunnel", "deferred:rolled_back".
const char* otaDecisionName(OtaDecision d);

// viaTunnel: the offer came from the tunnel origin (not the LAN one).
// tunnelOtaEnabled: the XTEINK_OTA_TUNNEL build flag (decision c32).
// rolledBackList: the persistent rolled-back record (rolledBackListAdd form).
OtaDecision decideOta(const FirmwareOffer& offer, const std::string& runningVersion, bool viaTunnel,
                      bool tunnelOtaEnabled, const std::string& rolledBackList = "");

// True when `origin` is the LAN origin serverOrder() derives from `lanUrl`
// (kDefaultLanUrl when empty).
bool isLanOrigin(const std::string& origin, const std::string& lanUrl);

// Seam between the install control flow below and the device: HTTP, the
// ESP-IDF OTA partition calls, sha256, and the chip-id/board-tag checks. The
// device implementation lives in src/network/OtaUpdater.cpp; host tests fake
// it and count calls.
struct OtaFetchResult {
  int status = -1;        // HTTP status; -1 on transport failure
  bool complete = false;  // the whole body arrived
  bool aborted = false;   // cancelled by the caller
};

struct OtaSink {
  // Body bytes in order. Return false to stop the transfer.
  std::function<bool(const uint8_t* data, size_t len)> write;
  // The server restarted the body from byte 0 (ignored a Range resume): start
  // over. Return false to stop the transfer.
  std::function<bool()> rewind;
};

class OtaPort {
 public:
  virtual ~OtaPort() = default;
  // GET the offer's image, streaming the body into `sink`.
  virtual OtaFetchResult fetch(const OtaSink& sink) = 0;
  virtual bool otaBegin() = 0;                                 // esp_ota_begin (inactive slot)
  virtual bool otaWrite(const uint8_t* data, size_t len) = 0;  // esp_ota_write
  virtual bool otaEnd() = 0;                                   // esp_ota_end (ESP image check)
  virtual void otaAbort() = 0;                                 // esp_ota_abort
  virtual bool setBootPartition() = 0;                         // esp_ota_set_boot_partition
  // Rollback bookkeeping around the boot switch: recordCandidate() runs right
  // before setBootPartition() (NVS "rb_cand" = the offered build), so an image
  // that crashes before its own health check still gets reported by the old
  // image after the bootloader rolls back; clearCandidate() undoes it when the
  // switch fails.
  virtual void recordCandidate() {}
  virtual void clearCandidate() {}
  virtual void hashReset() = 0;
  virtual void hashUpdate(const uint8_t* data, size_t len) = 0;
  virtual std::string hashHex() = 0;
  // Device identity over the stream (chip id, board tag). feed() returns false
  // once the image is known to be for another device.
  virtual void guardReset() = 0;
  virtual bool guardFeed(const uint8_t* data, size_t len) = 0;
  virtual void progress(size_t written, size_t total) {
    (void)written;
    (void)total;
  }
};

// Streams the offered image into the inactive OTA slot, hashing exactly the
// bytes written, and switches the boot partition only when the size and the
// sha256 match the offer and esp_ota_end accepted the image (recording the
// candidate first). Any failure aborts the slot, so the running firmware stays
// the boot target. Returns the error code to record (Error::None on success).
Error installOffer(const FirmwareOffer& offer, OtaPort& port);

}  // namespace xteink::sync
