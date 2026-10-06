#include "SyncProtocol.h"

#include <ArduinoJson.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

#include "util/StringUtils.h"

namespace xteink::sync {

namespace {

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

bool isFormatExtension(const std::string& f) {
  if (f.empty() || f.size() > 5) return false;
  for (const char c : f) {
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) return false;
  }
  return true;
}

std::string idString(const int64_t id) {
  char buf[24];
  snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(id));
  return buf;
}

}  // namespace

const char* errorCode(const Error e) {
  switch (e) {
    case Error::None:
      return "OK";
    case Error::NoKey:
      return "ENOKEY";
    case Error::Network:
      return "ENET";
    case Error::LowMemory:
      return "ELOWMEM";
    case Error::Unauthorized:
      return "E401";
    case Error::Protocol:
      return "E426";
    case Error::BadRequest:
      return "E422";
    case Error::Http:
      return "EHTTP";
    case Error::BadResponse:
      return "EPARSE";
    case Error::SdCard:
      return "ESD";
    case Error::Hash:
      return "ESHA";
    case Error::Download:
      return "EDL";
    case Error::Cancelled:
      return "ECANCEL";
  }
  return "EHTTP";
}

Error errorForStatus(const int httpStatus) {
  if (httpStatus < 0) return Error::Network;
  if (httpStatus >= 200 && httpStatus < 300) return Error::None;
  switch (httpStatus) {
    case 401:
    case 403:
      return Error::Unauthorized;
    case 426:
      return Error::Protocol;
    case 422:
      return Error::BadRequest;
    default:
      return Error::Http;
  }
}

bool isFatal(const Error e) {
  switch (e) {
    case Error::NoKey:
    case Error::Network:
    case Error::LowMemory:
    case Error::Unauthorized:
    case Error::Protocol:
    case Error::Cancelled:
      return true;
    default:
      return false;
  }
}

// --- server order --------------------------------------------------------

std::string normalizeOrigin(const std::string& url) {
  const size_t schemeEnd = url.find("://");
  if (schemeEnd == std::string::npos) return "";
  const std::string scheme = lower(url.substr(0, schemeEnd));
  if (scheme != "http" && scheme != "https") return "";
  const size_t hostStart = schemeEnd + 3;
  const size_t hostEnd = url.find_first_of("/?#", hostStart);
  std::string authority = url.substr(hostStart, hostEnd == std::string::npos ? std::string::npos : hostEnd - hostStart);
  // Userinfo would be sent nowhere useful and hides the real host; refuse it.
  if (authority.empty() || authority.find('@') != std::string::npos) return "";
  for (const char c : authority) {
    if (std::isspace(static_cast<unsigned char>(c))) return "";
  }
  authority = lower(authority);
  const size_t colon = authority.rfind(':');
  if (colon != std::string::npos) {
    const std::string port = authority.substr(colon + 1);
    if (colon == 0 || port.empty() || port.size() > 5) return "";
    for (const char c : port) {
      if (c < '0' || c > '9') return "";
    }
    const long p = std::strtol(port.c_str(), nullptr, 10);
    if (p <= 0 || p > 65535) return "";
    // Drop the scheme's default port so equal origins compare equal.
    if ((scheme == "http" && p == 80) || (scheme == "https" && p == 443)) authority.erase(colon);
  }
  return scheme + "://" + authority;
}

bool isHttps(const std::string& origin) { return origin.rfind("https://", 0) == 0; }

std::vector<std::string> serverOrder(const std::string& lanUrl, const std::string& tunnelUrl) {
  std::vector<std::string> out;
  out.reserve(2);
  const std::string lan = normalizeOrigin(lanUrl.empty() ? kDefaultLanUrl : lanUrl);
  if (!lan.empty()) out.push_back(lan);
  // The tunnel crosses the internet: plain http there would expose the key.
  const std::string tunnel = normalizeOrigin(tunnelUrl.empty() ? kDefaultTunnelUrl : tunnelUrl);
  if (!tunnel.empty() && isHttps(tunnel) && tunnel != lan) out.push_back(tunnel);
  return out;
}

bool isRootRelativePath(const std::string& path) {
  if (path.size() < 2 || path[0] != '/' || path[1] == '/' || path[1] == '\\') return false;
  if (path.find("://") != std::string::npos) return false;
  for (const char c : path) {
    if (static_cast<unsigned char>(c) <= 0x20 || c == 0x7f || c == '\\') return false;
  }
  return true;
}

// --- status / ack ----------------------------------------------------------

std::string buildStatusJson(const StatusReport& report) {
  JsonDocument doc;
  if (report.freeSdBytes >= 0) doc["free_sd_bytes"] = report.freeSdBytes;
  doc["firmware_version"] = report.firmwareVersion;
  if (report.lastError.empty()) {
    doc["last_error"] = nullptr;
  } else {
    doc["last_error"] = report.lastError;
  }
  if (report.lastSyncResult.empty()) {
    doc["last_sync_result"] = nullptr;
  } else {
    doc["last_sync_result"] = report.lastSyncResult;
  }
  JsonArray inv = doc["inventory"].to<JsonArray>();
  for (const auto& e : report.inventory) {
    JsonObject o = inv.add<JsonObject>();
    o["sha256"] = e.sha256;
    o["unmodified"] = e.unmodified;
  }
  std::string out;
  serializeJson(doc, out);
  return out;
}

std::string buildAckJson(const int64_t itemId, const std::string& sha256) {
  JsonDocument doc;
  doc["item_id"] = itemId;
  doc["sha256"] = sha256;
  std::string out;
  serializeJson(doc, out);
  return out;
}

bool isSha256Hex(const std::string& s) {
  if (s.size() != 64) return false;
  for (const char c : s) {
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}

std::string toHex(const uint8_t* digest, const size_t len) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out;
  out.reserve(len * 2);
  for (size_t i = 0; i < len; ++i) {
    out += kHex[digest[i] >> 4];
    out += kHex[digest[i] & 0x0f];
  }
  return out;
}

// --- queue ---------------------------------------------------------------

bool parseQueue(const char* json, const size_t len, Queue& out) {
  out = Queue{};
  JsonDocument doc;
  if (deserializeJson(doc, json, len) != DeserializationError::Ok) return false;
  if (!doc.is<JsonObjectConst>()) return false;
  // The protocol field is optional for forward compatibility of the parser,
  // but when present it must say v1.
  if (!doc["protocol"].isNull() && doc["protocol"].as<std::string>() != kProtocolVersion) return false;
  if (!doc["items"].is<JsonArrayConst>()) return false;

  JsonArrayConst items = doc["items"].as<JsonArrayConst>();
  out.items.reserve(std::min(items.size(), kMaxItems));
  for (JsonObjectConst it : items) {
    if (out.items.size() >= kMaxItems) {
      ++out.droppedItems;
      continue;
    }
    QueueItem q;
    if (!it["id"].is<int64_t>() || !it["size"].is<int64_t>() || !it["sha256"].is<const char*>() ||
        !it["url"].is<const char*>()) {
      ++out.droppedItems;
      continue;
    }
    q.id = it["id"].as<int64_t>();
    q.size = it["size"].as<int64_t>();
    q.sha256 = it["sha256"].as<const char*>();
    q.url = it["url"].as<const char*>();
    q.format = lower(it["format"] | "");
    q.title = it["title"] | "";
    if (q.title.size() > kMaxTitleLen) q.title.resize(kMaxTitleLen);
    if (q.size < 0 || !isSha256Hex(q.sha256) || !isRootRelativePath(q.url)) {
      ++out.droppedItems;
      continue;
    }
    out.items.push_back(std::move(q));
  }

  if (doc["skipped"].is<JsonArrayConst>()) {
    JsonArrayConst skipped = doc["skipped"].as<JsonArrayConst>();
    out.skipped.reserve(std::min(skipped.size(), kMaxItems));
    for (JsonObjectConst s : skipped) {
      if (out.skipped.size() >= kMaxItems || !s["id"].is<int64_t>()) continue;
      out.skipped.push_back(SkippedItem{s["id"].as<int64_t>(), s["reason"] | ""});
    }
  }

  if (doc["deletes"].is<JsonArrayConst>()) {
    JsonArrayConst deletes = doc["deletes"].as<JsonArrayConst>();
    out.deletes.reserve(std::min(deletes.size(), kMaxManifestEntries));
    for (JsonObjectConst d : deletes) {
      if (out.deletes.size() >= kMaxManifestEntries) break;
      const std::string sha = d["sha256"] | "";
      if (isSha256Hex(sha)) out.deletes.push_back(sha);
    }
  }
  return true;
}

// --- manifest -------------------------------------------------------------

const DeliveredFile* Manifest::findBySha(const std::string& sha256) const {
  for (const auto& f : files) {
    if (f.sha256 == sha256) return &f;
  }
  return nullptr;
}

bool Manifest::add(const DeliveredFile& f) {
  for (auto& existing : files) {
    if (existing.path == f.path) {
      existing = f;
      return true;
    }
  }
  if (files.size() >= kMaxManifestEntries) return false;
  files.push_back(f);
  return true;
}

bool Manifest::removePath(const std::string& path) {
  for (auto it = files.begin(); it != files.end(); ++it) {
    if (it->path == path) {
      files.erase(it);
      return true;
    }
  }
  return false;
}

bool parseManifest(const char* json, const size_t len, Manifest& out) {
  out.files.clear();
  JsonDocument doc;
  if (deserializeJson(doc, json, len) != DeserializationError::Ok) return false;
  if ((doc["v"] | 0) != 1 || !doc["files"].is<JsonArrayConst>()) return false;
  JsonArrayConst files = doc["files"].as<JsonArrayConst>();
  out.files.reserve(std::min(files.size(), kMaxManifestEntries));
  for (JsonObjectConst f : files) {
    DeliveredFile d;
    d.sha256 = f["sha256"] | "";
    d.path = f["path"] | "";
    d.itemId = f["id"] | static_cast<int64_t>(0);
    if (!isSha256Hex(d.sha256) || d.path.empty() || d.path[0] != '/') continue;
    if (!out.add(d)) break;
  }
  return true;
}

std::string buildManifestJson(const Manifest& m) {
  JsonDocument doc;
  doc["v"] = 1;
  JsonArray files = doc["files"].to<JsonArray>();
  for (const auto& f : m.files) {
    JsonObject o = files.add<JsonObject>();
    o["sha256"] = f.sha256;
    o["path"] = f.path;
    o["id"] = f.itemId;
  }
  std::string out;
  serializeJson(doc, out);
  return out;
}

bool shouldDelete(const DeliveredFile& file, const std::string& deleteSha, const std::string& currentSha) {
  return isSha256Hex(deleteSha) && file.sha256 == deleteSha && currentSha == deleteSha;
}

// --- downloads -----------------------------------------------------------

size_t resumeOffset(const size_t partSize, const int64_t itemSize) {
  if (itemSize <= 0) return 0;
  return static_cast<int64_t>(partSize) < itemSize ? partSize : 0;
}

std::string bookFileName(const std::string& title, const int64_t id, const std::string& format) {
  const std::string ext = isFormatExtension(format) ? format : "bin";
  const std::string base = title.empty() ? "book-" + idString(id) : StringUtils::sanitizeFilename(title);
  return base + "." + ext;
}

std::string bookFileNameWithId(const std::string& title, const int64_t id, const std::string& format) {
  const std::string ext = isFormatExtension(format) ? format : "bin";
  if (title.empty()) return "book-" + idString(id) + "." + ext;
  return StringUtils::sanitizeFilename(title, 88) + " (" + idString(id) + ")." + ext;
}

// --- last result / status line --------------------------------------------

std::string encodeLastResult(const LastResult& r) {
  if (!r.valid) return "";
  std::string out = r.ok ? "ok|" + std::to_string(r.newCount) : "err|" + r.code;
  out += "|";
  out += r.time;
  return out;
}

LastResult decodeLastResult(const std::string& s) {
  LastResult r;
  const size_t a = s.find('|');
  if (a == std::string::npos) return r;
  const size_t b = s.find('|', a + 1);
  if (b == std::string::npos) return r;
  const std::string kind = s.substr(0, a);
  const std::string mid = s.substr(a + 1, b - a - 1);
  std::string time = s.substr(b + 1);
  if (time.size() > 8) time.clear();
  if (kind == "ok") {
    if (mid.empty() || mid.size() > 5) return r;
    for (const char c : mid) {
      if (c < '0' || c > '9') return r;
    }
    const long n = std::strtol(mid.c_str(), nullptr, 10);
    r.newCount = static_cast<uint16_t>(n > 65535 ? 65535 : n);
    r.ok = true;
  } else if (kind == "err") {
    if (mid.empty() || mid.size() > 12) return r;
    r.code = mid;
  } else {
    return r;
  }
  r.time = time;
  r.valid = true;
  return r;
}

bool formatStatusLine(const LastResult& r, const StatusTemplates& t, char* buf, const size_t len) {
  if (len == 0) return false;
  buf[0] = '\0';
  if (!r.valid) return false;
  const bool hasTime = !r.time.empty();
  if (!r.ok) {
    snprintf(buf, len, t.error, r.code.c_str());
  } else if (r.newCount > 0) {
    if (hasTime) {
      snprintf(buf, len, t.okNew, r.time.c_str(), static_cast<int>(r.newCount));
    } else {
      snprintf(buf, len, t.okNewNoTime, static_cast<int>(r.newCount));
    }
  } else if (hasTime) {
    snprintf(buf, len, t.ok, r.time.c_str());
  } else {
    snprintf(buf, len, "%s", t.okNoTime);
  }
  return true;
}

}  // namespace xteink::sync
