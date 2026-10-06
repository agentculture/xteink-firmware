# Contributing to the xteink fork

The fork exists to stay cheap to rebase on upstream CrossPoint. Keep the diff
against `upstream/develop` inside these areas:

- **Theme and UI**: an xteink theme registered through upstream's theme
  mechanism (boot, home and library screens).
- **Input**: the button map and zoom mode, built on `MappedInputManager` and
  the existing button-remap settings.
- **Wi-Fi and sync configuration**: serial provisioning of saved networks,
  server URLs and the device key.
- **Sync client**: a new module implementing the xteink device protocol v1,
  plus minimal hooks to start it.
- **OTA source**: updates come from this fork's releases, never upstream's.
- **TLS verification**: verified HTTPS for sync, no `setInsecure()`.

Leave upstream's reader engine alone: EPUB layout, fonts, SD card handling and
power management. If a change there seems necessary, open an issue first and
record why.

## Reviewing the fork diff

```bash
git remote add upstream https://github.com/crosspoint-reader/crosspoint-reader.git  # once
scripts/xteink-diff.sh
```

The script fetches `upstream/develop` and prints `git diff --stat` against it,
so a reviewer can check that every changed path falls inside the areas above.
