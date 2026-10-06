# On-device checklist for the Xteink X3

Run each section on a physical X3 with the fork's firmware flashed. Record the
result and the firmware version next to each item. Nothing here has been run on
hardware yet: "build-verified" is not "hardware-verified". Each fork task adds
its own section.

## Input, zoom and menu (t14)

Settings used: **Settings > Controls**. Defaults: Zoom Button (Reader) = Up,
Side Button Layout (Reader) = Prev/Next.

### Key map (expected)

| Logical button | Reader | Zoom mode | Reader menu |
|----------------|--------|-----------|-------------|
| Left (front) | previous page | smaller text | previous row |
| Right (front) | next page | larger text | next row |
| Confirm (front) | open reader menu | apply the size and leave zoom | select |
| Back (front) | back / leave book (existing behaviour) | cancel, keep the old size | close menu |
| Zoom key (side, default Up) | enter zoom mode | apply the size and leave zoom | n/a |
| Other side key (Down) | next page (Prev/Next layout) | larger text | next row |

### Checks

1. Verify which physical key is "top" on the X3 and that it is the one the
   firmware calls Up. If it is not, set **Zoom Button (Reader)** to Down in
   Settings > Controls and note the result here. No rebuild should be needed.
2. In a book, Left/Right turn pages back/forward. Confirm opens the reader
   menu. Back leaves the reader as before.
3. Press the zoom key: a panel appears at the bottom of the page listing the
   available point sizes with the current one highlighted. The page text
   behind the panel does not change yet.
4. With the built-in font, the panel shows 12 14 16 18. With a vector SD font it
   shows 8 to 22. With an installed `.cpfont` family it shows only the sizes
   that family ships.
5. Right moves the highlight to a larger size and Left to a smaller one, one
   step per press, stopping at the ends. Each press redraws only the panel (no
   reflow, no long wait).
6. Press the zoom key again (or Confirm): the book reflows once at the chosen
   size. The same passage is on screen (compare the first words of the page
   before entering zoom and after leaving it). Repeat at the start, middle and
   end of a chapter and across a chapter boundary.
7. Enter zoom, change the size, press Back: the panel closes, the size is the
   one from before and the page is unchanged (no reflow).
8. Enter zoom and leave without changing the size: no reflow happens.
9. Zoom in then zoom out again (e.g. 14 to 18 and back to 14): the reading
   position is still the same passage.
10. After a commit, the new size is what Settings > Reader Font Size shows, and
    survives a power cycle.
11. Rotate the screen (inverted or landscape) and repeat 3 to 6: Right still
    means larger text on the side of the screen the page-forward key is on.
12. The zoom key no longer turns pages. In Settings, set Zoom Button to Off:
    both side keys turn pages again and the zoom key does nothing.
13. Reader menu (Confirm): **Select Chapter** opens the chapter list and jumps
    there. **Go to %** opens the percent dialog (front Left/Right = 1%, side
    keys = 10%) and jumps. **Zoom** starts zoom mode.
14. Toolbar menu style (Settings > Reader > Reader Menu Style = Toolbar): the
    More panel lists **Zoom** and it starts zoom mode.
15. Check for e-ink ghosting around the zoom panel after several steps and after
    closing it. A grayscale text page may need a full refresh to look clean;
    note whether the firmware does that on its own.
16. Zoom mode does nothing in an XTC book (pre-rendered pages cannot reflow).

## Provisioning (t16)

Hardware checklist for serial provisioning (`docs/provisioning.md`). Use a
non-slim build (`default` or `gh_release`) with an SD card inserted.

- [ ] Settings > System lists "Provision via USB"; slim build does not.
- [ ] Outside the screen, send `XTEINK-PROV 1 e30=` (`{}`): the reply is
      `XTEINK-PROV-ERR 1 not_in_provisioning_mode`, and nothing is stored.
- [ ] Open the screen: it shows "Waiting for computer...". Back returns to
      Settings and the window closes again (repeat the previous check).
- [ ] A message sent before opening the screen is not applied when the screen
      opens (stale input is dropped).
- [ ] `{}` returns ACK with the correct MAC (matches About), firmware version,
      current network count and key id.
- [ ] One network with a password: ACK `networks_saved` grows, the network appears
      in Wi-Fi Networks, and the device joins it.
- [ ] `/.crosspoint/wifi.json` holds `password_obf` and no plaintext password.
- [ ] A full-size message (8 networks, 2 URLs, key; about 2.5 KB) sent in 128-byte
      chunks at 10 ms gaps is accepted with no dropped bytes.
- [ ] 9 networks gets `too_many_networks`; saved networks are unchanged.
- [ ] `replace_networks: true` leaves exactly the networks sent.
- [ ] Malformed base64, bad JSON, wrong version, over-long line (4 KB): each gets
      its error code and the device keeps running (no reboot, UI responsive).
- [ ] After provisioning the key and URLs survive a power cycle, an SD card
      swap, and "Clear Reading Cache" (NVS, not SD).
- [ ] The screen and the serial log never show the key, passwords or URLs
      (only the key id and counts).
- [ ] The ACK arrives intact while the 10 s `MEM` log line is also printing.
- [ ] Reboot with no host attached: no hang waiting on USB.

## Theme (t15)

The xteink theme ("bookplate") replaces the stock default. The repo has no
host-side renderer for firmware screens, so these screens have only been
build-verified. No preview images exist yet.

### Setup

1. Flash the `default` env build (`pio run -e default -t upload`).
2. On a unit with an existing `/.crosspoint/settings.json`, the saved theme
   wins. Either delete the `uiTheme` key or pick **Settings > Display > UI
   Theme > xteink**. On a fresh SD card, xteink should already be selected.
3. Open at least one EPUB that has a cover, so Home shows a continue-reading
   card. Then remove it from recents (or use a second SD card) to see the
   empty state.

### Theme checks

- [ ] **Boot.** A cold boot (power on, not a wake from sleep) shows the
  bookplate: a heavy outer frame with a hairline inner frame, an open-book
  mark, the serif "xteink" wordmark, a short rule and the italic tagline
  "Your library, on hardware you own". Below the frame are the booting label
  and the version. Nothing is clipped at 528 px width.
- [ ] **Home header.** The serif "xteink" wordmark sits top-left and the
  battery top-right. If the header clock is enabled, it is centered.
- [ ] **Home card.** The cover is on the left with a 1 px border. To its right
  are the bold serif title (up to 4 lines), the italic author, a short rule
  and "Continue reading". When the card is selected, a 3 px black frame
  surrounds it. Moving the selection away removes the frame without leaving
  a ghost after a FAST refresh.
- [ ] **Home empty state.** With no recent book, the card shows the book-mark
  placeholder plus "No open book" and the start-reading hint.
- [ ] **Home menu.** Rows are separated by hairline rules and have no icons.
  The selected row shows a black bookmark ribbon on its left edge and a bold
  label, with no grey fill.
- [ ] **Button hints.** Each label is open (no box) above a short black bar
  that sits over its physical button. All four line up with the X3's front
  buttons.
- [ ] **Library.** The header title is left-aligned over a 2 px rule. The
  active tab is a square black tab. The selected list row is marked with a
  triangle, not a filled bar.
- [ ] **Ghosting.** After 20 or more FAST-refresh navigations across Home,
  Library and back, no grey residue remains from selection markers.
- [ ] **Theme switching.** Switching to Classic, Lyra, Lyra Extended and
  RoundedRaff and back to xteink works without a reboot. Each stock theme
  looks as it does upstream.

### Evidence for the fork PR

- Home and Library: press **POWER + DOWN** on each screen. The firmware writes
  the exact framebuffer to `/screenshots/screenshot-<time>.bmp` on the SD card
  (`src/util/ScreenshotUtil.cpp`, triggered in `src/main.cpp`). Attach those
  BMPs, converted to PNG, and name them `docs/xteink/screens/home.png` and
  `library.png`.
- Boot: the splash shows only until Home loads and can't be captured as a
  screenshot, so photograph it head-on in even light and save it as
  `docs/xteink/screens/boot.jpg`.
- Also take one photo of Home on the device, so reviewers can judge contrast
  on the real panel and not only in the 1-bit framebuffer dump.
- Do not use `scripts/debugging_monitor.py` for X3 screenshots. It hardcodes 800x480
  when it decodes the serial dump, so it can't decode the X3's 792x528 frame.

## Security (t13)

Flash an `env:default` build with serial logging (`LOG_LEVEL=2`), and keep the
serial monitor open for every item.

- [ ] **Verified sync host.** Join Wi-Fi, then trigger a request to
  `https://xteink.culture.dev/` (sync or an OPDS entry pointing there). The
  log shows `[SecureClient] handshake ok` and the request succeeds. Record the
  free heap and max block from the `wolfSSL GET` debug line. While the zone
  serves the Let's Encrypt chain (X2 cross-signed by X1), this is one of the
  two heaviest verifications, about 39 KB peak on the host.
- [ ] **OTA check against the fork.** Settings > check for updates. The log
  shows a GET to `api.github.com/repos/agentculture/xteink-firmware/releases/latest`
  that verifies, and either "No update" or the fork's tag. It never contacts
  `crosspoint-reader/crosspoint-reader`.
- [ ] **OTA download.** With a fork release newer than the device, install it.
  The redirect to `release-assets.githubusercontent.com` (an RSA-4096 chain,
  the heaviest verification) completes, and the device boots the new image.
  Record `ESP.getFreeHeap()` / max block from the `wolfSSL GET` debug line.
- [ ] **MITM fails closed.** Run the bench test below. The download fails,
  the log shows `wolfSSL_connect failed (auto): -188` (or a later TLS error),
  and no file is written.
- [ ] **Wrong name fails closed.** Point an OPDS entry at
  `https://<IP of github.com>/`, or at any HTTPS host under a CNAME it has no
  certificate for. The log shows `-322` (name mismatch).
- [ ] **Unpinned root fails closed.** An OPDS feed on a DigiCert-issued host
  (for example `https://www.microsoft.com/`) fails with `-188`. It does not
  silently download.
- [ ] **Plain HTTP still works.** `http://xteink.local:8781/...` (or any LAN
  `http://` OPDS feed) downloads normally, with no TLS log lines.
- [ ] **Clock.** After a cold boot with the battery drained (clock restored
  from the NVS floor), the first HTTPS request logs either a successful SNTP
  sync or `SNTP sync failed`, and then a verified handshake. Measure the
  added delay before the first request.
- [ ] **Heap headroom.** Open a large EPUB, read a few pages, go to the
  library, and start an OPDS download over HTTPS. There is no `MEMORY_E` /
  `MP_MEM` (`-125`) in the log. Record the free heap reported before the
  handshake.
- [ ] **No open services at boot.** Cold-boot the device and wait 2 minutes.
  A phone Wi-Fi scan shows no `CrossPoint-Reader` network, and a port scan of
  the device's LAN IP after joining Wi-Fi for OPDS (`nmap -p 80,81,53,5353
  <ip>`) shows the ports closed.
- [ ] **Services only on demand.** Home > File Transfer > Create Hotspot
  brings up `CrossPoint-Reader` and the web UI. Leaving the screen takes the
  AP down; check with a phone scan within 30 seconds.

### Bench test: MITM with an untrusted CA

Run this on a laptop that the X3 joins as its Wi-Fi hotspot:

```bash
pip install mitmproxy
# Share the laptop's connection as a hotspot, then send the X3's HTTPS through
# mitmproxy in transparent mode (Linux, hotspot interface wlan0):
sudo sysctl -w net.ipv4.ip_forward=1
sudo iptables -t nat -A PREROUTING -i wlan0 -p tcp --dport 443 -j REDIRECT --to-port 8080
mitmproxy --mode transparent --showhost
```

mitmproxy signs on the fly with its own CA, which is not in the firmware
bundle. Trigger an OTA check and an OPDS HTTPS download on the X3. **Expected:**
both fail (`-188`), mitmproxy shows the client aborting the handshake, and no
request body or `Authorization` header appears in mitmproxy. Remove the
iptables rule afterwards
(`sudo iptables -t nat -D PREROUTING -i wlan0 -p tcp --dport 443 -j REDIRECT --to-port 8080`).

The host equivalent, with the firmware's own wolfSSL and bundle, runs without
hardware: `scripts/xteink-tls/run.sh` (see `docs/xteink/tls.md`).

## Sync (t17)

Setup: an xteink server reachable on the LAN (`xteink serve`, device app on
port 8781) with a registered device, the device key and URLs provisioned with
Settings > System > Provision via USB (docs/provisioning.md), and a saved
Wi-Fi network. Watch the serial log (`XSYNC` lines). Details of the client are
in `docs/xteink/sync.md`.

- [ ] **Manual sync over LAN.** Queue two books, then open Settings > System >
  Sync books now. The device reboots once ("Loading"), joins Wi-Fi, shows
  "Book 1 of 2" with a progress bar, then a result such as "Synced 14:02 · 2
  new". Both files are in `/xteink/` and open in the reader. The server shows
  both entries `delivered` and the device's `last_seen`, `free_sd_bytes` and
  firmware version.
- [ ] **Status line.** Back to Home: one line above the button hints reads
  "Synced HH:MM · 2 new" (no time on a board without RTC before the clock is
  set). Reboot: the line is still there. Check the xteink theme and Lyra.
- [ ] **xteink.local fallback.** Clear the LAN URL (provision `lan_url: ""`)
  with the server advertising `xteink.local`: sync still uses the LAN. Stop
  the server: the log shows "No answer from http://xteink.local:8781" within
  a few seconds, then the tunnel is tried.
- [ ] **Tunnel with verified TLS.** Off the LAN (phone hotspot), sync uses
  `https://xteink.culture.dev`. Note the `heap`/`max block` values in the
  "Sync start" log line and that no `ELOWMEM` or wolfSSL `MEMORY_E` appears.
  Repeat right after a long reading session (open a large EPUB, page 50+
  times, go Home, then Sync books now).
- [ ] **Resume.** Queue a large book (20 MB+). Mid-download, power-cycle the
  access point for ~10 s. The log shows a Range resume (or a restart) and the
  book still arrives with a matching sha256 and is acked once.
- [ ] **Cancel.** Press Back during a download: the screen shows "Sync error
  ECANCEL", no partial file is left in `/xteink/` or
  `/.crosspoint/xteink-download.part`, and the home line keeps the previous
  result.
- [ ] **Revoked key.** Revoke the device on the server, sync: "Sync error
  E401" and "Device key rejected. Re-pair this device."; the log shows no
  further requests after the status call.
- [ ] **Mirror deletes.** On a mirror-mode device, remove a delivered book
  from the library and sync: the file disappears from `/xteink/`. Annotate or
  replace a delivered file (any byte change) and remove it from the library:
  it is kept. A sideloaded copy of a library book is never touched.
- [ ] **Wi-Fi join hook.** With a book queued, open Home > OPDS (or Settings >
  Check for updates): on join a "Syncing books..." popup appears and the book
  arrives. From inside a book (reader menu > KOReader sync), joining Wi-Fi does
  **not** start a sync.
- [ ] **No key.** Erase the key (provision `device_key: ""`): Sync books now
  shows "No device key. Use Provision via USB first." and does not reboot or
  join Wi-Fi.
- [ ] **Key never on the wire elsewhere.** With mitmproxy on the tunnel path
  (see Security (t13) bench test), the sync fails closed and no
  `Authorization` header is visible.
