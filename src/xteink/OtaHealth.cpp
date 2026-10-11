#include "OtaHealth.h"

#include <algorithm>

#include "SyncProtocol.h"

namespace xteink::otahealth {

const char* reasonName(const Reason r) {
  switch (r) {
    case Reason::None:
      return "ok";
    case Reason::Display:
      return "display";
    case Reason::SdCard:
      return "sd";
    case Reason::Server:
      return "server";
  }
  return "unknown";
}

namespace {

// millis() arithmetic that survives the 49-day wrap.
uint32_t remainingMs(const uint32_t startMs, const uint32_t windowMs, const uint32_t nowMs) {
  const uint32_t elapsed = nowMs - startMs;
  return elapsed >= windowMs ? 0 : windowMs - elapsed;
}

}  // namespace

Decision decideHealth(HealthPort& port, const uint32_t windowMs, const uint32_t retryDelayMs) {
  Decision d;
  d.verdict = Verdict::Rollback;
  if (!port.displayOk()) {
    d.reason = Reason::Display;
    return d;
  }
  if (!port.sdOk()) {
    d.reason = Reason::SdCard;
    return d;
  }
  const uint32_t startMs = port.millis();
  uint32_t left = remainingMs(startMs, windowMs, startMs);
  while (left > 0) {
    ++d.attempts;
    if (port.postStatus(left)) {
      d.verdict = Verdict::Valid;
      d.reason = Reason::None;
      return d;
    }
    left = remainingMs(startMs, windowMs, port.millis());
    if (left == 0) break;
    port.sleepMs(std::min(left, retryDelayMs));
    left = remainingMs(startMs, windowMs, port.millis());
  }
  d.reason = Reason::Server;
  return d;
}

BootOutcome runOnBoot(HealthPort& port, const uint32_t windowMs, const uint32_t retryDelayMs, Decision* decision) {
  if (!port.pendingVerify()) {
    // The previous boot ran a candidate that never passed: it failed its check
    // or was aborted by the bootloader (crash, power cut). Report it once.
    const std::string cand = port.candidate();
    if (!cand.empty()) {
      if (port.hasInvalidPartition() && !sync::sameFirmwareVersion(cand, port.runningVersion())) {
        port.setRolledBack(cand);
        port.rememberRolledBack(cand);
      }
      port.clearCandidate();
    }
    return BootOutcome::NotPending;
  }

  // The installer recorded the candidate before switching the boot partition;
  // make sure (an image flashed another way, or an older installer) without
  // rewriting NVS when it is already set.
  const std::string running = port.runningVersion();
  if (port.candidate() != running) port.setCandidate(running);
  const Decision d = decideHealth(port, windowMs, retryDelayMs);
  if (decision) *decision = d;
  if (d.verdict == Verdict::Valid) {
    // Clear first: a power cut between the two leaves the image pending (the
    // bootloader rolls back) rather than a valid image recorded as rolled back.
    port.clearCandidate();
    port.markValid();
    return BootOutcome::Valid;
  }
  port.markInvalidAndReboot();
  return BootOutcome::RolledBack;
}

}  // namespace xteink::otahealth

#ifdef ARDUINO
#include <Arduino.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_ota_ops.h>
#include <nvs.h>

#include "WifiCredentialStore.h"
#include "WifiJoin.h"
#include "network/OtaUpdater.h"

// Arduino core hook (esp32-hal-misc.c): true stops initArduino() from marking a
// PENDING_VERIFY image valid on its own. onBoot() decides instead.
extern "C" bool verifyRollbackLater() { return true; }

namespace xteink::otahealth {

namespace {

constexpr const char* kTag = "OTAHC";
// Same string the status report sends as firmware_version.
constexpr const char* kFirmwareVersion = "xteink-fw " CROSSPOINT_VERSION;
constexpr const char* kNvsNamespace = "xteink";  // shared with XteinkConfig/XteinkSync
constexpr const char* kNvsCandidate = "rb_cand";
constexpr const char* kNvsRolledBack = "rb_version";
constexpr const char* kNvsRolledBackList = "rb_list";
constexpr size_t kMaxVersionLen = 96;
// rb_list: kMaxRolledBackBuilds report-form versions, newline-separated.
constexpr size_t kMaxListLen = sync::kMaxRolledBackBuilds * (sync::kMaxFirmwareVersionLen + 11);

std::string nvsGet(const char* key, const size_t maxLen = kMaxVersionLen) {
  nvs_handle_t h;
  if (nvs_open(kNvsNamespace, NVS_READONLY, &h) != ESP_OK) return "";
  std::string buf(maxLen + 1, '\0');
  size_t len = buf.size();
  if (nvs_get_str(h, key, buf.data(), &len) != ESP_OK) len = 0;
  nvs_close(h);
  buf.resize(len > 0 ? len - 1 : 0);  // len counts the terminator
  return buf;
}

void nvsSet(const char* key, const std::string& value, const size_t maxLen = kMaxVersionLen) {
  nvs_handle_t h;
  if (nvs_open(kNvsNamespace, NVS_READWRITE, &h) != ESP_OK) {
    LOG_ERR(kTag, "NVS open failed");
    return;
  }
  esp_err_t err;
  if (value.empty()) {
    err = nvs_erase_key(h, key);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
  } else {
    err = nvs_set_str(h, key, value.substr(0, maxLen).c_str());
  }
  if (err == ESP_OK) err = nvs_commit(h);
  nvs_close(h);
  if (err != ESP_OK) LOG_ERR(kTag, "NVS write %s failed: %d", key, static_cast<int>(err));
}

class DevicePort final : public HealthPort {
 public:
  DevicePort(const bool display, const bool sd) : display(display), sd(sd) {}

  bool pendingVerify() override {
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    return running && esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY;
  }
  bool displayOk() override { return display; }
  bool sdOk() override { return sd; }

  bool postStatus(const uint32_t remaining) override {
    const uint32_t startMs = ::millis();
    if (WiFi.status() != WL_CONNECTED) {
      if (!storeLoaded) {
        WIFI_STORE.loadFromFile();
        storeLoaded = true;
      }
      const wifijoin::Result r = wifijoin::joinAnySaved(remaining);
      if (!r.ok()) {
        LOG_ERR(kTag, "Wi-Fi join failed: %s", wifijoin::errorName(r.error));
        return false;
      }
    }
    // The OTA check is one status POST (LAN first, then the tunnel), bounded by
    // what the join left of the window.
    const uint32_t spent = ::millis() - startMs;
    if (spent >= remaining) return false;
    OtaUpdater updater;
    const OtaUpdater::OtaUpdaterError e = updater.checkForUpdate(remaining - spent);
    // Any HTTP answer proves the new image boots and reaches the server (d8): a
    // server error (5xx while it restarts, 401 for a key problem) is not a reason
    // to roll back a good build. Only no answer at all fails the check.
    const bool answered = updater.getServerAnswered();
    if (e != OtaUpdater::OK && e != OtaUpdater::NO_UPDATE) {
      LOG_ERR(kTag, "Status POST %s: %s", answered ? "answered with an error" : "failed",
              sync::errorCode(updater.getLastError()));
    }
    return answered;
  }

  uint32_t millis() override { return ::millis(); }
  void sleepMs(const uint32_t ms) override { ::delay(ms); }

  void markValid() override {
    const esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err != ESP_OK) LOG_ERR(kTag, "mark valid failed: %s", esp_err_to_name(err));
  }
  void markInvalidAndReboot() override {
    // Returns only when there is no image to roll back to.
    const esp_err_t err = esp_ota_mark_app_invalid_rollback_and_reboot();
    LOG_ERR(kTag, "rollback failed: %s", esp_err_to_name(err));
  }

  std::string runningVersion() override { return kFirmwareVersion; }
  bool hasInvalidPartition() override { return esp_ota_get_last_invalid_partition() != nullptr; }
  std::string candidate() override { return nvsGet(kNvsCandidate); }
  void setCandidate(const std::string& version) override { nvsSet(kNvsCandidate, version); }
  void clearCandidate() override { nvsSet(kNvsCandidate, ""); }
  void setRolledBack(const std::string& version) override {
    LOG_INF(kTag, "Rolled back from %s", version.c_str());
    nvsSet(kNvsRolledBack, version);
  }
  void rememberRolledBack(const std::string& version) override {
    nvsSet(kNvsRolledBackList, sync::rolledBackListAdd(rolledBackBuilds(), version), kMaxListLen);
  }

 private:
  const bool display;
  const bool sd;
  bool storeLoaded = false;
};

}  // namespace

void onBoot(const bool displayOk, const bool sdOk) {
  DevicePort port(displayOk, sdOk);
  const uint32_t startMs = millis();
  Decision d;
  const BootOutcome outcome = runOnBoot(port, kHealthServerWindowMs, kHealthRetryDelayMs, &d);
  if (outcome == BootOutcome::NotPending) return;
  // Only the pass path, or a rollback with no image to roll back to, gets here.
  LOG_INF(kTag, "Health check of %s: %s (%s, %u attempts, %lu ms)", kFirmwareVersion,
          outcome == BootOutcome::Valid ? "valid" : "rollback failed", reasonName(d.reason),
          static_cast<unsigned>(d.attempts), static_cast<unsigned long>(millis() - startMs));
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }
}

std::string rolledBackVersion() { return nvsGet(kNvsRolledBack); }

std::string rolledBackBuilds() { return nvsGet(kNvsRolledBackList, kMaxListLen); }

void recordInstallCandidate(const std::string& offeredVersion) {
  nvsSet(kNvsCandidate, sync::firmwareReportVersion(offeredVersion));
}

void clearInstallCandidate() { nvsSet(kNvsCandidate, ""); }

void clearRolledBackVersion(const std::string& sent) {
  if (sent.empty() || nvsGet(kNvsRolledBack) != sent) return;
  nvsSet(kNvsRolledBack, "");
}

}  // namespace xteink::otahealth
#endif
