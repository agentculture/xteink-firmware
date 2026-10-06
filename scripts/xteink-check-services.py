#!/usr/bin/env python3
"""xteink fork guard: no listening service starts without an explicit menu action.

The file-transfer web server (HTTP :80, WebSocket :81), the captive-portal DNS
server, mDNS and the open "CrossPoint-Reader" access point may only come up
from Home > File Transfer (HomeActivity::onFileTransferOpen) followed by a
choice in NetworkModeSelectionActivity. Upstream already behaves that way;
this check pins each call site so a rebase or a new feature cannot quietly
add another entry point (for example on boot or on Wi-Fi join).

Each rule names a pattern and the only files allowed to contain it. A match
anywhere else under src/ or lib/ fails the check. If you add a legitimate
call site, update the rule and docs/xteink/tls.md ("No open services").

Usage: python3 scripts/xteink-check-services.py   (exit 0 = clean)
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCAN_DIRS = ("src", "lib")
SUFFIXES = {".c", ".cpp", ".h", ".hpp"}

# (description, regex, allowed files relative to the repo root)
RULES: list[tuple[str, str, set[str]]] = [
    (
        "soft AP start",
        r"\bsoftAP\s*\(|WIFI_AP\b|WIFI_AP_STA\b|WIFI_MODE_APSTA\b",
        {
            "src/activities/network/CrossPointWebServerActivity.cpp",
            # Reads the mode to pick the AP IP for display; starts nothing.
            "src/network/CrossPointWebServer.cpp",
        },
    ),
    (
        "file-transfer web server construction",
        r"new\s+CrossPointWebServer\b|make_unique<\s*CrossPointWebServer\s*>|makeUniqueNoThrow<\s*CrossPointWebServer\s*>",
        {
            "src/activities/network/CrossPointWebServerActivity.cpp",
            "src/activities/network/CalibreConnectActivity.cpp",
        },
    ),
    (
        "raw HTTP/WebSocket/TCP server",
        r"\bWiFiServer\b|\bNetworkServer\b|new\s+(\(std::nothrow\)\s*)?WebSocketsServer\b|:\s*WebServer\s*\(|\bAsyncWebServer\b",
        {"src/network/CrossPointWebServer.cpp"},
    ),
    (
        "captive-portal DNS server",
        r"new\s+DNSServer\b|make_unique<\s*DNSServer\s*>",
        {"src/activities/network/CrossPointWebServerActivity.cpp"},
    ),
    (
        "mDNS responder",
        r"\bMDNS\.begin\s*\(",
        {
            "src/activities/network/CrossPointWebServerActivity.cpp",
            "src/activities/network/CalibreConnectActivity.cpp",
        },
    ),
    (
        "File Transfer activity launch",
        r"make_unique<\s*CrossPointWebServerActivity\s*>|makeUniqueNoThrow<\s*CrossPointWebServerActivity\s*>",
        {"src/activities/ActivityManager.cpp"},
    ),
    (
        "Calibre connect activity launch",
        r"make_unique<\s*CalibreConnectActivity\s*>|makeUniqueNoThrow<\s*CalibreConnectActivity\s*>",
        {"src/activities/network/CrossPointWebServerActivity.cpp"},
    ),
    (
        "goToFileTransfer() caller (must be the Home menu item)",
        r"\bgoToFileTransfer\s*\(\s*\)\s*;",
        {"src/activities/home/HomeActivity.cpp"},
    ),
    (
        # Only reached after the user picked Join Network: the activity arms a
        # silent reboot (main.cpp silentRestartToJoinNetwork) and main.cpp resumes it.
        "goToJoinNetwork() caller (silent-reboot resume only)",
        r"\bgoToJoinNetwork\s*\(\s*\)\s*;",
        {"src/main.cpp"},
    ),
    (
        "silentRestartToJoinNetwork() caller (Join Network menu choice only)",
        r"\bsilentRestartToJoinNetwork\s*\(\s*\)\s*;",
        {"src/activities/network/CrossPointWebServerActivity.cpp"},
    ),
]

# (description, regex, files that must never contain it) -- t13 TLS/OTA pins.
FORBIDDEN: list[tuple[str, str, set[str]]] = [
    (
        "unverified TLS in the shared downloader (sync, OPDS, OTA)",
        r"\bsetInsecure\s*\(",
        {"src/network/HttpDownloader.cpp", "src/network/TlsTrust.cpp", "src/network/OtaUpdater.cpp"},
    ),
    (
        "upstream OTA release feed",
        r"crosspoint-reader/crosspoint-reader/releases",
        {"src/network/OtaUpdater.cpp", "src/network/OtaUpdater.h"},
    ),
]

COMMENT_LINE = re.compile(r"^\s*(//|\*|/\*)")
# Function declarations ("void goToFileTransfer();") name a call site's target,
# they do not call it.
DECLARATION = re.compile(r"^\s*(static\s+)?void\s+[\w:]+\s*\(\s*\)\s*;")


def scan() -> list[str]:
    errors: list[str] = []
    compiled = [(desc, re.compile(rx), allowed) for desc, rx, allowed in RULES]
    for top in SCAN_DIRS:
        for path in sorted((ROOT / top).rglob("*")):
            if path.suffix not in SUFFIXES or not path.is_file():
                continue
            rel = path.relative_to(ROOT).as_posix()
            for lineno, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
                if COMMENT_LINE.match(line) or DECLARATION.match(line):
                    continue
                for desc, rx, allowed in compiled:
                    if rx.search(line) and rel not in allowed:
                        errors.append(f"{rel}:{lineno}: {desc} outside allowed files: {line.strip()}")
    for desc, rx, files in FORBIDDEN:
        pattern = re.compile(rx)
        for rel in sorted(files):
            path = ROOT / rel
            if not path.is_file():
                continue
            for lineno, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
                if not COMMENT_LINE.match(line) and pattern.search(line):
                    errors.append(f"{rel}:{lineno}: {desc}: {line.strip()}")
    return errors


def main() -> int:
    errors = scan()
    if errors:
        print("xteink service guard FAILED:", file=sys.stderr)
        for e in errors:
            print("  " + e, file=sys.stderr)
        return 1
    print(f"xteink service guard: OK ({len(RULES)} placement rules, {len(FORBIDDEN)} forbidden patterns)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
