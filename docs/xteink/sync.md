# xteink sync client (fork)

The firmware pulls books from the xteink server with device protocol v1. The
contract is `docs/device-protocol.md` in the xteink repo, and the server's
reference client is `tests/server/fake_device.py`. This client follows the
same flow.

## Code

| Piece | Where |
| --- | --- |
| Protocol logic with no device dependencies: JSON build and parse, server order, manifest, delete decision, error codes, status line | `src/xteink/SyncProtocol.{h,cpp}`, tested in `test/xteink_sync/` |
| Device engine: HTTP, SD, sha256, NVS | `src/xteink/XteinkSync.{h,cpp}` |
| "Sync books now" screen | `src/activities/settings/XteinkSyncActivity.{h,cpp}` |

Upstream files get only small hooks:

- `WifiSelectionActivity.cpp`: calls `xteink::sync::onStationJoined()` on
  every station join, next to the plugin-event drain.
- `SettingsActivity`: adds the **Sync books now** action under System.
- `HomeActivity::render`: calls `drawHomeStatusLine()`.
- `main.cpp` and `SilentRestart.h`: add the fresh-heap reboot target
  `silentRestartToXteinkSync()`, modelled on `silentRestartToJoinNetwork()`.
- `HalStorage::sdFreeBytes()`: a read-only accessor that runs
  SDCardManager's cached free-space query under the HAL mutex. Nothing else
  in the SD path changes.

## One sync

1. **Inventory.** The client reads `/.crosspoint/xteink-delivered.json`. This
   manifest maps sha256 to path and lists only files the server delivered.
   Each listed file still on the card is re-hashed. Entries whose file is gone
   are dropped. Sideloaded files never enter the manifest, so they are never
   reported and never deleted.
2. **Status, and choosing a server.** The client sends `POST
   /api/device/status` with `free_sd_bytes`, `firmware_version`, the previous
   `last_sync_result` and `last_error`, and the inventory. It tries each server
   in this order (decision c60):
   1. The provisioned LAN URL, or `http://xteink.local:8781` when none is set.
      This attempt uses a 4 s timeout. `.local` names resolve through lwIP's
      built-in mDNS query support (`CONFIG_LWIP_DNS_SUPPORT_MDNS_QUERIES=y` in
      the framework sdkconfig). That only sends a query and starts no mDNS
      responder, so the no-open-services guard is unaffected.
   2. The provisioned tunnel URL, or `https://xteink.culture.dev`. It must be
      https, verified against the pinned roots (`docs/xteink/tls.md`).

   The first server that returns any HTTP status is used for the rest of the
   sync.
3. **Queue.** `GET /api/device/queue`. The body is capped at 24 KB, and at
   most 32 items are taken per sync. The rest stay queued on the server.
4. **Each item.**
   - The item is streamed to `/.crosspoint/xteink-download.part` and hashed as
     it arrives.
   - A dropped connection resumes with `Range` inside the same sync
     (freeink `fetchResumable`). A `416` discards the partial bytes and starts
     again from 0, once.
   - Once the size and sha256 match, the file is renamed into `/xteink/`. The
     client never overwrites a sideloaded or user-modified file. If the title
     is taken, it uses `Title (id).ext`.
   - The manifest is saved, then the client sends `POST /api/device/ack`.
   - If the ack never arrived, the next sync finds the file in the manifest
     and acks it again instead of downloading it twice.
5. **Deletes** (mirror devices). A listed sha256 is deleted only if the server
   delivered it, the file is re-hashed now, and its hash still matches.
6. **Closing status.** A second `POST /api/device/status` carries
   `last_sync_result`, `last_error` and the updated inventory.

### Headers and the device key

Every request carries `Authorization: Bearer <key>` and
`X-Xteink-Protocol: 1`. The key is read from NVS (`xteink::config`) and never
logged. Log lines show only the key id.

The key goes only to the chosen origin:

- API calls never follow redirects.
- Downloads use `fetchResumable`. It passes `sameOrigin=false` once a
  redirect leaves the starting scheme, host or port, and the headers are then
  left off.
- Queue download URLs must be root-relative paths. Absolute and
  protocol-relative URLs are dropped.

The LAN URL may be plain http. The key then crosses the LAN in clear text,
which is the c60 trade-off.

## Errors

| Code | Cause | Device action |
| --- | --- | --- |
| `E401` | Key missing, rotated or revoked | Stop. The screen says re-pair |
| `E426` | Server does not speak protocol v1 | Stop. The screen says a firmware update is needed |
| `E422` | Server rejected a request body (firmware bug) | Reported in `last_error` |
| `ENET` | No server answered | |
| `ELOWMEM` | Heap below the floor for the connection | |
| `ESHA` | Downloaded bytes did not match, or ack `409` | Copy deleted. The item is fetched again next sync |
| `EDL`, `EHTTP`, `EPARSE`, `ESD` | Download incomplete, unexpected status, bad JSON, SD failure | |
| `ECANCEL` | Back pressed during a manual sync | Not recorded |
| `ENOKEY` | No device key provisioned | |

Per-item errors (`ESHA`, `EDL`, item `404`) skip that item and the sync
continues. `E401`, `E426`, `ENET` and `ELOWMEM` stop the sync.

## When it runs

- **Sync books now** (Settings > System). On non-touch boards it first reboots
  onto a fresh heap. The reboot is skipped on touch boards and when sleep is
  in progress, as for Join Network. It then joins Wi-Fi through
  `WifiSelectionActivity`, which auto-connects to saved networks, and shows
  progress. Back cancels. Leaving the screen reboots to Home, like the other
  Wi-Fi screens.
- **Wi-Fi join.** Any station join that goes through `WifiSelectionActivity`
  (OPDS, OTA, file transfer, clock sync, and so on) starts a sync under these
  conditions:
  - A device key is set.
  - No book is open. `ActivityManager::isReaderActivity()` checks the whole
    stack.
  - The heap allows plain HTTP.

  The sync shows a "Syncing books..." popup. After 60 s it stops starting new
  items and leaves the rest for the next sync.

The sync runs on the main loop task. That is the simplest safe option on the
single-core C3: there is no second task holding a TLS session and no shared
SD state. It is never started while the reader is open, so reading never
waits on the network.

### Heap (plan risk r15)

Verified TLS to `xteink.culture.dev` peaked at about 39 KB in the host
measurement. The client checks heap before every connection:

- https needs `MIN_TLS_FREE_HEAP + 6 KB` free and `MIN_TLS_MAX_ALLOC + 2 KB`
  as the largest block.
- http needs 16 KB free and an 8 KB largest block.

Below that, the server is skipped. If no server is left, the sync fails with
`ELOWMEM`.

These steps reduce peak heap:

- Before syncing, the client drops the rebuildable SD-font caches.
- Only one TLS session is open at a time: the API connection is closed
  before each download.
- The queue and manifest are parsed into small structs, and the JSON is
  freed.
- The status line state is a 40-byte static.

The manual path also reboots onto a fresh heap first.

## Status line

The home screen shows one line above the button hints: `Synced 14:02 · 2 new`,
`Synced 14:02`, or `Sync error E401`. The time comes from the RTC when the
board has one, otherwise from the system clock once it is trustworthy, and is
left out when neither is available. The result is stored in NVS (`xteink` /
`sync_last`), so it survives a reboot.

The line is drawn by `HomeActivity`, not by a theme, so it shows on every
list-home theme: xteink, Lyra, Classic and RoundedRaff. It is skipped when the
menu reaches the hint band, for example in landscape. The cover-grid home
does not show it.
