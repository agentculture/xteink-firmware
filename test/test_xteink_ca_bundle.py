"""xteink fork: the pinned CA bundle is exactly the documented roots.

Checks src/network/XteinkCaBundle.h without a firmware build:
  - the PEM blocks decode, and their count matches XTEINK_CA_BUNDLE_COUNT;
  - each root's SHA-256 matches the pinned list below (and docs/xteink/tls.md),
    so a hand edit or a bad regeneration cannot swap a trust anchor silently;
  - with openssl on PATH, every root is a CA and stays valid for 2+ years.

Run: python3 -m pytest test/test_xteink_ca_bundle.py   (or python3 <file>)
"""

from __future__ import annotations

import base64
import hashlib
import re
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "src" / "network" / "XteinkCaBundle.h"
DOC = ROOT / "docs" / "xteink" / "tls.md"

# SHA-256 of each root's DER, in bundle order. Rotation updates this list,
# the generator (scripts/xteink-gen-ca-bundle.sh) and docs/xteink/tls.md.
PINNED = {
    "ISRG Root X1": "96BCEC06264976F37460779ACF28C5A7CFE8A3C0AAE11A8FFCEE05C0BDDF08C6",
    "ISRG Root X2": "69729B8E15A86EFC177A57AFB7171DFC64ADD28C2FCA8CF1507E34453CCB1470",
    "GTS Root R1": "D947432ABDE7B7FA90FC2E6B59101B1280E0E1C7E4E40FA3C6887FFF57A7F4CF",
    "GTS Root R4": "349DFA4058C5E263123B398AE795573C4E1313C83FE68F93556CD5E8031B3C7D",
    "Sectigo Public Server Authentication Root E46": "C90F26F0FB1B4018B22227519B5CA2B53E2CA5B3BE5CF18EFE1BEF47380C5383",
}

PEM_RE = re.compile(r"-----BEGIN CERTIFICATE-----(.*?)-----END CERTIFICATE-----", re.S)


def bundle_pem() -> str:
    text = HEADER.read_text()
    start = text.index("XTEINK_CA_BUNDLE_PEM[] =")
    end = text.index(";", start)
    # Join the C string literals; drop the per-root // comments.
    literals = re.findall(r'"((?:[^"\\]|\\.)*)"', text[start:end])
    return "".join(literals).replace("\\n", "\n")


def bundle_ders() -> list[bytes]:
    return [base64.b64decode("".join(b.split())) for b in PEM_RE.findall(bundle_pem())]


def test_count_matches_constant():
    text = HEADER.read_text()
    count = int(re.search(r"XTEINK_CA_BUNDLE_COUNT\s*=\s*(\d+)", text).group(1))
    assert len(bundle_ders()) == count == len(PINNED)


def test_fingerprints_are_pinned():
    got = [hashlib.sha256(der).hexdigest().upper() for der in bundle_ders()]
    assert got == list(PINNED.values())


def test_docs_list_every_fingerprint():
    doc = DOC.read_text().replace(":", "").upper()
    for name, fp in PINNED.items():
        assert fp in doc, f"{name} fingerprint missing from {DOC}"


def test_roots_are_cas_and_not_expiring():
    openssl = shutil.which("openssl")
    if not openssl:
        return  # structural checks above still ran
    two_years = str(2 * 365 * 24 * 3600)
    for der in bundle_ders():
        text = subprocess.run(
            [openssl, "x509", "-inform", "DER", "-noout", "-text"], input=der, capture_output=True, check=True
        ).stdout.decode()
        assert "CA:TRUE" in text
        alive = subprocess.run(
            [openssl, "x509", "-inform", "DER", "-noout", "-checkend", two_years], input=der, capture_output=True
        )
        assert alive.returncode == 0, "a pinned root expires within two years: rotate it (docs/xteink/tls.md)"


if __name__ == "__main__":
    for name, fn in list(globals().items()):
        if name.startswith("test_") and callable(fn):
            fn()
            print(f"ok {name}")
