#pragma once
// xteink serial provisioning protocol v1: pure parsing/validation/encoding, no
// device dependencies, so it is unit-tested on the host (test/xteink_provisioning).
// Wire format: docs/provisioning.md.
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace xteink::prov {

constexpr int kProtocolVersion = 1;
constexpr const char* kRequestPrefix = "XTEINK-PROV";
constexpr const char* kAckPrefix = "XTEINK-PROV-ACK";
constexpr const char* kErrPrefix = "XTEINK-PROV-ERR";

// A whole request line (without the newline) is capped here. 8 networks at the
// worst case (32 B ssid + 64 B password + JSON framing, ~150 B each) plus two
// 200 B URLs and a 128 B key stay under 2 KB of JSON; base64 inflates by 4/3 to
// ~2.7 KB, so 3072 leaves headroom for the "XTEINK-PROV 1 " header.
constexpr size_t kMaxLineLen = 3072;
constexpr size_t kMaxJsonLen = 2048;

// Limits applied to decoded fields.
constexpr size_t kMaxNetworks = 8;  // mirrors WifiCredentialStore::MAX_NETWORKS
constexpr size_t kMaxSsidLen = 32;  // 802.11 SSID limit
constexpr size_t kMaxPasswordLen = 64;  // mirrors WifiCredentialStore::MAX_PASSWORD_LENGTH
constexpr size_t kMaxUrlLen = 200;
constexpr size_t kMaxKeyLen = 128;

enum class Error {
  None,
  NotInProvisioningMode,
  BadFormat,
  UnsupportedVersion,
  TooLong,
  BadBase64,
  BadJson,
  InvalidField,
  TooManyNetworks,
  StorageError,
};

// Stable wire code for an error (see docs/provisioning.md).
const char* errorCode(Error e);

struct Network {
  std::string ssid;
  std::string password;
};

struct Request {
  bool replaceNetworks = false;
  std::vector<Network> networks;
  // nullopt = leave unchanged; empty string = clear.
  std::optional<std::string> lanUrl;
  std::optional<std::string> tunnelUrl;
  std::optional<std::string> deviceKey;
};

// True when `line` starts with "XTEINK-PROV " (request marker). Cheap check used
// to route/refuse a line without parsing it.
bool isProvisioningLine(const char* line, size_t len);

// Parse one request line (no trailing newline; a trailing '\r' is tolerated).
// `line` is modified in place (the base64 payload is decoded over itself).
Error parseLine(char* line, size_t len, Request& out);

// "xtd_<keyid8hex>_<secret>" -> "<keyid8hex>"; empty when the shape is wrong.
std::string keyIdOf(const std::string& deviceKey);

// Full reply lines, each ending in "\n".
std::string buildAckLine(const std::string& mac, const std::string& firmwareVersion, size_t networksSaved,
                         const std::string& keyId);
std::string buildErrLine(Error e);

}  // namespace xteink::prov
