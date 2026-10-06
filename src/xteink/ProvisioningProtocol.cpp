#include "ProvisioningProtocol.h"

#include <ArduinoJson.h>

#include <cstdint>
#include <cstring>

namespace xteink::prov {

namespace {

int b64Value(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

// Standard base64 with padding, decoded in place (output never exceeds input).
// Returns the decoded length or -1 on any malformed input.
int decodeBase64InPlace(char* s, size_t len) {
  if (len == 0 || len % 4 != 0) return -1;
  size_t out = 0;
  for (size_t i = 0; i < len; i += 4) {
    int v[4];
    int pad = 0;
    for (int j = 0; j < 4; j++) {
      const char c = s[i + j];
      if (c == '=') {
        // Padding only in the last quantum, last one or two positions.
        if (i + 4 != len || j < 2) return -1;
        pad++;
        v[j] = 0;
      } else {
        if (pad > 0) return -1;
        v[j] = b64Value(c);
        if (v[j] < 0) return -1;
      }
    }
    const uint32_t triple = (static_cast<uint32_t>(v[0]) << 18) | (static_cast<uint32_t>(v[1]) << 12) |
                            (static_cast<uint32_t>(v[2]) << 6) | static_cast<uint32_t>(v[3]);
    s[out++] = static_cast<char>((triple >> 16) & 0xFF);
    if (pad < 2) s[out++] = static_cast<char>((triple >> 8) & 0xFF);
    if (pad < 1) s[out++] = static_cast<char>(triple & 0xFF);
  }
  return static_cast<int>(out);
}

std::string encodeBase64(const std::string& in) {
  static constexpr char kTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((in.size() + 2) / 3 * 4);
  size_t i = 0;
  for (; i + 2 < in.size(); i += 3) {
    const uint32_t t = (static_cast<uint8_t>(in[i]) << 16) | (static_cast<uint8_t>(in[i + 1]) << 8) |
                       static_cast<uint8_t>(in[i + 2]);
    out += kTable[(t >> 18) & 63];
    out += kTable[(t >> 12) & 63];
    out += kTable[(t >> 6) & 63];
    out += kTable[t & 63];
  }
  if (i + 1 == in.size()) {
    const uint32_t t = static_cast<uint8_t>(in[i]) << 16;
    out += kTable[(t >> 18) & 63];
    out += kTable[(t >> 12) & 63];
    out += "==";
  } else if (i + 2 == in.size()) {
    const uint32_t t = (static_cast<uint8_t>(in[i]) << 16) | (static_cast<uint8_t>(in[i + 1]) << 8);
    out += kTable[(t >> 18) & 63];
    out += kTable[(t >> 12) & 63];
    out += kTable[(t >> 6) & 63];
    out += '=';
  }
  return out;
}

// Reads a string field. Absent -> found=false. Wrong type, embedded NUL, or
// longer than maxLen -> invalid=true.
void readString(JsonVariantConst v, size_t maxLen, std::string& out, bool& found, bool& invalid) {
  found = false;
  if (v.isNull()) return;
  if (!v.is<JsonString>()) {
    invalid = true;
    return;
  }
  const JsonString s = v.as<JsonString>();
  if (s.size() > maxLen || std::strlen(s.c_str()) != s.size()) {
    invalid = true;
    return;
  }
  out.assign(s.c_str(), s.size());
  found = true;
}

bool hasControlChars(const std::string& s) {
  for (const char c : s) {
    if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) return true;
  }
  return false;
}

bool startsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

bool validUrl(const std::string& u, bool httpsOnly) {
  if (u.empty()) return true;  // empty clears
  if (hasControlChars(u) || u.find(' ') != std::string::npos) return false;
  if (httpsOnly) return startsWith(u, "https://") && u.size() > 8;
  return (startsWith(u, "https://") && u.size() > 8) || (startsWith(u, "http://") && u.size() > 7);
}

// "xtd_" + 8 hex + "_" + secret of base64url-ish characters.
bool validDeviceKey(const std::string& k) {
  if (k.empty()) return true;  // empty clears
  if (k.size() > kMaxKeyLen || k.size() < 15) return false;
  if (!startsWith(k, "xtd_")) return false;
  for (size_t i = 4; i < 12; i++) {
    const char c = k[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  if (k[12] != '_') return false;
  for (size_t i = 13; i < k.size(); i++) {
    const char c = k[i];
    const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-' || c == '_';
    if (!ok) return false;
  }
  return true;
}

}  // namespace

const char* errorCode(Error e) {
  switch (e) {
    case Error::None:
      return "ok";
    case Error::NotInProvisioningMode:
      return "not_in_provisioning_mode";
    case Error::BadFormat:
      return "bad_format";
    case Error::UnsupportedVersion:
      return "unsupported_version";
    case Error::TooLong:
      return "too_long";
    case Error::BadBase64:
      return "bad_base64";
    case Error::BadJson:
      return "bad_json";
    case Error::InvalidField:
      return "invalid_field";
    case Error::TooManyNetworks:
      return "too_many_networks";
    case Error::StorageError:
      return "storage_error";
  }
  return "bad_format";
}

bool isProvisioningLine(const char* line, size_t len) {
  const size_t n = std::strlen(kRequestPrefix);
  return len > n && std::memcmp(line, kRequestPrefix, n) == 0 && line[n] == ' ';
}

Error parseLine(char* line, size_t len, Request& out) {
  if (len > kMaxLineLen) return Error::TooLong;
  if (len > 0 && line[len - 1] == '\r') len--;
  if (!isProvisioningLine(line, len)) return Error::BadFormat;

  // "XTEINK-PROV <version> <payload>"
  size_t pos = std::strlen(kRequestPrefix) + 1;
  size_t verStart = pos;
  while (pos < len && line[pos] >= '0' && line[pos] <= '9') pos++;
  if (pos == verStart || pos - verStart > 4 || pos >= len || line[pos] != ' ') return Error::BadFormat;
  int version = 0;
  for (size_t i = verStart; i < pos; i++) version = version * 10 + (line[i] - '0');
  if (version != kProtocolVersion) return Error::UnsupportedVersion;
  pos++;

  char* payload = line + pos;
  const size_t payloadLen = len - pos;
  if (payloadLen == 0) return Error::BadFormat;
  const int jsonLen = decodeBase64InPlace(payload, payloadLen);
  if (jsonLen < 0) return Error::BadBase64;
  if (static_cast<size_t>(jsonLen) > kMaxJsonLen) return Error::TooLong;

  JsonDocument doc;
  const DeserializationError err =
      deserializeJson(doc, payload, static_cast<size_t>(jsonLen), DeserializationOption::NestingLimit(4));
  if (err || !doc.is<JsonObjectConst>()) return Error::BadJson;

  Request req;
  bool found = false;
  bool invalid = false;

  const JsonVariantConst replace = doc["replace_networks"];
  if (!replace.isNull()) {
    if (!replace.is<bool>()) return Error::InvalidField;
    req.replaceNetworks = replace.as<bool>();
  }

  const JsonVariantConst nets = doc["networks"];
  if (!nets.isNull()) {
    if (!nets.is<JsonArrayConst>()) return Error::InvalidField;
    const JsonArrayConst arr = nets.as<JsonArrayConst>();
    if (arr.size() > kMaxNetworks) return Error::TooManyNetworks;
    req.networks.reserve(arr.size());
    for (const JsonVariantConst n : arr) {
      if (!n.is<JsonObjectConst>()) return Error::InvalidField;
      Network net;
      readString(n["ssid"], kMaxSsidLen, net.ssid, found, invalid);
      if (invalid || !found || net.ssid.empty()) return Error::InvalidField;
      readString(n["password"], kMaxPasswordLen, net.password, found, invalid);
      if (invalid) return Error::InvalidField;
      for (const auto& prev : req.networks) {
        if (prev.ssid == net.ssid) return Error::InvalidField;  // duplicate SSID
      }
      req.networks.push_back(std::move(net));
    }
  }

  std::string s;
  readString(doc["lan_url"], kMaxUrlLen, s, found, invalid);
  if (invalid || (found && !validUrl(s, false))) return Error::InvalidField;
  if (found) req.lanUrl = s;

  readString(doc["tunnel_url"], kMaxUrlLen, s, found, invalid);
  if (invalid || (found && !validUrl(s, true))) return Error::InvalidField;
  if (found) req.tunnelUrl = s;

  readString(doc["device_key"], kMaxKeyLen, s, found, invalid);
  if (invalid || (found && !validDeviceKey(s))) return Error::InvalidField;
  if (found) req.deviceKey = s;

  out = std::move(req);
  return Error::None;
}

std::string keyIdOf(const std::string& deviceKey) {
  if (!validDeviceKey(deviceKey) || deviceKey.empty()) return "";
  return deviceKey.substr(4, 8);
}

std::string buildAckLine(const std::string& mac, const std::string& firmwareVersion, size_t networksSaved,
                         const std::string& keyId) {
  JsonDocument doc;
  doc["mac"] = mac;
  doc["firmware_version"] = firmwareVersion;
  doc["networks_saved"] = networksSaved;
  doc["key_id"] = keyId;
  std::string json;
  serializeJson(doc, json);
  return std::string(kAckPrefix) + " " + std::to_string(kProtocolVersion) + " " + encodeBase64(json) + "\n";
}

std::string buildErrLine(Error e) {
  return std::string(kErrPrefix) + " " + std::to_string(kProtocolVersion) + " " + errorCode(e) + "\n";
}

}  // namespace xteink::prov
