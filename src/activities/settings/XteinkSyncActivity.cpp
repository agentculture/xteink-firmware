#include "XteinkSyncActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <cstdio>

#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "xteink/XteinkConfig.h"

namespace xsync = xteink::sync;

namespace {
constexpr unsigned long kCancelPollMs = 100;
// E-ink refreshes are slow; redraw the progress at most this often.
constexpr unsigned long kProgressRenderMs = 2000;
}  // namespace

void XteinkSyncActivity::onEnter() {
  Activity::onEnter();
  xsync::setJoinHookSuppressed(true);

  if (xteink::config::getDeviceKey().empty()) {
    outcome.error = xsync::Error::NoKey;
    state = State::Done;
    requestUpdate();
    return;
  }
  if (WiFi.status() == WL_CONNECTED) {
    state = State::Syncing;
    startPending = true;
    requestUpdate();
    return;
  }
  // Fresh heap for the TLS session; returns (and we carry on) on boards where
  // a soft reset is not safe, or when sleep is already in progress.
  if (!freshHeap) silentRestartToXteinkSync();

  WiFi.mode(WIFI_STA);
  auto wifi = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput);
  if (!wifi) {
    LOG_ERR("XSYNC", "OOM: WifiSelectionActivity");
    outcome.error = xsync::Error::LowMemory;
    state = State::Done;
    requestUpdate();
    return;
  }
  startActivityForResult(std::move(wifi), [this](const ActivityResult& result) { onWifiJoined(!result.isCancelled); });
}

void XteinkSyncActivity::onWifiJoined(const bool connected) {
  if (!connected) {
    finish();
    return;
  }
  {
    RenderLock lock(*this);
    state = State::Syncing;
  }
  startPending = true;
}

void XteinkSyncActivity::onExit() {
  Activity::onExit();
  xsync::setJoinHookSuppressed(false);
  // Free the LWIP/TLS fragmentation on the way out, like the other Wi-Fi screens.
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void XteinkSyncActivity::onProgress(void* ctx, const xsync::Progress& p) {
  auto* self = static_cast<XteinkSyncActivity*>(ctx);
  const unsigned long now = millis();
  const bool newItem = p.itemIndex != self->progress.itemIndex;
  if (!newItem && now - self->lastProgressRenderMs < kProgressRenderMs) return;
  self->lastProgressRenderMs = now;
  {
    RenderLock lock(*self);
    self->progress = p;
  }
  // immediate: the main loop is blocked inside the sync, so the deferred
  // update flag would not be seen until it returns.
  self->requestUpdate(true);
}

bool XteinkSyncActivity::shouldCancel(void* ctx) {
  auto* self = static_cast<XteinkSyncActivity*>(ctx);
  if (self->cancelRequested) return true;
  const unsigned long now = millis();
  if (now - self->lastCancelPollMs < kCancelPollMs) return false;
  self->lastCancelPollMs = now;
  // The sync blocks the main loop, so poll the buttons here.
  self->mappedInput.update();
  if (self->mappedInput.wasReleased(MappedInputManager::Button::Back)) self->cancelRequested = true;
  return self->cancelRequested;
}

void XteinkSyncActivity::runSync() {
  requestUpdateAndWait();
  const xsync::Callbacks callbacks{this, &XteinkSyncActivity::onProgress, &XteinkSyncActivity::shouldCancel};
  const xsync::Outcome result = xsync::run(&renderer, callbacks);
  {
    RenderLock lock(*this);
    outcome = result;
    state = State::Done;
  }
  requestUpdate();
}

void XteinkSyncActivity::loop() {
  if (startPending) {
    startPending = false;
    runSync();
    return;
  }
  if (state != State::Done) return;
  int x = 0;
  int y = 0;
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      mappedInput.wasReleased(MappedInputManager::Button::Confirm) || mappedInput.wasScreenTapped(x, y)) {
    finish();
  }
}

void XteinkSyncActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
  const int top = (pageHeight - lineH) / 2;
  char line[96];

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_XTEINK_SYNC_TITLE));

  if (state != State::Done) {
    if (progress.itemIndex == 0) {
      renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_XTEINK_SYNC_CONTACTING));
    } else {
      renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_XTEINK_SYNCING), true, EpdFontFamily::BOLD);
      snprintf(line, sizeof(line), tr(STR_XTEINK_SYNC_ITEM), static_cast<int>(progress.itemIndex),
               static_cast<int>(progress.itemCount));
      int y = top + lineH + metrics.verticalSpacing;
      renderer.drawCenteredText(UI_10_FONT_ID, y, line);
      y += lineH + metrics.verticalSpacing;
      GUI.drawProgressBar(
          renderer,
          Rect{metrics.contentSidePadding, y, pageWidth - metrics.contentSidePadding * 2, metrics.progressBarHeight},
          progress.bytes, progress.total > 0 ? progress.total : 1);
    }
    const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  const xsync::Error err = outcome.error;
  if (err == xsync::Error::NoKey) {
    const Rect bounds{metrics.contentSidePadding, top, pageWidth - metrics.contentSidePadding * 2, pageHeight - top};
    UITheme::drawCenteredWrappedText(renderer, bounds, UI_10_FONT_ID, tr(STR_XTEINK_SYNC_NO_KEY), 3, true,
                                     EpdFontFamily::BOLD, UITheme::TextVerticalAlignment::TOP);
  } else {
    if (err == xsync::Error::Cancelled || !xsync::statusLine(line, sizeof(line))) {
      snprintf(line, sizeof(line), tr(STR_XTEINK_SYNC_ERROR), xsync::errorCode(err));
    }
    renderer.drawCenteredText(UI_10_FONT_ID, top, line, true, EpdFontFamily::BOLD);
    const char* detail = nullptr;
    if (err == xsync::Error::Unauthorized) {
      detail = tr(STR_XTEINK_SYNC_REPAIR);
    } else if (err == xsync::Error::Protocol) {
      detail = tr(STR_XTEINK_SYNC_UPDATE_FW);
    } else if (err != xsync::Error::Cancelled) {
      snprintf(line, sizeof(line), tr(STR_XTEINK_SYNC_SUMMARY), static_cast<int>(outcome.delivered),
               static_cast<int>(outcome.deleted), static_cast<int>(outcome.skipped));
      detail = line;
    }
    if (detail) renderer.drawCenteredText(UI_10_FONT_ID, top + lineH + metrics.verticalSpacing, detail);
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
