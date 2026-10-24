# Serial provisioning (xteink protocol v1)

This fork can be provisioned over USB with saved Wi-Fi networks, the sync server
URLs and the device key. The only intended client is the `xteink` CLI
(`xteink device provision`); this page is the wire contract between the two.

## Device side

- Open **Settings > System > Provision via USB**. The screen shows
  "Waiting for computer..." and, after a successful message, the number of saved
  networks and the key id. It never shows passwords, URLs or the key.
- Outside that screen the firmware refuses every provisioning line with
  `not_in_provisioning_mode`. Leaving the screen (Back) closes the window.
- Storage:
  - Wi-Fi networks go through upstream's `WifiCredentialStore`
    (`/.crosspoint/wifi.json` on the SD card, passwords obfuscated with the
    hardware key, at most 8 networks, passwords at most 64 bytes).
  - The device key and both server URLs go to NVS, namespace `xteink`, keys
    `devkey`, `lan_url`, `tunnel_url`. Read them with `src/xteink/XteinkConfig.h`.
- The firmware never echoes the key or any password, never logs them, and wipes
  its receive buffer after each message. The reply carries only the key id.

### Availability

The serial port is the native USB Serial/JTAG of the ESP32-C3 (shows up as
`/dev/ttyACM0` on Linux, "USB JTAG/serial debug unit"). It is shared with
upstream's log output.

| Build | Provisioning |
|-------|--------------|
| `default`, `gh_release`, `gh_release_rc` and the other envs that define `ENABLE_SERIAL_LOG` | available |
| `slim` (`-UENABLE_SERIAL_LOG`) | **not available**: the serial port is never started, so there is no RX path, and the menu entry is not compiled in. Use a non-slim build to provision. |

## Wire format

One request line, one reply line. Lines end in `\n`; a trailing `\r` is
tolerated on requests.

```text
XTEINK-PROV <version> <base64(JSON)>\n
```

- `<version>`: decimal protocol version. This document is version `1`. Any other
  value gets `unsupported_version`.
- `<base64(JSON)>`: standard RFC 4648 base64 with `=` padding, no line breaks, of
  a UTF-8 JSON object.
- Limits: the whole line is at most 3072 bytes, the decoded JSON at most 2048
  bytes. Longer input gets `too_long` (the device discards the rest of an
  over-long line up to its newline).

### Request JSON

All fields are optional. A field that is absent is left unchanged; unknown
fields are ignored.

```json
{
  "networks": [{"ssid": "<ssid>", "password": "<password>"}],
  "replace_networks": false,
  "lan_url": "http://<lan-host>:<port>",
  "tunnel_url": "https://<tunnel-host>",
  "device_key": "xtd_<keyid8hex>_<secret>"
}
```

| Field | Rules |
|-------|-------|
| `networks` | array of at most 8 objects. `ssid`: 1-32 bytes, no NUL, unique within the message. `password`: 0-64 bytes (`""` or absent for an open network). Each entry is added, or updates the password of a saved network with the same SSID. |
| `replace_networks` | `true` removes every saved network first, so the saved set becomes exactly `networks`. Default `false`. |
| `lan_url` | `http://` or `https://`, at most 200 bytes, no spaces or control characters. `""` clears it. |
| `tunnel_url` | `https://` only (the sync client verifies TLS), same other rules. `""` clears it. |
| `device_key` | `xtd_` + 8 lowercase hex chars (key id) + `_` + secret of `A-Za-z0-9_-`, at most 128 bytes. `""` clears it. |

An empty object `{}` changes nothing and returns the reply, so the host can use
it to read the MAC and firmware version.

The 8-network cap is checked before anything is written: if the result would
exceed 8 saved networks the message is refused with `too_many_networks` and
nothing changes.

### Success reply

```text
XTEINK-PROV-ACK <version> <base64(JSON)>\n
```

```json
{"mac": "AA:BB:CC:DD:EE:FF", "firmware_version": "<version>", "networks_saved": 3, "key_id": "0a1b2c3d"}
```

- `networks_saved`: total networks saved on the device after the message.
- `key_id`: the 8 hex chars of the stored device key, or `""` if none is set.

### Error reply

```text
XTEINK-PROV-ERR <version> <code>\n
```

| Code | Meaning |
|------|---------|
| `not_in_provisioning_mode` | the Provision via USB screen is not open |
| `bad_format` | not `XTEINK-PROV <version> <payload>` |
| `unsupported_version` | `<version>` is not `1` |
| `too_long` | line over 3072 bytes or JSON over 2048 bytes |
| `bad_base64` | payload is not valid padded base64 |
| `bad_json` | payload is not a JSON object (or nests deeper than 4 levels) |
| `invalid_field` | a field has the wrong type, is out of range, or fails the rules above |
| `too_many_networks` | more than 8 networks in the message or in the result |
| `storage_error` | NVS or the SD card write failed |

The device never parses or logs the payload of a rejected message.

### Atomicity

NVS (key and URLs) and the SD card store (networks) are separate. The key and
URLs are committed together in one NVS commit, then the networks are applied. On
`storage_error` part of the message may already be applied; every operation is
idempotent, so the host retries the whole message.

## Coexisting with log output

Upstream logs on the same port, so the stream is not only protocol lines.

- The device sends a `\n` before each reply, so a reply always starts at the
  beginning of a line even if a log line was cut short.
- The host must scan incoming lines for the prefixes `XTEINK-PROV-ACK` and
  `XTEINK-PROV-ERR` and ignore everything else.
- While the screen is open the firmware does not treat `CMD:` lines as
  commands.

## Host-side flow

1. On the device, open Settings > System > Provision via USB.
2. Open the serial port at 115200 baud (the rate is ignored by USB CDC but is
   what upstream configures). Do not toggle DTR/RTS in a way that resets the
   chip: opening the port with `dtr=False, rts=False` avoids a reset on boards
   that map them to EN/BOOT.
3. Discard pending input. Optionally send `{}` as a hello to read the MAC and
   firmware version.
4. Build the JSON, base64 it, and send the request in chunks of at most 128
   bytes with about 10 ms between chunks. The device's USB receive queue is 256
   bytes and drops overflow; the main loop drains it continuously, but chunking
   keeps a 3 KB line from overrunning it.
5. Read lines until an `XTEINK-PROV-ACK` or `XTEINK-PROV-ERR` arrives (timeout 5
   s). On `storage_error`, retry; on any other error, fix the input.
6. Close the port. The user presses Back on the device.

Example (placeholders only, never real secrets):

```text
host:   XTEINK-PROV 1 eyJuZXR3b3JrcyI6W3sic3NpZCI6Ijxzc2lkPiIsInBhc3N3b3JkIjoiPHBhc3N3b3JkPiJ9XX0=
device: XTEINK-PROV-ACK 1 <base64 of {"mac":"AA:BB:CC:DD:EE:FF","firmware_version":"<version>","networks_saved":1,"key_id":""}>
```

## Testing

Host-side parsing and validation tests live in `test/xteink_provisioning`
(`cmake -S test -B build/test && cmake --build build/test --target
XteinkProvisioningTest`). On-device checks are in
[`docs/test-x3.md`](test-x3.md) under "Provisioning (t16)".
