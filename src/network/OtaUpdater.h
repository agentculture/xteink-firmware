#pragma once

#include <cstddef>
#include <string>

#include "xteink/SyncProtocol.h"

// xteink fork: firmware comes only from the provisioned xteink server, as an
// optional offer in the POST /api/device/status response (docs/xteink/tls.md,
// "OTA source"). The decision and the verify-then-commit flow are the pure
// xteink::sync::decideOta() and installOffer() (SyncProtocol.h, host-tested in
// test/xteink_ota); this class binds them to HTTP, NVS and the ESP-IDF OTA
// calls.

// Tunnel OTA (a 6 MB TLS download across the internet) stays off until it is
// proven on the X3 (decision c32). With 0, an offer that only the tunnel origin
// answered is reported as deferred and never installed.
#ifndef XTEINK_OTA_TUNNEL
#define XTEINK_OTA_TUNNEL 0
#endif

class OtaUpdater {
  xteink::sync::FirmwareOffer offer;
  xteink::sync::OtaDecision decision = xteink::sync::OtaDecision::None;
  xteink::sync::Error lastError = xteink::sync::Error::None;
  std::string origin;  // origin whose status response carried the offer
  size_t processedSize = 0;
  size_t totalSize = 0;

 public:
  using ProgressCallback = void (*)(void* ctx);

  enum OtaUpdaterError {
    OK = 0,
    NO_UPDATE,  // no offer, the offer is the running build, or it is deferred
    HTTP_ERROR,
    JSON_PARSE_ERROR,
    UPDATE_OLDER_ERROR,  // installUpdate() without an installable offer
    INTERNAL_UPDATE_ERROR,
    OOM_ERROR,
    WRONG_DEVICE_ERROR,
    NOT_PROVISIONED,  // no device key: there is no update source
    HASH_ERROR,       // the image bytes did not match the offer's sha256
  };

  size_t getOtaSize() const { return totalSize; }

  size_t getProcessedSize() const { return processedSize; }

  size_t getTotalSize() const { return totalSize; }

  OtaUpdater() = default;

  // POST /api/device/status (firmware_version only) to the LAN origin, then
  // the tunnel, and reads the offer from the first origin that answers.
  // OK when the offer should be installed; NO_UPDATE when there is none, it is
  // the running build, or it is deferred (getDecision() says which).
  OtaUpdaterError checkForUpdate();

  // Takes an offer read from another status response (for example the daily
  // routine's). `fromOrigin` is the origin that sent that response.
  void setOffer(const xteink::sync::FirmwareOffer& newOffer, const std::string& fromOrigin);

  xteink::sync::OtaDecision getDecision() const { return decision; }
  const xteink::sync::FirmwareOffer& getOffer() const { return offer; }
  // Error of the last check or install (Error::None on success). errorCode()
  // turns it into the E-code for the status report's last_error.
  xteink::sync::Error getLastError() const { return lastError; }

  // True when the offer should be installed. The offer decides, not version
  // ordering: an older build or another -dev build installs too. Kept under
  // the upstream name for OtaUpdateActivity.
  bool isUpdateNewer() const { return decision == xteink::sync::OtaDecision::Install; }
  const std::string& getLatestVersion() const { return offer.version; }

  // Streams the offered image into the inactive slot and switches the boot
  // partition only when the size and sha256 match. Does not reboot.
  OtaUpdaterError installUpdate(ProgressCallback onProgress = nullptr, void* ctx = nullptr);
};
