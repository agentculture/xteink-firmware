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

## Tab navigation (d2)

Applies to Settings and Reader > Text Settings (every screen built on
`UiTabListActivity`). The Library keeps its own sort-tab navigation (front
Left on the sort tabs opens Search) and is not covered here.

- [ ] **Left/Right switch tabs at once.** In Settings, a single front Left or
  Right press moves to the previous/next tab (wrapping at the ends). No long
  press is needed. The button hints over those keys read "Left"/"Right".
- [ ] **Focus kind is kept.** With the tab band highlighted, Left/Right keep
  the band highlighted. With a row highlighted, Left/Right land on a row of
  the new tab (Settings: its first row; Text Settings: the row last used on
  that tab), never on the band.
- [ ] **Up/Down move within the tab.** Side Up/Down move one row per press.
  Down from the band enters the first row; Up from the first row returns to the
  band; Down from the last row wraps to the band.
- [ ] **Hold Up/Down pages.** On a long tab (Text Settings > Family with SD
  fonts, or a Settings tab that scrolls), holding Down jumps a page of rows at
  a time; holding Up jumps back.
- [ ] **Hold Left/Right repeats.** Holding Right steps through the tabs about
  twice a second.
- [ ] **Confirm/Back unchanged.** Confirm on the band still advances the tab;
  on a row it toggles/opens the setting. In Settings, Back from a row returns
  to the band and Back on the band leaves Settings (and saves); in Text
  Settings, Back closes the screen as before.
- [ ] **Front-button remap.** In Settings > Controls swap the front Left/Right
  mapping: tab switching follows the remapped keys.
- [ ] **Reader untouched.** In a book, Left/Right still turn pages and the
  zoom key still enters zoom mode (t14 checks 2 to 6).
- [ ] **Touch boards (if available).** On an X4 Pro, tapping a tab pill still
  switches tabs and tapping a row still selects it.

## Key mapping diagnostic

The X3 has 8 physical keys (top edge: a regular key and Power; a left and a
right key; four bottom keys in two pairs). freeink-sdk's `XteinkAdcLadder`
decode reads 7 logical keys: 4 on the GPIO1 ladder, 2 on the GPIO2 ladder and
Power on its own GPIO. Debug builds (`pio run -e default`, which has
`ENABLE_SERIAL_LOG` and `LOG_LEVEL=2`) log every key state change on the serial
console so the physical-to-logical mapping can be measured. Release builds
(`gh_release`, `LOG_LEVEL=1`) do not contain the code. No key mapping changes
until the values are recorded.

Open a serial monitor at 115200 baud, then press and release each physical key
on its own, then a few pairs. Each change prints one line, at most ten a
second:

```text
[XKEY] adc1=2873 adc2=4095 pwr=1 mask=0x02 -> Confirm
[XKEY] adc1=4095 adc2=4095 pwr=1 mask=0x00 -> none
```

- `adc1`, `adc2`: raw 12-bit `analogRead` of GPIO1 and GPIO2. Both idle near
  4095.
- `pwr`: level of the power GPIO (`BoardConfig::ACTIVE.input.power`).
- `mask` and the names: the logical buttons the firmware currently sees as
  pressed (bit 0 Back, 1 Confirm, 2 Left, 3 Right, 4 Up, 5 Down, 6 Power).
- `UNDECODED`: a reading is off the idle rail (below 4000) but no logical
  button is pressed. A brief one at a press or release edge is the 5 ms
  debounce. A steady one means a key whose ladder value the SDK does not map.
- While a key is held, a reading that moves more than 150 counts prints a new
  line, so a second key on the same ladder shows up.

SDK bands (`freeink-sdk/libs/hardware/InputManager/src/InputManager.cpp`,
`ADC_RANGES_1` / `ADC_RANGES_2`; `ADC_NO_BUTTON = 3900` in `InputManager.h`).
A reading `v` is button `i` when `ranges[i+1] < v <= ranges[i]`:

| Pin | Band | Logical button | SDK recorded average |
| --- | --- | --- | --- |
| GPIO1 | 3100 < v <= 3900 | Back | 3512 |
| GPIO1 | 2090 < v <= 3100 | Confirm | 2694 |
| GPIO1 | 750 < v <= 2090 | Left | 1493 |
| GPIO1 | v <= 750 | Right | 5 |
| GPIO2 | 1120 < v <= 3900 | Up | 2242 |
| GPIO2 | v <= 1120 | Down | 5 |
| either | v > 3900 | none (idle) | ~4095 |

Every reading at or below 3900 decodes to some button, so an extra key on one
of the two ladders shows up as an existing logical button, with a raw value
away from that button's average. A key that prints no line at all is on
neither ladder nor the power GPIO, and the firmware cannot see it today.

Record for each physical key: its position, the `adc1`/`adc2`/`pwr` values
and the decoded name. Check especially the top regular key (reported as no
reaction or a lock-up until Power) and which key, if any, decodes as Up (the
default zoom key, t14).

## X3 key profile (d5)

On a unit detected as X3 (`[MAIN] Hardware detect: X3`), the firmware reads
the keys through the X3 key profile (`src/xteink/X3KeyProfile.h`, applied once
in `MappedInputManager`). X4 builds and X4 units are unchanged. Host test:
`test/x3_key_profile`.

| Physical key | Raw (XKEY) | Logical | Home | Settings (tabbed) | Reader |
|--------------|-----------|---------|------|-------------------|--------|
| Left edge | adc2 ~2222 | Left | selection up | previous tab | previous page |
| Right edge | adc2 ~2 | Right | selection down | next tab | next page |
| Bottom 4.1 | adc1 ~3515 | Back | open last book | back | back (long: rotate, d5) |
| Bottom 4.2 | adc1 ~2686 | Confirm | select | toggle / open | menu (long: zoom, d5) |
| Bottom 4.3 | adc1 ~1486 | Up | selection up | row up | paragraph back (d3) |
| Bottom 4.4 | adc1 ~0 | Down | selection down | row down | paragraph forward (d3) |
| Top regular key | none | chip RESET | reboots (see Reset boot) | | |

How the existing settings combine with the profile:

- **Front button remap** (Settings > Controls > Remap): remaps Back, Confirm,
  Left and Right among 4.1, 4.2 and the two edge keys. The remap screen only
  accepts those four keys. 4.3/4.4 stay Up/Down, and the preview shows no label
  over them.
- **Side Button Layout** and **Zoom Button (Reader)** are hidden on the X3:
  there is no side page-turn key (pages turn with the edge keys) and zoom is
  long-press Confirm. Saved values are kept and still apply on an X4.
- **Front buttons follow orientation** and the touch-style orientation swap
  (`isNavDirectionSwapped`) flip the logical axes on top of the profile, as
  on the X4.
- Raw-index combos are not remapped: screenshot is Power + right edge, and
  recovery mode at boot is Power + 4.3 (logical Up).

Checks:

- [ ] Home: 4.3 moves the selection up, 4.4 moves it down. The edge keys also
  move it (left up, right down). Confirm opens, Back opens the last book.
- [ ] Settings: left/right edge switch tabs; 4.3/4.4 move between rows; the
  hints over 4.3/4.4 read Up/Down.
- [ ] A plain list (Library, file browser, reader menu): 4.3/4.4 and the edges
  move the selection.
- [ ] Reader: left edge previous page, right edge next page; 4.3/4.4 do not turn
  pages.
- [ ] Remap: swap Left and Right in Settings > Controls > Remap; the edges swap
  in the reader and on tabs. Reset with 4.3 (Up) restores the default.
- [ ] Settings > Controls has no Side Button Layout and no Zoom Button entry.
- [ ] Reader rotated to Inverted with Front buttons follow orientation on: the
  edge keys swap previous/next, and 4.3/4.4 swap up/down in lists.
- [ ] Percent dialog (Go to %): edges change by 1%, 4.3 by -10%, 4.4 by +10%.
- [ ] Keyboard entry: note which keys move the cursor; the side hints still sit
  at the edges (known: the keyboard's own hint labels were not adapted).

## Reader long-press (d5)

X3 only (X4 keeps its side zoom key and upstream long-press Back). Both holds
fire at 700 ms while the key is still down; the release is then swallowed, so
the short-press action does not also run. Policy is host-tested in
`test/reader_long_press`.

- [ ] Long-press Confirm (4.2) in a book: the zoom scale appears after about
  0.7 s, before releasing. Releasing does **not** open the reader menu.
- [ ] Short Confirm still opens the reader menu.
- [ ] In zoom mode the left/right edge keys step the size smaller/larger;
  Confirm (short, or held 0.7 s) applies with one reflow at the same passage;
  Back cancels without a reflow (t14 checks 5 to 9 still hold).
- [ ] Settings > Controls > Long-Press Menu shows **Zoom** where X4 shows
  Disabled. Pick Bookmark: long-press Confirm adds a bookmark instead of
  zooming. Set it back to Zoom.
- [ ] Long-press Back (4.1): portrait switches to Landscape CCW (and back on the
  next hold); the same passage is on screen; the choice survives leaving and
  reopening the book (Settings > Reader > Orientation shows it).
- [ ] From Inverted, a hold goes to Landscape CW and back to Inverted.
- [ ] Short Back still leaves the book as before. The long-press Back to the
  file browser no longer exists on the X3 (use Back Short to File Browser in
  Settings if needed).
- [ ] Long-press Back while the end-of-book menu is up does not rotate.
- [ ] XTC book: long-press Confirm/Back do nothing new (zoom and the rotate
  hold are EPUB-reader only).

## Reset boot (d5)

The X3's top regular key is the chip RESET. A reset reports
`ESP_RST_POWERON` with no wakeup cause, which upstream classifies as an unheld
power-button cold boot (`HalGPIO::getWakeupReason`, `lib/hal/HalGPIO.cpp`) and
puts back to sleep (`src/main.cpp`, "Power-button wake not held through
verification, sleeping"). On the X3 it now boots like a cold boot when the fuel
gauge reports at least 3% and 3400 mV. Decision logic: `src/xteink/ResetBoot.h`,
host tests in `test/reset_boot`.

| Boot | Reset reason / wake cause | X3 now | X4 |
|------|---------------------------|--------|----|
| Power key wake from deep sleep, held | DEEPSLEEP / GPIO | boots (verified) | same |
| Power key wake from deep sleep, released early | DEEPSLEEP / GPIO | sleeps (ghost-wake guard) | same |
| Power key cold boot, held | POWERON / none | boots (verified) | same |
| Top key (RESET), battery >= 3% and >= 3400 mV | POWERON / none | **boots, splash, then Home or the last book** | n/a |
| Top key or battery reconnect, battery low or gauge unreadable | POWERON / none | sleeps (upstream) | n/a |
| USB plugged into an off unit, charging | POWERON / none, USB | charge-sleeps (upstream AfterUSBPower) | same |
| Brownout, panic, software restart | BROWNOUT / SW / PANIC | boots (upstream Other) | same |

Checks (serial log at 115200):

- [ ] While reading, press the top key: the log shows `Power-on without held
  button: battery NN% NNNNmV -> reset key, booting`, the splash appears, and the
  device returns to the book (or Home if the book was not open).
- [ ] On Home, press the top key: boots to Home.
- [ ] Power key from sleep still needs the hold: a quick tap while asleep (Short
  Power Button = not Sleep) goes back to sleep; a hold wakes.
- [ ] With USB attached and charging, press the top key: note the result (the
  upstream AfterUSBPower path charge-sleeps; this was not changed).
- [ ] Low battery: below 3% (or gauge reading under 3.4 V) the top key leaves
  the device asleep and the log says `-> sleeping`; no repeated boots.
- [ ] Ten top-key presses in a row: each boots once, no boot loop, no crash
  screen.

## Paragraph scroll (d3)

X3 key profile, EPUB reader only: 4.4 (Down) skips the rest of the paragraph
at the top so the next paragraph starts at the top of the screen; 4.3 (Up)
brings the start of the paragraph above back to the top. (First built as one
line per press; changed to one paragraph per press on operator request,
2026-10-07.) A paragraph starts at a line whose gap from the line above is
wider than the line pitch (more than 1.25 lines): "Extra paragraph spacing"
(Settings > Reader, on by default) adds half a line between paragraphs, and
CSS margins, headings and images widen gaps too. The window is built from the
two cached pages (the page on screen and the next one): no reflow, pagination
unchanged. Window logic: `src/activities/reader/LineWindow.h`, host tests in
`test/line_window`.

- [ ] Press 4.4 once: the paragraph at the top disappears, the next paragraph
  is now the top line, and lines of the next page fill the bottom. Line
  spacing at the seam looks like normal line spacing.
- [ ] Press 4.4 on the last paragraph of a page: the view goes to the top of
  the next page (the page's top line is treated as a paragraph start, so a
  paragraph continued from the previous page shows its tail first; no text
  is skipped).
- [ ] Press 4.3 after scrolling down: the start of the paragraph above is on
  top again. From an unscrolled page, 4.3 shows the start of the previous
  page's last paragraph on top.
- [ ] Press 4.4 twice, then the right edge (next page): the new page keeps the
  same line offset (not paragraph-aligned). Then 4.3 aligns to the start of
  the paragraph at the top.
- [ ] "Extra paragraph spacing" off: there are no paragraph gaps to find, so
  4.4 acts as a page turn and 4.3 goes to the page top / previous page top.
- [ ] Image: an image counts as its own paragraph; scrolling past it takes it
  off the top in one step; an image at the top of the next page appears only
  once it fits (the bottom stays blank until then).
- [ ] Refresh: each step is a fast partial refresh; after the configured number
  of steps/turns (Settings > Refresh Frequency) a half refresh clears ghosting.
- [ ] The offset resets to the page top on zoom (enter and leave), rotation
  (long-press Back or the menu), Go to %, chapter select, bookmarks and links.
- [ ] Last page of a chapter: it shows unscrolled (no lines are lost, a few may
  repeat); 4.4 there does nothing; the right edge goes to the next chapter at the
  top.
- [ ] Reading position after a scroll survives leaving and reopening the book
  (it reopens at the top of the page the window started on).
- [ ] XTC book: 4.3/4.4 do nothing; pages only.
- [ ] Free heap in the `MEM` log line while scrolling stays within a few KB of
  plain page turns (two pages resident during a scrolled render).

## Wi-Fi auto-join timeout (fix E)

Saved-network auto-join (`WifiSelectionActivity`) now waits up to 15 s per
network (was 7 s; a range extender measured 8.3 s to associate) within a 45 s
budget for the whole auto-connect session. A network is started only with at
least 10 s of budget left, and its timeout is cut to what remains. Networks
after the first (last-used) attempt come from the scan, so unseen saved
networks are not tried.

- [ ] With the range extender as the only reachable saved network
  and a stale last-used network (iPhone) saved: the log shows `Attempting
  saved network: iPhone (5) (timeout 15000 ms)`, a failure after 15 s, then
  `<extender SSID> (timeout 15000 ms)` and a connection after about 8 s.
- [ ] With 4+ saved networks all failing: auto-connect gives up and shows the
  network list no later than about 45 s plus the scan time, and the log shows
  `Auto-connect budget used up` for the networks it skipped.
- [ ] Confirm during auto-connect still shows the network list at once.
- [ ] Manual connection timeout is unchanged (15 s).

## Sync free-space cache (fix F)

The status report's `free_sd_bytes` comes from a cache (RAM + NVS keys
`fs_free`/`fs_at` in the `xteink` namespace) instead of a 7 s free-cluster
scan on every sync. Policy: `src/xteink/FreeSpaceCache.h`, host tests in
`test/xteink_free_space`. The card is scanned when there is no cached value,
the clock is not trusted, the value is 24 h old or more, or the cached space
minus the queued downloads is under 64 MB. After downloads the cached value is
reduced by the bytes written. Space freed or used outside sync (USB drive, web
upload, deleting books) is not tracked until the next scan, at most 24 h later.

- [ ] First sync after flashing: the log shows `Free space query: NNNN ms`
  (the scan) and the server shows the device's free space.
- [ ] Second sync right after: the log shows `Free space: cached N bytes (S s
  old)` and no `Free space query` line; the time from `Inventory` to the LAN
  verdict drops by about 7 s.
- [ ] Download a 20 MB book: on the next sync the server's free space is about
  20 MB lower than before, without a scan.
- [ ] Power cycle and sync again: still cached (NVS), no scan.
- [ ] Nearly full card (fill it to under 64 MB free plus the queue): the sync
  scans before downloading, and the server marks a too-large item `sd_full`.
- [ ] Set the clock back or boot without a trusted time: the sync scans.
