# X3 hardware checklists

On-device checks that cannot run in CI. Each task adds its own section.

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
