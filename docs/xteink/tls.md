# TLS trust and network exposure (xteink fork)

The fork verifies every HTTPS request that goes through
`HttpDownloader` (the xteink sync client, OPDS catalog and book downloads, and
OTA) against a small set of root certificates compiled into the firmware. It
also checks that the certificate names the host being contacted. Upstream
CrossPoint skips verification (`setInsecure()`), because its wolfSSL transport
shipped without a CA bundle.

Plain `http://` URLs still work and carry no certificate. LAN sync to
`http://xteink.local:8781` is unaffected.

## How it works

| Piece | Where | What it does |
| --- | --- | --- |
| Root bundle | `src/network/XteinkCaBundle.h` (generated) | Five PEM roots in flash, loaded by `wolfSSL_CTX_load_verify_buffer` on each connection |
| Trust hook | `src/network/TlsTrust.cpp` `tls_trust::applyVerifiedTls()` | `SecureHttpClient::setCACert(bundle)` instead of `setInsecure()`; called from `HttpDownloader.cpp` for every connection, redirects included |
| Hostname check | `src/network/TlsTrust.cpp` `__wrap_wolfSSL_UseSNI` + `-Wl,--wrap=wolfSSL_UseSNI` | freeink-sdk's `SecureClient` sets SNI but never calls `wolfSSL_check_domain_name()`. The link-time wrap makes the SNI host the expected certificate name. If that cannot be set, the session's I/O is cut so the handshake fails instead of continuing unchecked |
| Clock | `tls_trust::ensureClockForTls()` | Certificate dates are checked against `time()`. Before the first HTTPS request of each boot, one bounded SNTP sync (5 s) runs so a stale clock restored by `TrustedTime` cannot reject fresh certificates. There is no fallback that skips date checks |
| Build flags | `platformio.ini` `[base]` | `WOLFSSL_ALT_CERT_CHAINS`, `WOLFSSL_SHA384`, `WOLFSSL_SP_384` (see below) |

The TLS stack is unchanged: wolfSSL through freeink-sdk `SecureNet`, the same
transport OTA already used (`src/network/OtaUpdater.cpp`, `installUpdate()`).
`TlsTrust.cpp` refuses to compile without `FREEINK_NET_WOLFSSL` or
`WOLFSSL_ALT_CERT_CHAINS`, so any env that builds has the verified path.

### Why these wolfSSL flags

All three were found with `scripts/xteink-tls/run.sh` (see below). Without each
one, a verified handshake to a required host fails.

- `WOLFSSL_ALT_CERT_CHAINS`: Cloudflare (GTS) and GitHub (Sectigo) append a
  cross-signed root whose issuer is not pinned (GlobalSign Root CA and
  USERTrust ECC, respectively). Without alternate chains, wolfSSL rejects the
  whole chain with `-188` (no signer). With it, wolfSSL accepts the chain when
  the leaf reaches a pinned root, which is what OpenSSL and browsers do. The
  leaf must still chain to a pinned root.
- `WOLFSSL_SHA384`: the GTS WE1, Sectigo DV E36 and Let's Encrypt E-series
  intermediates are signed `ecdsa-with-SHA384`. Without it: `-232` (hash type
  not available).
- `WOLFSSL_SP_384`: P-384 math on wolfSSL's single-precision code. Without it,
  P-384 falls back to fast-math, and the handshake peak heap went from about
  24 KB to 41 KB in the host measurement. That is too much for the C3 heap.

## Pinned roots

Source: Mozilla's root program as shipped in Ubuntu 24.04 `ca-certificates`
20240203 (`/usr/share/ca-certificates/mozilla/*.crt`), selected 2026-10-06.
The SHA-256 values below are over the DER certificate. Before you trust a
regenerated bundle, cross-check them against the CA's own repository
(letsencrypt.org/certificates, pki.goog/repository, sectigo.com).

| Root | Key | Expires | SHA-256 | Why |
| --- | --- | --- | --- | --- |
| ISRG Root X1 | RSA 4096 | 2035-06-04 | `96:BC:EC:06:26:49:76:F3:74:60:77:9A:CF:28:C5:A7:CF:E8:A3:C0:AA:E1:1A:8F:FC:EE:05:C0:BD:DF:08:C6` | Let's Encrypt RSA. Today it anchors GitHub's release-asset CDN (`release-assets.githubusercontent.com`, `objects.githubusercontent.com`: YR1 ← Root YR ← X1) and is a Cloudflare edge CA |
| ISRG Root X2 | EC P-384 | 2040-09-17 | `69:72:9B:8E:15:A8:6E:FC:17:7A:57:AF:B7:17:1D:FC:64:AD:D2:8C:2F:CA:8C:F1:50:7E:34:45:3C:CB:14:70` | **xteink.culture.dev today** (Cloudflare: `*.culture.dev` leaf ← YE2 ← Root YE ← X2, with X2 also sent cross-signed by X1) |
| GTS Root R1 | RSA 4096 | 2036-06-22 | `D9:47:43:2A:BD:E7:B7:FA:90:FC:2E:6B:59:10:1B:12:80:E0:E1:C7:E4:E4:0F:A3:C6:88:7F:FF:57:A7:F4:CF` | Google Trust Services RSA (Cloudflare edge, RSA fallback) |
| GTS Root R4 | EC P-384 | 2036-06-22 | `34:9D:FA:40:58:C5:E2:63:12:3B:39:8A:E7:95:57:3C:4E:13:13:C8:3F:E6:8F:93:55:6C:D5:E8:03:1B:3C:7D` | **culture.dev apex today** (Cloudflare: leaf ← WE1 ← GTS Root R4) |
| Sectigo Public Server Authentication Root E46 | EC P-384 | 2046-03-21 | `C9:0F:26:F0:FB:1B:40:18:B2:22:27:51:9B:5C:A2:B5:3E:2C:A5:B3:BE:5C:F1:8E:FE:1B:EF:47:38:0C:53:83` | **api.github.com / github.com** (OTA release check and download redirect: leaf ← DV E36 ← E46) |

Chains as observed on 2026-10-06 with
`openssl s_client -showcerts -servername <host> -connect <host>:443`.
`xteink.culture.dev` began resolving during this work and serves a Let's
Encrypt `*.culture.dev` certificate, while the `culture.dev` apex serves a
Google Trust Services one from the same Cloudflare zone. Both CAs are
therefore in live use for the zone. `scripts/xteink-tls/run.sh` checks
`xteink.culture.dev` whenever it resolves.

Cloudflare Universal SSL can switch the zone between Let's Encrypt and Google
Trust Services at renewal, so both CAs' RSA and ECDSA roots are pinned.
Cloudflare's third Universal SSL CA, SSL.com, is **not** pinned. If the zone
ever gets an SSL.com certificate, sync fails closed until the root is added or
the zone's CA is pinned in the Cloudflare dashboard (SSL/TLS > Edge
Certificates > Certificate Authority). Pinning the zone's CA there is
recommended.

### Consequence for OPDS and other servers

This is an explicit allowlist, not a general web trust store. An OPDS server or
download link whose certificate does not chain to one of these five roots
(for example DigiCert-issued or self-signed) now fails with an HTTP error
instead of downloading over unauthenticated TLS. Let's Encrypt, which most
self-hosted servers use, is covered. A server on the LAN can use plain `http://`.

### Not covered by this change

These paths still call `setInsecure()` and are outside the sync/OTA scope of
t13. They are tracked as follow-ups:

- `lib/KOReaderSync/KOReaderSyncClient.cpp` (user-configured KOSync servers)
- `src/util/PluginHttp.cpp` (plugin API calls)
- `src/network/CrossPointWebServer.cpp` plugin download relay
  (`fetchResumable` with `setInsecure()`)

`HttpDownloader::downloadToFile(..., downgradeRedirectsToHttp=true)` keeps
upstream's opt-in for content-encrypted downloads: a followed redirect may
step down to plain `http`. No xteink sync or OTA path uses it.

## Cost

Measured on env `default` (ESP32-C3), against the pre-fork baseline
(5,776,409 B flash, 58,984 B RAM):

- Flash: 5,810,031 B, which is **+33,622 B**. The ELF symbol sizes break this
  down as: SHA-384/512 about 16.0 KB, SP P-384 about 7.6 KB, the PEM bundle
  6,240 B (`.rodata`), and `TlsTrust` code about 0.4 KB.
- Static RAM: 58,984 B, unchanged. The bundle stays in flash.
- Heap per verified handshake, from the host build (64-bit, so device figures
  are somewhat lower):
  - The CTX plus the parsed roots hold about 4.1 KB, against about 2.0 KB for
    an unverified CTX. Loading the roots peaks at about 7.1 KB.
  - The handshake peak with ECDSA chains (culture.dev, GitHub API) is about
    24 KB, against 16 to 19 KB unverified.
  - The handshake peak with an RSA-4096 signature in the chain is about
    39 to 41 KB, against 20 to 32 KB unverified. This covers the GitHub
    release CDN (Root YR cross-signed by X1) and **xteink.culture.dev**
    (X2 cross-signed by X1). The RSA-4096 signature runs on fast-math. This is
    the case to watch on the X3 (see the hardware checks).

Two mitigations were measured but not applied:

- **Pin the Cloudflare zone's CA to Google Trust Services** (dashboard:
  SSL/TLS > Edge Certificates). The GTS ECDSA chain peaks at about 24 KB,
  which brings xteink.culture.dev down to the culture.dev apex figure. This
  needs no firmware change and is recommended.
- **`-DWOLFSSL_HAVE_SP_RSA -DWOLFSSL_SP_4096`.** RSA on SP math lowers the
  RSA-4096 cases to about 33 KB. It is not applied because it also switches
  the RSA backend that ContentProtection uses (key generation and the
  4096-bit account certificate). That needs its own hardware validation and
  costs more flash.

## Host check: `scripts/xteink-tls/run.sh`

```bash
pio run -e default                 # once, fetches wolfSSL into .pio/libdeps
scripts/xteink-tls/run.sh          # needs gcc/g++, openssl, network
```

The script compiles the firmware's own wolfSSL sources and `user_settings.h`
for the host with the same TLS `-D` flags, and links `host_check.cpp`, which
includes the real `XteinkCaBundle.h`. It then performs handshakes the way
`SecureClient` does after this change (bundle, SNI plus expected name, X25519).
It expects:

- **VERIFIED:** culture.dev, api.github.com, github.com,
  objects.githubusercontent.com and release-assets.githubusercontent.com,
  plus xteink.culture.dev once it resolves.
- **REFUSED (MITM):** a local `openssl s_server` presenting a self-signed
  certificate for `xteink.culture.dev` fails with `-188` (no signer).
- **REFUSED (wrong name):** github.com's valid chain, presented when
  `xteink.culture.dev` was expected, fails with `-322` (peer subject name
  mismatch).
- **REFUSED (unpinned root):** `www.microsoft.com` fails with `-188`.

`test/test_xteink_ca_bundle.py` pins the bundle's fingerprints to this page.

## Rotating roots

1. Pick the change, such as a new CA for the Cloudflare zone, a GitHub CA
   change, or a root that expires within two years (the bundle test fails at
   that point).
2. Edit `ROOTS=(...)` in `scripts/xteink-gen-ca-bundle.sh` and run it. It
   reads `/etc/ssl/certs/<Name>.pem` (Mozilla roots), or pass another
   directory as the first argument.
3. Update the table above and `PINNED` in `test/test_xteink_ca_bundle.py`.
   Cross-check each fingerprint against the CA's website.
4. Run `python3 test/test_xteink_ca_bundle.py`, `scripts/xteink-tls/run.sh`
   and `pio run -e default`, and record the flash delta here.
5. Ship the change as a normal OTA release **before** the old root stops
   being used. Devices can only receive the new bundle over a connection the
   old bundle still verifies. If GitHub's CA changes first, OTA breaks and
   devices need a USB or SD-card update.

## No open services

The file-transfer web server (HTTP port 80, WebSocket port 81), the
captive-portal DNS server, mDNS, and the open `CrossPoint-Reader` access point
start only after an explicit menu action. Upstream already behaves this way.
The fork pins it with a guard instead of changing code:

- Home menu `FILE_TRANSFER` → `HomeActivity::onFileTransferOpen()`
  (`src/activities/home/HomeActivity.cpp:322-323`, `:574`) →
  `ActivityManager::goToFileTransfer()`
  (`src/activities/ActivityManager.cpp:251-253`).
- `CrossPointWebServerActivity::onEnter()` opens
  `NetworkModeSelectionActivity` and starts nothing until the user picks a
  mode (`src/activities/network/CrossPointWebServerActivity.cpp:89-106`).
- The open AP (`WiFi.softAP`, SSID `CrossPoint-Reader`, no password) is only
  in `startAccessPoint()`, reached from the **Create Hotspot** choice
  (`CrossPointWebServerActivity.cpp:238-289`).
- The web server is only constructed in
  `CrossPointWebServerActivity::startWebServer()` (`:303`) and
  `CalibreConnectActivity::startWebServer()`
  (`src/activities/network/CalibreConnectActivity.cpp:94`).
- The only boot path into these activities is `goToJoinNetwork()`
  (`src/main.cpp:629-632`). It runs only after
  `silentRestartToJoinNetwork()` armed a RAM-only flag, and that function is
  called only when the user picks **Join Network**
  (`CrossPointWebServerActivity.cpp:144-146`, `src/main.cpp:201-214`). The flag
  needs a magic value and does not survive power loss
  (`src/main.cpp:139-149`, `:450-457`).
- Joining Wi-Fi for OPDS, OTA or sync (`WifiSelectionActivity`) does not start
  any server.

`scripts/xteink-check-services.py`, which CI runs, fails if any of these call
sites (soft AP, web/WebSocket/DNS servers, mDNS, the activity launches) appears
anywhere else. It also fails if `setInsecure()` comes back into
`HttpDownloader.cpp` or the upstream release feed comes back into
`OtaUpdater.cpp`.

## OTA source

`src/network/OtaUpdater.cpp` reads
`https://api.github.com/repos/agentculture/xteink-firmware/releases/latest`,
built from one constant (`OTA_RELEASE_REPO`). Release assets must keep
upstream's naming, `crosspoint-<tag>-<device>.bin` with `x3-x4` for the C3
image, which `.github/workflows/release.yml` produces. Tags must be
`X.Y.Z`.

`isUpdateNewer()` parses `X.Y.Z` from both the tag and `CROSSPOINT_VERSION`.
Release envs set `CROSSPOINT_VERSION` to `[crosspoint] version` from
`platformio.ini`. Dev builds get `<version>-dev-<branch>-<sha>` from
`scripts/git_branch.py`. A tag or version that does not parse as three
integers is now never treated as newer. Upstream read uninitialized integers
in that case.
