#!/usr/bin/env python3
"""xteink fork guard: flash layout, image headroom and SDK pointer stay pinned.

Checks (each can be run alone):

  partitions   partitions.csv must hash to PARTITIONS_SHA256, the layout the fork
               ships on feat/issue-1. Changing the layout changes OTA slots on
               devices in the field, so a change must be deliberate: update the
               hash here and the docs in the same commit.
  sdk          the freeink-sdk gitlink must equal SDK_GITLINK. Bumping the SDK
               is a documented act: change SDK_GITLINK in the same commit that
               moves the pointer and say why in the commit message.
  image PATH   the built firmware image must fit the app0 OTA slot (size read
               from partitions.csv) and keep at least MIN_HEADROOM_PCT free.

Usage:
  python3 scripts/xteink-check-footprint.py                 # partitions + sdk
  python3 scripts/xteink-check-footprint.py --image .pio/build/default/firmware.bin
  python3 scripts/xteink-check-footprint.py --only image --image PATH

Exit 0 = clean, 1 = a check failed, 2 = usage / missing input.
"""

from __future__ import annotations

import argparse
import hashlib
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# sha256 of partitions.csv as shipped on feat/issue-1.
PARTITIONS_SHA256 = "8eab8ddac3fe3b14ab1f5b3a108b8982ac7a6851d7e3e28d1848d37f9a62d2ed"
# freeink-sdk submodule commit this fork is built against.
SDK_GITLINK = "9729236ce7b730b81b8fcceec4aa77051b6dfae3"
MIN_HEADROOM_PCT = 5.0


def check_partitions(root: Path) -> list[str]:
    path = root / "partitions.csv"
    if not path.is_file():
        return [f"{path}: missing"]
    got = hashlib.sha256(path.read_bytes()).hexdigest()
    if got != PARTITIONS_SHA256:
        return [
            "partitions.csv differs from the pinned feat/issue-1 layout "
            f"(sha256 {got}, expected {PARTITIONS_SHA256}); OTA slots must not move "
            "without updating PARTITIONS_SHA256 deliberately"
        ]
    return []


def check_sdk(root: Path, override: str | None) -> list[str]:
    sha = override
    if sha is None:
        try:
            out = subprocess.run(
                ["git", "-C", str(root), "ls-files", "-s", "freeink-sdk"],
                check=True, capture_output=True, text=True,
            ).stdout.split()
        except (OSError, subprocess.CalledProcessError) as exc:
            return [f"cannot read freeink-sdk gitlink: {exc}"]
        if len(out) < 2 or out[0] != "160000":
            return [f"freeink-sdk is not a gitlink in the index: {out!r}"]
        sha = out[1]
    if sha != SDK_GITLINK:
        return [
            f"freeink-sdk gitlink is {sha}, expected {SDK_GITLINK}; a deliberate SDK "
            "bump must change SDK_GITLINK in scripts/xteink-check-footprint.py"
        ]
    return []


def app0_size(root: Path) -> int:
    for line in (root / "partitions.csv").read_text().splitlines():
        cols = [c.strip() for c in line.split(",")]
        if cols and cols[0] == "app0" and len(cols) >= 5:
            return int(cols[4], 0)
    raise ValueError("no app0 row in partitions.csv")


def check_image(root: Path, image: Path) -> list[str]:
    if not image.is_file():
        return [f"{image}: firmware image not found"]
    slot = app0_size(root)
    size = image.stat().st_size
    free_pct = (slot - size) * 100.0 / slot
    print(f"image {image}: {size} B of {slot} B slot, {free_pct:.2f}% headroom")
    if size > slot:
        return [f"image is {size} B, larger than the {slot:#x} B app0 slot"]
    if free_pct < MIN_HEADROOM_PCT:
        return [f"headroom {free_pct:.2f}% is below the required {MIN_HEADROOM_PCT}%"]
    return []


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--root", type=Path, default=ROOT, help="repo root (for tests)")
    ap.add_argument("--image", type=Path, help="built firmware.bin to size-check")
    ap.add_argument("--only", choices=["partitions", "sdk", "image"], action="append")
    ap.add_argument("--sdk-sha", help="override the gitlink read (for tests)")
    args = ap.parse_args()

    default = ["partitions", "sdk"] + (["image"] if args.image else [])
    wanted = args.only or default
    if "image" in wanted and not args.image:
        print("--image PATH is required for the image check", file=sys.stderr)
        return 2

    problems: list[str] = []
    if "partitions" in wanted:
        problems += check_partitions(args.root)
    if "sdk" in wanted:
        problems += check_sdk(args.root, args.sdk_sha)
    if "image" in wanted:
        problems += check_image(args.root, args.image)

    for p in problems:
        print(f"FAIL: {p}", file=sys.stderr)
    if problems:
        return 1
    print("xteink footprint guards: ok (" + ", ".join(wanted) + ")")
    return 0


if __name__ == "__main__":
    sys.exit(main())
