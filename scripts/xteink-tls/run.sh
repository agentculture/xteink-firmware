#!/usr/bin/env bash
# xteink fork: host-side TLS trust check (docs/xteink/tls.md).
#
# Compiles the firmware's wolfSSL (same release, same user_settings.h, same
# TLS-relevant -D flags as platformio.ini) for the host, links host_check.cpp
# against the pinned bundle, and runs:
#   - positive cases: the hosts the firmware must reach (sync, OTA)
#   - MITM case: a local TLS server with an untrusted self-signed certificate
#     for xteink.culture.dev -> must be REFUSED (fail closed)
#   - wrong-name case: a trusted chain for a different name -> must be REFUSED
#   - control: a public site whose root is not pinned -> must be REFUSED
#
# Usage: scripts/xteink-tls/run.sh [WOLFSSL_SRC]
#   WOLFSSL_SRC defaults to .pio/libdeps/default/Arduino-wolfSSL/src (run
#   `pio run -e default` once first). Needs gcc/g++, openssl, network access.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
WOLF="${1:-$ROOT/.pio/libdeps/default/Arduino-wolfSSL/src}"
OUT="${XTEINK_TLS_BUILD:-$ROOT/build/xteink-tls}"
[ -f "$WOLF/user_settings.h" ] || { echo "wolfSSL sources not found at $WOLF" >&2; exit 2; }
mkdir -p "$OUT/obj"

# Mirrors the wolfSSL-related build_flags in platformio.ini [base].
FLAGS=(-DWOLFSSL_USER_SETTINGS -DWOLFSSL_OPTIONS_H -DWOLFSSL_CLIENT_EXAMPLE -DWOLFSSL_TLS13
  -DWOLFSSL_HAVE_SP_ECC -DWOLFSSL_SP_SMALL -DHAVE_FFDHE_2048 -DHAVE_CURVE25519 -DHAVE_SNI
  -DWOLFSSL_KEY_GEN -DWC_RC2 -DWOLFSSL_ALT_CERT_CHAINS -DWOLFSSL_SHA384 -DWOLFSSL_SP_384 -I"$WOLF" -O1 -w)

if [ ! -f "$OUT/libwolfssl-host.a" ]; then
  echo "building wolfSSL for host from $WOLF ..."
  for c in "$WOLF"/src/*.c "$WOLF"/wolfcrypt/src/*.c; do
    o="$OUT/obj/$(basename "$(dirname "$(dirname "$c")")")_$(basename "$c" .c).o"
    gcc -c "${FLAGS[@]}" "$c" -o "$o"
  done
  ar rcs "$OUT/libwolfssl-host.a" "$OUT"/obj/*.o
fi
g++ -std=c++17 "${FLAGS[@]}" "$ROOT/scripts/xteink-tls/host_check.cpp" "$OUT/libwolfssl-host.a" -lm -o "$OUT/host_check"

CHECK="$OUT/host_check"
fail=0
expect() {  # expect <VERIFIED|REFUSED> <connect-host> <port> <name>
  local want="$1"; shift
  local line
  line="$("$CHECK" "$@" || true)"
  echo "$line"
  case "$line" in
    "$want"*) ;;
    *) echo "  ^ expected $want" >&2; fail=1 ;;
  esac
}

echo "== hosts the firmware must reach"
for h in culture.dev api.github.com github.com objects.githubusercontent.com release-assets.githubusercontent.com; do
  expect VERIFIED "$h" 443 "$h"
done
if getent hosts xteink.culture.dev >/dev/null; then
  expect VERIFIED xteink.culture.dev 443 xteink.culture.dev
else
  echo "(xteink.culture.dev does not resolve yet; skipped)"
fi

echo "== MITM: untrusted self-signed certificate for xteink.culture.dev"
tmp="$(mktemp -d)"
trap 'kill "${srv:-0}" 2>/dev/null || true; rm -rf "$tmp"' EXIT
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes -days 2 \
  -subj "/CN=xteink.culture.dev" -addext "subjectAltName=DNS:xteink.culture.dev" \
  -keyout "$tmp/key.pem" -out "$tmp/cert.pem" 2>/dev/null
port=$((20000 + RANDOM % 20000))
openssl s_server -quiet -accept "127.0.0.1:$port" -cert "$tmp/cert.pem" -key "$tmp/key.pem" -www \
  >/dev/null 2>&1 &
srv=$!
sleep 1
expect REFUSED 127.0.0.1 "$port" xteink.culture.dev

echo "== trusted chain, wrong name"
expect REFUSED github.com 443 xteink.culture.dev

echo "== root not pinned"
expect REFUSED www.microsoft.com 443 www.microsoft.com

if [ "$fail" -ne 0 ]; then
  echo "FAIL" >&2
  exit 1
fi
echo "PASS"
