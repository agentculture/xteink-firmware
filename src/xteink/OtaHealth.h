#pragma once
// Post-update health check and rollback (docs/xteink/tls.md, "OTA source").
//
// The Arduino core's weak verifyRollbackLater() is overridden to return true,
// so initArduino() no longer marks a freshly installed image valid on its own.
// On the first boot of an image the bootloader left PENDING_VERIFY, setup()
// calls onBoot() once the display and SD card are up. The new image is kept
// only when the display initialised, the SD card mounted, and one POST
// /api/device/status got any HTTP answer (even an error status) within
// kHealthServerWindowMs. Otherwise it is
// marked invalid and the device reboots into the previous image, which then
// reports `rolled_back_version` on its next status POST.
//
// The decision and the whole boot flow are pure logic behind HealthPort, so
// they are unit-tested on the host (test/xteink_ota_health). The ESP-IDF, NVS,
// Wi-Fi and HTTP bindings are compiled only on the device.
#include <cstdint>
#include <string>

namespace xteink::otahealth {

// Budget for reaching the server: Wi-Fi join plus status POST attempts. No new
// attempt starts once it has run out.
constexpr uint32_t kHealthServerWindowMs = 45000;
// Pause between failed status POST attempts inside the window.
constexpr uint32_t kHealthRetryDelayMs = 3000;

enum class Verdict : uint8_t {
  Valid,     // keep the new image: esp_ota_mark_app_valid_cancel_rollback
  Rollback,  // esp_ota_mark_app_invalid_rollback_and_reboot
};

enum class Reason : uint8_t {
  None,     // all checks passed
  Display,  // display init failed
  SdCard,   // SD card did not mount
  Server,   // no HTTP answer to a status POST within the window
};

struct Decision {
  Verdict verdict = Verdict::Rollback;
  Reason reason = Reason::None;
  uint16_t attempts = 0;  // status POST attempts made
};

const char* reasonName(Reason r);

// Everything the health check needs from the device.
class HealthPort {
 public:
  virtual ~HealthPort() = default;
  // The running image is ESP_OTA_IMG_PENDING_VERIFY (first boot after OTA).
  virtual bool pendingVerify() = 0;
  virtual bool displayOk() = 0;
  virtual bool sdOk() = 0;
  // One status POST attempt (joining Wi-Fi first when needed), bounded by the
  // port to roughly `remainingMs`. True when the server gave any HTTP answer
  // (2xx, 4xx or 5xx); false only when no server answered.
  virtual bool postStatus(uint32_t remainingMs) = 0;
  virtual uint32_t millis() = 0;
  virtual void sleepMs(uint32_t ms) = 0;
  virtual void markValid() = 0;             // esp_ota_mark_app_valid_cancel_rollback
  virtual void markInvalidAndReboot() = 0;  // esp_ota_mark_app_invalid_rollback_and_reboot

  // --- rolled_back_version bookkeeping (NVS namespace "xteink") ----------
  // The running image's version as the status report sends it.
  virtual std::string runningVersion() = 0;
  // A partition is marked invalid or aborted (esp_ota_get_last_invalid_partition).
  virtual bool hasInvalidPartition() = 0;
  // "rb_cand": the version of an image under verification. The installer
  // writes it right before switching the boot partition (installOffer), the
  // new image writes it again (idempotent) before its check, and it is cleared
  // when the check passes.
  virtual std::string candidate() = 0;
  virtual void setCandidate(const std::string& version) = 0;
  virtual void clearCandidate() = 0;
  // "rb_version": the version to report as rolled_back_version (cleared by
  // the server's 2xx).
  virtual void setRolledBack(const std::string& version) = 0;
  // "rb_list": adds the version to the persistent rolled-back record
  // (SyncProtocol.h, rolledBackListAdd), which no server answer clears.
  virtual void rememberRolledBack(const std::string& version) = 0;
};

// The verdict. Display or SD failure selects Rollback without touching the
// network. Otherwise status POSTs are attempted until a server answers (Valid) or
// windowMs has run out (Rollback, Reason::Server); retryDelayMs separates
// attempts.
Decision decideHealth(HealthPort& port, uint32_t windowMs, uint32_t retryDelayMs);

enum class BootOutcome : uint8_t {
  NotPending,  // normal boot: nothing checked, no network, no delay
  Valid,       // new image kept
  RolledBack,  // markInvalidAndReboot() called (does not return on device)
};

// The whole boot hook. A non-PENDING_VERIFY boot only records a rollback the
// previous boot left behind (candidate set, naming another build, and a
// partition invalid) in both rb_version and rb_list, and returns. A
// PENDING_VERIFY boot makes sure it is the candidate, decides, and calls
// markValid() only on the pass path, markInvalidAndReboot() otherwise.
// `decision` (optional) receives the verdict of a PENDING_VERIFY boot.
BootOutcome runOnBoot(HealthPort& port, uint32_t windowMs = kHealthServerWindowMs,
                      uint32_t retryDelayMs = kHealthRetryDelayMs, Decision* decision = nullptr);

#ifdef ARDUINO
// Device entry from setup(): runs runOnBoot() with the ESP-IDF port. displayOk
// and sdOk are setup()'s own init results.
void onBoot(bool displayOk, bool sdOk);

// rolled_back_version for the next status POST ("" when none).
std::string rolledBackVersion();
// The persistent rolled-back record for decideOta() ("" when none).
std::string rolledBackBuilds();
// Installer side of the candidate bookkeeping (OtaUpdater, before and after
// esp_ota_set_boot_partition). `offeredVersion` in any form.
void recordInstallCandidate(const std::string& offeredVersion);
void clearInstallCandidate();
// Called after a status POST that carried `sent` got a 2xx answer.
void clearRolledBackVersion(const std::string& sent);
#endif

}  // namespace xteink::otahealth
