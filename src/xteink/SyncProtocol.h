#pragma once
// xteink device protocol v1 (docs/device-protocol.md in the xteink repo): the
// pure half of the sync client. JSON build/parse, server order, delivered-file
// manifest bookkeeping, delete decisions, error codes and the home status line.
// No Arduino or SD dependencies, so it is unit-tested on the host
// (test/xteink_sync). The device half is XteinkSync.cpp.
#include <cstddef>
#include <cstdint>
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

// The download url from the queue must be a root-relative path on the same
// server ("/api/device/items/42"). Rejects absolute and protocol-relative
// URLs, so the device key is never sent to another host.
bool isRootRelativePath(const std::string& path);

// --- status report -------------------------------------------------------

struct InventoryEntry {
  std::string sha256;
  bool unmodified = false;
};

struct StatusReport {
  // Negative = omit (free space unknown).
  int64_t freeSdBytes = -1;
  std::string firmwareVersion;
  std::string lastError;       // "" = null
  std::string lastSyncResult;  // "" = null
  std::vector<InventoryEntry> inventory;
};

std::string buildStatusJson(const StatusReport& report);

// --- queue ---------------------------------------------------------------

struct QueueItem {
  int64_t id = 0;
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

}  // namespace xteink::sync
