#include "TlsTrust.h"

#include <Arduino.h>
#include <Logging.h>
#include <SecureHttpClient.h>
#include <TrustedTime.h>
#include <esp_sntp.h>
#include <wolfssl/ssl.h>

#include <cstring>

#include "SntpPolicy.h"
#include "XteinkCaBundle.h"

// Verified HTTPS needs the wolfSSL transport; without it SecureClient is an
// inert stub and every https request would fail. Refuse to build instead.
#if !defined(FREEINK_NET_WOLFSSL)
#error "xteink TLS verification requires -DFREEINK_NET_WOLFSSL=1 (wolfSSL SecureNet transport)"
#endif
// The pinned roots are trust anchors, not the top of every chain servers send:
// Cloudflare (GTS) and GitHub (Sectigo) both append a cross-signed root whose
// issuer is deliberately not pinned. Alternate chains let wolfSSL accept the
// chain once the leaf reaches a pinned root, as browsers and OpenSSL do.
#if !defined(WOLFSSL_ALT_CERT_CHAINS)
#error "xteink TLS verification requires -DWOLFSSL_ALT_CERT_CHAINS (see docs/xteink/tls.md)"
#endif

namespace tls_trust {

namespace {
// Bounded SNTP wait before an https request. pool.ntp.org usually answers in
// well under a second; 5s caps the worst case. When and how often the wait
// happens is sntp_policy::actionFor (SntpPolicy.h): at most once per
// RETRY_AFTER_MS, and not at all while an SNTP attempt started earlier this
// boot is still running and the clock is plausible.
constexpr uint32_t CLOCK_SYNC_TIMEOUT_MS = 5000;
// configTzTime (HalClock::syncFromNTP, TrustedTime) registers at most three
// servers.
constexpr uint8_t SNTP_SERVER_SLOTS = 3;
bool clockSynced = false;
bool waitedBefore = false;
uint32_t lastWaitMs = 0;

// True once an SNTP answer has set the clock. ESP-IDF's
// sntp_get_sync_status() clears COMPLETED on read (lwip/apps/sntp/sntp.c), so
// it is read exactly once per poll; the per-server reachability register
// (set when a server's answer was processed, cleared only by sntp_stop) also
// catches an answer that another poller already consumed, e.g. one that
// arrived after HalClock::syncFromNTP gave up.
bool sntpAnswered() {
  if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) return true;
  for (uint8_t i = 0; i < SNTP_SERVER_SLOTS; ++i) {
    if (esp_sntp_getreachability(i) != 0) return true;
  }
  return false;
}

bool waitForSntp(const uint32_t timeoutMs) {
  const unsigned long deadline = millis() + timeoutMs;
  for (;;) {
    if (sntpAnswered()) return true;
    if (static_cast<long>(deadline - millis()) <= 0) return false;
    delay(100);
  }
}
}  // namespace

bool isHttpsUrl(const std::string_view url) {
  constexpr std::string_view scheme = "https://";
  if (url.size() < scheme.size()) return false;
  for (size_t i = 0; i < scheme.size(); ++i) {
    char c = url[i];
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    if (c != scheme[i]) return false;
  }
  return true;
}

void applyVerifiedTls(freeink::SecureHttpClient& http) { http.setCACert(XTEINK_CA_BUNDLE_PEM); }

void ensureClockForTls() {
  if (!clockSynced && sntpAnswered()) clockSynced = true;
  const uint32_t now = millis();
  const sntp_policy::Inputs in{clockSynced, esp_sntp_enabled(), trustedtime::trustedNow() != 0, waitedBefore,
                               now - lastWaitMs};
  const sntp_policy::Action action = sntp_policy::actionFor(in);
  if (action == sntp_policy::Action::Skip) return;

  waitedBefore = true;
  lastWaitMs = now;
  // StartAndWait: nothing started SNTP this boot. startSync() is a no-op while
  // SNTP runs, so an in-flight exchange is never restarted (Wait).
  if (action == sntp_policy::Action::StartAndWait) trustedtime::startSync();
  clockSynced = waitForSntp(CLOCK_SYNC_TIMEOUT_MS);
  if (clockSynced) {
    trustedtime::note();
    LOG_INF("TLS", "SNTP sync done in %lu ms", static_cast<unsigned long>(millis() - now));
  } else {
    // Not fatal here: a restored clock floor may still be good enough. If it
    // is not, wolfSSL rejects the certificate dates and the request fails.
    // Logged once per wait, not once per request.
    LOG_ERR("TLS", "SNTP sync failed; certificate date checks use the restored clock");
  }
}

}  // namespace tls_trust

// Hostname verification. SecureClient (freeink-sdk) sends SNI but never calls
// wolfSSL_check_domain_name(), so wolfSSL would accept any certificate that
// chains to a trusted root, whatever name it carries. The link step wraps
// wolfSSL_UseSNI (-Wl,--wrap=wolfSSL_UseSNI in platformio.ini): every SNI host
// also becomes the name the peer certificate must match (SAN/CN, wildcards per
// wolfSSL CheckHostName). wolfSSL skips the check when verification is off
// (setInsecure() callers), so only verified connections are affected.
//
// SecureClient ignores UseSNI's return value, so a failure here cannot simply
// be reported: it would leave a session with no name check. Instead the
// session's I/O is cut, and the handshake fails closed on its first read/write.
namespace {
int refuseIo(WOLFSSL* /*ssl*/, char* /*buf*/, int /*sz*/, void* /*ctx*/) { return WOLFSSL_CBIO_ERR_CONN_CLOSE; }

int failClosed(WOLFSSL* ssl) {
  wolfSSL_SSLSetIORecv(ssl, refuseIo);
  wolfSSL_SSLSetIOSend(ssl, refuseIo);
  return WOLFSSL_FAILURE;
}
}  // namespace

extern "C" int __real_wolfSSL_UseSNI(WOLFSSL* ssl, byte type, const void* data, word16 size);

extern "C" int __wrap_wolfSSL_UseSNI(WOLFSSL* ssl, byte type, const void* data, word16 size) {
  const int ret = __real_wolfSSL_UseSNI(ssl, type, data, size);
  if (ssl == nullptr || type != WOLFSSL_SNI_HOST_NAME) return ret;
  // DNS names are at most 253 characters; the SNI data is not NUL-terminated.
  char host[254];
  if (data == nullptr || size == 0 || size >= sizeof(host)) {
    LOG_ERR("TLS", "Unusable SNI host (len %u), refusing connection", static_cast<unsigned>(size));
    return failClosed(ssl);
  }
  memcpy(host, data, size);
  host[size] = '\0';
  if (wolfSSL_check_domain_name(ssl, host) != WOLFSSL_SUCCESS) {
    LOG_ERR("TLS", "Could not set expected certificate name for %s, refusing connection", host);
    return failClosed(ssl);
  }
  return ret;
}
