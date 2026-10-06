# xteink-firmware

This is the [xteink](https://github.com/agentculture/xteink) fork of
[CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader).
It keeps CrossPoint's reader engine and adds what xteink needs: our own theme,
a button map with a zoom mode, Wi-Fi/server provisioning over USB, and a sync
client for the self-hosted xteink library server (LAN first, then
`xteink.culture.dev`).

## Supported Xteink models

| Model | MCU | PlatformIO env | Verification level |
|-------|-----|----------------|--------------------|
| X3 | ESP32-C3 | `default` (X3 + X4 in one image) | build-verified; hardware verification planned (unit on hand) |
| X4 | ESP32-C3 | `default` | build-verified |
| X4 Pro | ESP32-S3 | `x4pro` | build-verified |
| X4 Classic | ESP32-S3 | `x4c` | build-verified |

"Build-verified" means the env builds in `.github/workflows/xteink-ci.yml` on
every push and pull request. "Hardware-verified" means the xteink checklist was
run on a physical unit; that column is updated only with evidence.

Non-Xteink devices that upstream supports (reTerminal Sticky, M5PaperMono and
others) still build from this tree, but the xteink fork does not target or gate
them.

## Branches

- `develop` mirrors upstream `crosspoint-reader/crosspoint-reader` `develop`.
- `feat/*` branches carry xteink work and merge back through pull requests.

See [CONTRIBUTING.md](CONTRIBUTING.md) for which areas of the tree the fork may
change.
