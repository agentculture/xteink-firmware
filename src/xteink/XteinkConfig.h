#pragma once
// xteink sync configuration kept in NVS (namespace "xteink"): the device key and
// the LAN/tunnel server URLs. Written by serial provisioning
// (ProvisioningActivity, docs/provisioning.md), read by the sync client.
//
// Saved Wi-Fi networks are NOT here: they live in upstream's obfuscated
// /.crosspoint/wifi.json (WIFI_STORE).
//
// Secrecy: the device key is never logged. Log keyId() if a log line needs to
// identify it.
#include <string>

namespace xteink::config {

// Empty string when unset or NVS is unavailable.
std::string getDeviceKey();
std::string getLanUrl();
std::string getTunnelUrl();
// "xtd_<keyid8hex>_<secret>" -> "<keyid8hex>"; empty when no key is set.
std::string getKeyId();

// True when a device key and at least one server URL are set.
bool hasConfig();

// Empty value erases the entry. Each returns false on NVS failure.
bool setDeviceKey(const std::string& key);
bool setLanUrl(const std::string& url);
bool setTunnelUrl(const std::string& url);

// Applies the given entries under one NVS handle and one commit; nullptr leaves
// an entry unchanged. All-or-nothing at the commit.
bool setAll(const std::string* deviceKey, const std::string* lanUrl, const std::string* tunnelUrl);

}  // namespace xteink::config
