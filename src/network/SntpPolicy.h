#pragma once

#include <cstdint>

// xteink fork (plan risk r21): when tls_trust::ensureClockForTls() may spend
// time on SNTP before an https request. Pure, so the host tests can pin it
// down (test/xteink_sync). The caller gathers the inputs from ESP-IDF's SNTP
// state and TrustedTime.
namespace sntp_policy {

// After an unsuccessful bounded wait, do not wait again for this long. A sync
// session (status, queue, items, acks) makes several https requests within a
// minute or two (37 s on the X3, 2026-10 hotspot log), so ten minutes means at
// most one wait per session; a later session in the same boot (a new network,
// a hotspot that gained connectivity) gets one fresh wait.
constexpr uint32_t RETRY_AFTER_MS = 10UL * 60UL * 1000UL;

enum class Action : uint8_t {
  Skip,          // proceed now with the current clock
  Wait,          // SNTP is already running: wait for its answer, do not restart it
  StartAndWait,  // nothing started SNTP this boot: start it, then wait
};

struct Inputs {
  bool syncedThisBoot;   // an SNTP answer has set the clock this boot
  bool sntpRunning;      // esp_sntp_enabled(): Wi-Fi join or an earlier attempt started it
  bool clockTrusted;     // trustedtime::trustedNow() != 0 (synced, or restored floor)
  bool waitedBefore;     // this module already spent a bounded wait this boot
  uint32_t msSinceWait;  // millis() since that wait started (ignored when !waitedBefore)
};

constexpr Action actionFor(const Inputs& in) {
  if (in.syncedThisBoot) return Action::Skip;
  // An attempt already ran this boot (Wi-Fi join, Clock sync, an earlier
  // request) and is still polling in the background; with a plausible clock,
  // reuse that outcome instead of trying again before every request. The
  // background SNTP corrects the clock whenever its answer arrives.
  if (in.sntpRunning && in.clockTrusted) return Action::Skip;
  if (in.waitedBefore && in.msSinceWait < RETRY_AFTER_MS) return Action::Skip;
  return in.sntpRunning ? Action::Wait : Action::StartAndWait;
}

}  // namespace sntp_policy
