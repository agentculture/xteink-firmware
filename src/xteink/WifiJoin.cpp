#include "WifiJoin.h"

#include <algorithm>

namespace xteink::wifijoin {

const char* errorName(Error e) {
  switch (e) {
    case Error::None:
      return "none";
    case Error::NoSavedNetworks:
      return "no-saved-networks";
    case Error::NoneInRange:
      return "none-in-range";
    case Error::JoinFailed:
      return "join-failed";
    case Error::Timeout:
      return "timeout";
  }
  return "unknown";
}

namespace {

// Elapsed-time arithmetic that survives millis() wraparound.
bool expired(uint32_t start, uint32_t budget, uint32_t now) { return static_cast<uint32_t>(now - start) >= budget; }

uint32_t remaining(uint32_t start, uint32_t budget, uint32_t now) {
  const uint32_t el = static_cast<uint32_t>(now - start);
  return el >= budget ? 0 : budget - el;
}

enum class Attempt { Joined, Failed, OutOfTime };

Attempt tryJoin(WifiPort& port, const Credential& cred, uint32_t start, uint32_t budget) {
  uint32_t left = remaining(start, budget, port.millis());
  if (left == 0) return Attempt::OutOfTime;
  const uint32_t limit = std::min(left, kPerNetworkTimeoutMs);
  const uint32_t attemptStart = port.millis();
  port.begin(cred.ssid, cred.password);
  while (!port.connected()) {
    if (expired(start, budget, port.millis())) {
      port.disconnect();
      return Attempt::OutOfTime;
    }
    if (expired(attemptStart, limit, port.millis())) {
      port.disconnect();
      return Attempt::Failed;
    }
    port.sleepMs(kPollMs);
  }
  return Attempt::Joined;
}

}  // namespace

Result joinAnySaved(WifiPort& port, uint32_t deadlineMs) {
  const uint32_t start = port.millis();
  if (port.savedCount() == 0) return {Error::NoSavedNetworks, {}};

  bool anyInRange = false;
  const std::string last = port.lastConnectedSsid();

  if (!last.empty()) {
    if (const auto cred = port.findCredential(last)) {
      anyInRange = true;  // attempted blindly; refined by the scan below
      switch (tryJoin(port, *cred, start, deadlineMs)) {
        case Attempt::Joined:
          return {Error::None, cred->ssid};
        case Attempt::OutOfTime:
          return {Error::Timeout, {}};
        case Attempt::Failed:
          break;
      }
    }
  }

  const uint32_t left = remaining(start, deadlineMs, port.millis());
  if (left == 0) return {Error::Timeout, {}};
  auto found = port.scan(left);
  if (expired(start, deadlineMs, port.millis())) return {Error::Timeout, {}};

  std::stable_sort(found.begin(), found.end(),
                   [](const ScanResult& a, const ScanResult& b) { return a.rssi > b.rssi; });

  std::vector<std::string> tried;
  if (!last.empty()) tried.push_back(last);
  bool attempted = !tried.empty() && anyInRange;
  bool sawSaved = false;
  for (const auto& r : found) {
    if (r.ssid.empty() || r.ssid == last) continue;
    if (std::find(tried.begin(), tried.end(), r.ssid) != tried.end()) continue;
    const auto cred = port.findCredential(r.ssid);
    if (!cred) continue;
    sawSaved = true;
    tried.push_back(r.ssid);
    switch (tryJoin(port, *cred, start, deadlineMs)) {
      case Attempt::Joined:
        return {Error::None, cred->ssid};
      case Attempt::OutOfTime:
        return {Error::Timeout, {}};
      case Attempt::Failed:
        attempted = true;
        break;
    }
  }
  if (attempted) return {Error::JoinFailed, {}};
  return {sawSaved ? Error::JoinFailed : Error::NoneInRange, {}};
}

}  // namespace xteink::wifijoin

#ifdef ARDUINO
#include <Arduino.h>
#include <WiFi.h>

#include "WifiCredentialStore.h"

namespace xteink::wifijoin {

namespace {
class ArduinoPort : public WifiPort {
 public:
  std::string lastConnectedSsid() override { return WIFI_STORE.getLastConnectedSsid(); }
  std::optional<Credential> findCredential(const std::string& ssid) override {
    const auto c = WIFI_STORE.findCredential(ssid);
    if (!c) return std::nullopt;
    return Credential{c->ssid, c->password};
  }
  size_t savedCount() override { return WIFI_STORE.getCredentialCount(); }
  std::vector<ScanResult> scan(uint32_t maxMs) override {
    std::vector<ScanResult> out;
    WiFi.mode(WIFI_STA);
    WiFi.scanDelete();
    // Async scan so the hard deadline is honoured.
    WiFi.scanNetworks(true);
    const uint32_t t0 = ::millis();
    int n = WiFi.scanComplete();
    while (n == WIFI_SCAN_RUNNING && static_cast<uint32_t>(::millis() - t0) < maxMs) {
      ::delay(50);
      n = WiFi.scanComplete();
    }
    if (n > 0) {
      for (int i = 0; i < n; i++) out.push_back({WiFi.SSID(i).c_str(), WiFi.RSSI(i)});
    }
    WiFi.scanDelete();
    return out;
  }
  void begin(const std::string& ssid, const std::string& password) override {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());
  }
  bool connected() override { return WiFi.status() == WL_CONNECTED; }
  void disconnect() override { WiFi.disconnect(); }
  void sleepMs(uint32_t ms) override { ::delay(ms); }
  uint32_t millis() override { return ::millis(); }
};
}  // namespace

Result joinAnySaved(uint32_t deadlineMs) {
  ArduinoPort port;
  const Result r = joinAnySaved(port, deadlineMs);
  if (r.ok()) WIFI_STORE.setLastConnectedSsid(r.ssid);
  return r;
}

}  // namespace xteink::wifijoin
#endif
