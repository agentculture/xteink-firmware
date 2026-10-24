// xteink fork: host-side check of the firmware's TLS trust policy.
//
// Builds against the same wolfSSL release and user_settings.h the firmware
// links (Arduino-wolfSSL from .pio/libdeps) and the exact pinned bundle bytes
// (src/network/XteinkCaBundle.h), then performs one TLS handshake the way
// SecureClient does after the xteink changes: PEM bundle as trust anchors,
// SNI + expected certificate name, X25519 key share. Exit 0 = verified,
// 1 = handshake refused (prints the wolfSSL error), 2 = setup error.
//
// Usage: host_check <connect-host> <port> <expected-name>
// Driven by scripts/xteink-tls/run.sh; see docs/xteink/tls.md.

#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#include <wolfssl/ssl.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../../src/network/XteinkCaBundle.h"

// Heap accounting through wolfSSL's allocator hooks, to estimate what the
// bundle costs per handshake (host pointers are 8 bytes, the C3's are 4, so
// device figures run somewhat lower).
namespace {
size_t heapNow = 0;
size_t heapPeak = 0;
struct Hdr {
  size_t size;
  size_t pad;
};
void* trackMalloc(size_t n) {
  auto* h = static_cast<Hdr*>(std::malloc(sizeof(Hdr) + n));
  if (!h) return nullptr;
  h->size = n;
  heapNow += n;
  if (heapNow > heapPeak) heapPeak = heapNow;
  return h + 1;
}
void trackFree(void* p) {
  if (!p) return;
  Hdr* h = static_cast<Hdr*>(p) - 1;
  heapNow -= h->size;
  std::free(h);
}
void* trackRealloc(void* p, size_t n) {
  if (!p) return trackMalloc(n);
  void* q = trackMalloc(n);
  if (!q) return nullptr;
  const size_t old = (static_cast<Hdr*>(p) - 1)->size;
  std::memcpy(q, p, old < n ? old : n);
  trackFree(p);
  return q;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 4) {
    std::fprintf(stderr, "usage: %s <connect-host> <port> <expected-name>\n", argv[0]);
    return 2;
  }
  const char* host = argv[1];
  const char* port = argv[2];
  const char* name = argv[3];

  wolfSSL_SetAllocators(trackMalloc, trackFree, trackRealloc);
  wolfSSL_Init();
  const size_t heapBase = heapNow;
  WOLFSSL_CTX* ctx = wolfSSL_CTX_new(wolfSSLv23_client_method());
  if (!ctx) return 2;
  // XTEINK_TLS_BASELINE=1: upstream's old posture (no bundle, no verification),
  // only to compare heap figures. Never a pass/fail mode.
  const bool baseline = std::getenv("XTEINK_TLS_BASELINE") != nullptr;
  if (baseline) wolfSSL_CTX_set_verify(ctx, WOLFSSL_VERIFY_NONE, nullptr);
  const int loaded =
      baseline ? WOLFSSL_SUCCESS
               : wolfSSL_CTX_load_verify_buffer(ctx, reinterpret_cast<const unsigned char*>(XTEINK_CA_BUNDLE_PEM),
                                                std::strlen(XTEINK_CA_BUNDLE_PEM), WOLFSSL_FILETYPE_PEM);
  if (loaded != WOLFSSL_SUCCESS) {
    std::fprintf(stderr, "bundle load failed: %d\n", loaded);
    return 2;
  }
  const size_t heapAfterLoad = heapNow;
  const size_t peakDuringLoad = heapPeak;

  addrinfo hints{};
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* res = nullptr;
  if (getaddrinfo(host, port, &hints, &res) != 0 || !res) {
    std::fprintf(stderr, "resolve failed: %s\n", host);
    return 2;
  }
  const int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (fd < 0 || connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
    std::fprintf(stderr, "tcp connect failed: %s:%s\n", host, port);
    return 2;
  }
  freeaddrinfo(res);

  WOLFSSL* ssl = wolfSSL_new(ctx);
  wolfSSL_set_fd(ssl, fd);
  wolfSSL_UseSNI(ssl, WOLFSSL_SNI_HOST_NAME, name, static_cast<word16>(std::strlen(name)));
  wolfSSL_check_domain_name(ssl, name);  // what the firmware's UseSNI wrap adds
#if defined(HAVE_CURVE25519)
  wolfSSL_UseKeyShare(ssl, WOLFSSL_ECC_X25519);
#endif

  heapPeak = heapNow;
  const int ret = wolfSSL_connect(ssl);
  std::fprintf(stderr, "heap: ctx+bundle %zu B held (load peak %zu B), handshake peak %zu B\n",
               heapAfterLoad - heapBase, peakDuringLoad - heapBase, heapPeak - heapBase);
  int rc = 0;
  if (ret == WOLFSSL_SUCCESS) {
    std::printf("VERIFIED %s (name %s): %s %s\n", host, name, wolfSSL_get_version(ssl), wolfSSL_get_cipher(ssl));
  } else {
    const int err = wolfSSL_get_error(ssl, ret);
    char buf[WOLFSSL_MAX_ERROR_SZ];
    std::printf("REFUSED %s (name %s): %d %s\n", host, name, err, wolfSSL_ERR_error_string(err, buf));
    rc = 1;
  }
  wolfSSL_free(ssl);
  close(fd);
  wolfSSL_CTX_free(ctx);
  wolfSSL_Cleanup();
  return rc;
}
