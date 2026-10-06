#include "TlsTrust.h"

#include <Arduino.h>
#include <Logging.h>
#include <SecureHttpClient.h>
#include <TrustedTime.h>
#include <wolfssl/ssl.h>

#include <cstring>

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
// Bounded SNTP wait before the first https request of a boot. pool.ntp.org
// usually answers in well under a second; 5s caps the worst case.
constexpr uint32_t CLOCK_SYNC_TIMEOUT_MS = 5000;
bool clockSynced = false;
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
  if (clockSynced) return;
  clockSynced = trustedtime::syncNow(CLOCK_SYNC_TIMEOUT_MS);
  if (!clockSynced) {
    // Not fatal here: a restored clock floor may still be good enough. If it
    // is not, wolfSSL rejects the certificate dates and the request fails.
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
