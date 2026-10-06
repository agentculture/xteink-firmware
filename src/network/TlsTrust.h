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

// Certificate validity is checked against the wall clock. Before the first
// https request of a boot, run one bounded SNTP sync so a clock restored from
// the persisted floor (possibly weeks stale) cannot reject freshly issued
// leaf certificates. Later calls return immediately. Never disables checks:
// with no usable time the handshake fails closed.
void ensureClockForTls();

}  // namespace tls_trust
