#include "XteinkConfig.h"

#include <Logging.h>
#include <nvs.h>

#include "ProvisioningProtocol.h"

namespace xteink::config {

namespace {
// NVS namespace/key names are limited to 15 chars.
constexpr const char* kNamespace = "xteink";
constexpr const char* kKeyDevKey = "devkey";
constexpr const char* kKeyLanUrl = "lan_url";
constexpr const char* kKeyTunnelUrl = "tunnel_url";

std::string readString(const char* key) {
  nvs_handle_t h;
  if (nvs_open(kNamespace, NVS_READONLY, &h) != ESP_OK) return "";
  std::string out;
  size_t len = 0;
  if (nvs_get_str(h, key, nullptr, &len) == ESP_OK && len > 1 && len <= 512) {
    out.resize(len);
    if (nvs_get_str(h, key, out.data(), &len) == ESP_OK) {
      out.resize(len - 1);  // len includes the NUL
    } else {
      out.clear();
    }
  }
  nvs_close(h);
  return out;
}

esp_err_t writeOne(nvs_handle_t h, const char* key, const std::string& value) {
  if (value.empty()) {
    const esp_err_t e = nvs_erase_key(h, key);
    return e == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : e;
  }
  return nvs_set_str(h, key, value.c_str());
}
}  // namespace

std::string getDeviceKey() { return readString(kKeyDevKey); }
std::string getLanUrl() { return readString(kKeyLanUrl); }
std::string getTunnelUrl() { return readString(kKeyTunnelUrl); }
std::string getKeyId() { return xteink::prov::keyIdOf(getDeviceKey()); }

bool hasConfig() { return !getDeviceKey().empty() && (!getLanUrl().empty() || !getTunnelUrl().empty()); }

bool setAll(const std::string* deviceKey, const std::string* lanUrl, const std::string* tunnelUrl) {
  nvs_handle_t h;
  if (nvs_open(kNamespace, NVS_READWRITE, &h) != ESP_OK) {
    LOG_ERR("XCFG", "NVS open failed");
    return false;
  }
  esp_err_t err = ESP_OK;
  if (err == ESP_OK && deviceKey) err = writeOne(h, kKeyDevKey, *deviceKey);
  if (err == ESP_OK && lanUrl) err = writeOne(h, kKeyLanUrl, *lanUrl);
  if (err == ESP_OK && tunnelUrl) err = writeOne(h, kKeyTunnelUrl, *tunnelUrl);
  if (err == ESP_OK) err = nvs_commit(h);
  nvs_close(h);
  if (err != ESP_OK) LOG_ERR("XCFG", "NVS write failed: %d", static_cast<int>(err));
  return err == ESP_OK;
}

bool setDeviceKey(const std::string& key) { return setAll(&key, nullptr, nullptr); }
bool setLanUrl(const std::string& url) { return setAll(nullptr, &url, nullptr); }
bool setTunnelUrl(const std::string& url) { return setAll(nullptr, nullptr, &url); }

}  // namespace xteink::config
