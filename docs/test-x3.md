# X3 hardware checklist

Checks that need a physical Xteink X3 (792x528, 3.7", ESP32-C3). Each fork
task adds its own section. Tick a box only after running the check on the
device, and attach the evidence to the fork PR.

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

### Checks

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
