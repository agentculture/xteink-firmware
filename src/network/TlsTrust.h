#pragma once

// xteink fork: verified HTTPS for every HttpDownloader request (sync, OPDS,
// OTA). Replaces upstream's setInsecure(): the wolfSSL transport verifies the
// server chain against a small pinned root bundle (XteinkCaBundle.h) and the
// certificate name against the requested host. Policy and rotation:
// docs/xteink/tls.md.

#include <string_view>

namespace freeink {
class SecureHttpClient;
}

namespace tls_trust {

// True for an https:// URL (scheme compared case-insensitively). Plain http
// (LAN sync on xteink.local) carries no certificate and is left alone.
bool isHttpsUrl(std::string_view url);

// Point the client at the pinned root bundle. Clears any insecure flag the
// client carried, so a request without trust anchors fails closed.
void applyVerifiedTls(freeink::SecureHttpClient& http);

// Certificate validity is checked against the wall clock. Before an https
// request, make sure SNTP had one bounded chance to set it, so a clock
// restored from the persisted floor (possibly weeks stale) cannot reject
// freshly issued leaf certificates. Reuses an SNTP attempt already running
// this boot (Wi-Fi join) instead of restarting it, and never waits more than
// once per sntp_policy::RETRY_AFTER_MS (SntpPolicy.h); other calls return
// immediately. Never disables checks: with no usable time the handshake fails
// closed.
void ensureClockForTls();

}  // namespace tls_trust
