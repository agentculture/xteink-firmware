#pragma once
// Unattended Wi-Fi join: try the last-connected SSID first, then scan and try
// every other saved network that is in range, strongest signal first, all
// inside one hard deadline. The strategy is pure logic behind WifiPort so it is
// unit-tested on the host (test/xteink_wifijoin); the Arduino adapter is
// compiled only on the device.
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace xteink::wifijoin {

struct ScanResult {
  std::string ssid;
  int rssi = 0;
};

struct Credential {
  std::string ssid;
  std::string password;
};

enum class Error : uint8_t {
  None,
  NoSavedNetworks,  // nothing to try
  NoneInRange,      // scan finished, no saved network visible
  JoinFailed,       // every candidate attempt failed
  Timeout,          // the hard deadline expired
};

struct Result {
  Error error = Error::None;
  std::string ssid;  // joined SSID when error == None
  bool ok() const { return error == Error::None; }
};

// Everything the strategy needs from the outside world.
class WifiPort {
 public:
  virtual ~WifiPort() = default;
  virtual std::string lastConnectedSsid() = 0;
  virtual std::optional<Credential> findCredential(const std::string& ssid) = 0;
  virtual size_t savedCount() = 0;
  // Blocking scan, bounded by the port to at most maxMs.
  virtual std::vector<ScanResult> scan(uint32_t maxMs) = 0;
  virtual void begin(const std::string& ssid, const std::string& password) = 0;
  virtual bool connected() = 0;
  virtual void disconnect() = 0;
  virtual void sleepMs(uint32_t ms) = 0;
  virtual uint32_t millis() = 0;
};

constexpr uint32_t kPerNetworkTimeoutMs = 8000;
constexpr uint32_t kPollMs = 100;

const char* errorName(Error e);

// Tries to join within deadlineMs (a budget measured from the call, not an
// absolute time). Returns the joined SSID or a failure code.
Result joinAnySaved(WifiPort& port, uint32_t deadlineMs);

#ifdef ARDUINO
// Device entry point: adapts Arduino WiFi + WIFI_STORE; records the joined SSID
// as last-connected (not persisted to disk here).
Result joinAnySaved(uint32_t deadlineMs);
#endif

}  // namespace xteink::wifijoin
