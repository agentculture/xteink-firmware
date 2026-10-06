# Xteink X3 hardware checklist

Checks that need a physical X3 (ESP32-C3). Each fork task appends its own
section. Tick an item only with evidence: a serial log excerpt, a photo, or a
measured number.

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
