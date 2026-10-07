#include "ProvisionUsbActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <esp_mac.h>

#include <algorithm>
#include <atomic>
#include <cstdio>

#include "MappedInputManager.h"
#include "WifiCredentialStore.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "xteink/ProvisioningProtocol.h"
#include "xteink/XteinkConfig.h"

namespace prov = xteink::prov;

namespace {
std::atomic<bool> g_active{false};

std::string macString() {
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char buf[18];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return buf;
}

// Applies a validated request. NVS entries are committed together first; the
// networks then go through WIFI_STORE (each add saves wifi.json). The two stores
// are not atomic with each other: a storage_error may leave the NVS part applied,
// and the host retries the whole message (all operations are idempotent).
prov::Error applyRequest(const prov::Request& req, size_t& networksSaved) {
  WIFI_STORE.loadFromFile();

  // Pre-check the 8-network cap so nothing is half-applied for that reason.
  size_t resulting = req.replaceNetworks ? 0 : WIFI_STORE.getCredentialCount();
  resulting += static_cast<size_t>(std::count_if(req.networks.begin(), req.networks.end(), [&req](const auto& n) {
    return req.replaceNetworks || !WIFI_STORE.hasSavedCredential(n.ssid);
  }));
  if (resulting > prov::kMaxNetworks) return prov::Error::TooManyNetworks;

  const std::string* key = req.deviceKey ? &*req.deviceKey : nullptr;
  const std::string* lan = req.lanUrl ? &*req.lanUrl : nullptr;
  const std::string* tun = req.tunnelUrl ? &*req.tunnelUrl : nullptr;
  if ((key || lan || tun) && !xteink::config::setAll(key, lan, tun)) return prov::Error::StorageError;

  if (req.replaceNetworks) {
    const auto saved = WIFI_STORE.getCredentialSummaries();
    if (std::any_of(saved.begin(), saved.end(), [](const auto& s) { return !WIFI_STORE.removeCredential(s.ssid); })) {
      return prov::Error::StorageError;
    }
  }
  if (std::any_of(req.networks.begin(), req.networks.end(),
                  [](const auto& n) { return !WIFI_STORE.addCredential(n.ssid, n.password); })) {
    return prov::Error::StorageError;
  }
  networksSaved = WIFI_STORE.getCredentialCount();
  return prov::Error::None;
}
}  // namespace

bool ProvisionUsbActivity::isActive() { return g_active.load(); }

void ProvisionUsbActivity::onEnter() {
  Activity::onEnter();
  // One 3073-byte buffer for the visit (worst-case line, decoded in place by the
  // parser so no second copy). Freed in onExit. Nothrow: bare new aborts on ESP32.
  lineBuf = makeUniqueNoThrow<char[]>(prov::kMaxLineLen + 1);
  lineLen = 0;
  overflowed = false;
  provisioned = false;
  lastError = lineBuf ? nullptr : "out of memory";
  keyId = xteink::config::getKeyId();
  // Drop anything that arrived before the screen opened so a stale message
  // cannot be applied the moment the user enters provisioning.
  while (logSerial.available() > 0) logSerial.read();
  g_active.store(lineBuf != nullptr);
  requestUpdate();
}

void ProvisionUsbActivity::onExit() {
  g_active.store(false);
  lineBuf.reset();
  Activity::onExit();
}

void ProvisionUsbActivity::sendLine(const std::string& line) {
  // A leading newline terminates any partial log line so the reply starts at a
  // line boundary. The 1 ms TX timeout is load-bearing for logging (main.cpp);
  // raise it only for this short reply and restore it.
#if LOG_SERIAL_HAS_TX_TIMEOUT
  logSerial.setTxTimeoutMs(100);
#endif
  logSerial.write('\n');
  logSerial.write(reinterpret_cast<const uint8_t*>(line.data()), line.size());
#if LOG_SERIAL_HAS_TX_TIMEOUT
  logSerial.setTxTimeoutMs(1);
#endif
}

void ProvisionUsbActivity::handleLine(size_t len) {
  prov::Request req;
  lineBuf[len] = '\0';
  prov::Error err = prov::parseLine(lineBuf.get(), len, req);
  size_t saved = WIFI_STORE.getCredentialCount();
  if (err == prov::Error::None) err = applyRequest(req, saved);

  // Wipe the decoded payload (passwords/key) from the line buffer.
  memset(lineBuf.get(), 0, prov::kMaxLineLen + 1);

  if (err != prov::Error::None) {
    LOG_ERR("PROV", "Rejected: %s", prov::errorCode(err));
    lastError = prov::errorCode(err);
    sendLine(prov::buildErrLine(err));
  } else {
    keyId = xteink::config::getKeyId();
    networksSaved = saved;
    provisioned = true;
    lastError = nullptr;
    LOG_INF("PROV", "Provisioned: %zu networks, key id %s", saved, keyId.empty() ? "-" : keyId.c_str());
    sendLine(prov::buildAckLine(macString(), CROSSPOINT_VERSION, saved, keyId));
  }
  requestUpdate();
}

void ProvisionUsbActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (!lineBuf) return;

  while (logSerial.available() > 0) {
    const int c = logSerial.read();
    if (c < 0) break;
    if (c == '\n') {
      if (overflowed) {
        overflowed = false;
        lastError = prov::errorCode(prov::Error::TooLong);
        sendLine(prov::buildErrLine(prov::Error::TooLong));
        requestUpdate();
      } else if (lineLen > 0) {
        const size_t len = lineLen;
        lineLen = 0;
        handleLine(len);
      }
      lineLen = 0;
      continue;
    }
    if (overflowed) continue;
    if (lineLen >= prov::kMaxLineLen) {
      overflowed = true;  // discard the rest of this line, report at its newline
      lineLen = 0;
      continue;
    }
    lineBuf[lineLen++] = static_cast<char>(c);
  }
}

void ProvisionUsbActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_PROVISION_USB));

  char buf[64];
  if (provisioned) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 40, tr(STR_PROVISION_DONE), true, EpdFontFamily::BOLD);
    snprintf(buf, sizeof(buf), "%s: %zu", tr(STR_PROVISION_NETWORKS), networksSaved);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 10, buf);
    if (!keyId.empty()) {
      snprintf(buf, sizeof(buf), "%s: %s", tr(STR_PROVISION_KEY_ID), keyId.c_str());
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 20, buf);
    }
  } else {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 10, tr(STR_PROVISION_WAITING), true, EpdFontFamily::BOLD);
    if (lastError) {
      // Wire error codes are protocol identifiers, shown as-is.
      snprintf(buf, sizeof(buf), "%s", lastError);
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 20, buf);
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
