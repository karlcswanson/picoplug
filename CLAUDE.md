# Working on picoplug

Notes for anyone changing this code, with or without an AI assistant.
`README.md` covers what the tester is and how to build it; this file covers
how to work on it without breaking things.

## What it is

Firmware for the WIZnet W55RP20-EVB-Pico (RP2040 + W5500). The board shows
up as a USB Ethernet adapter (CDC-NCM) on a phone or laptop, listens
passively on the switch port (LLDP, CDP, STP, LACP, 802.1X, IGMP, PTP, DHCP)
and serves a single-page app at http://192.168.7.1 / http://picoplug.local.
Testers on the same network find each other over mDNS (`_picoplug._tcp`).

## Layout

* `firmware/src/` - C firmware: one module per protocol (`lldp.c`, `cdp.c`,
  `stp.c`, `lacp.c`, `eapol.c`, `mcast.c`, ...), `status.c` builds the JSON,
  `http_server.c` / `ws.c` serve it, `usb_net.c` and `eth_net.c` are the two
  lwIP netifs, `settings.c` is the versioned flash store, `peers.c` the mDNS
  table of other testers.
* `firmware/src/tusb_ncm_device.c` - a patched copy of TinyUSB 0.18's NCM
  driver (macOS link fix); CMake swaps it in for the SDK's copy.
* `firmware/web/` - the Svelte 5 page, built to one `dist/index.html` and
  embedded in the firmware at build time. See `firmware/web/README.md`.
* `firmware/test/` - host unit tests (C), page smoke test (headless Chrome),
  `web_mock.py` to run the page without hardware.
* `docs/PROTOCOL.md` - `/status.json`, the :9000 NDJSON stream and the HTTP
  endpoints. Keep it in step with `status.c`.

## Build and test

Needs CMake, Python 3, Node.js and the Arm GNU toolchain 14.x with newlib
(Homebrew's `arm-none-eabi-gcc` lacks newlib; pass `PICO_TOOLCHAIN_PATH`).
Run commands from the repo root.

```
npm ci --prefix firmware/web && npm run build --prefix firmware/web
cmake -S firmware/test -B build-test && cmake --build build-test && ctest --test-dir build-test
cmake -S firmware -B build-w55rp20 -DPICO_BOARD=wiznet_w55rp20_evb_pico -DPICO_TOOLCHAIN_PATH=...
cmake --build build-w55rp20
```

* Rebuild the page before running `test_page.py` or the firmware build, or
  they use a stale `dist/index.html`.
* `page.smoke` needs Chrome or Chromium and is skipped (77) without one.
* `captures` reads a private capture file that is not in the repo and is
  skipped when it is absent. Never add real captures from other people's
  networks to the repo; fixtures are synthetic or scrubbed (see
  `firmware/test/fixtures/README.md`).
* Page work without hardware: `python3 firmware/test/web_mock.py` then
  `BOARD=http://127.0.0.1:8080 npm run dev` in `firmware/web`.

## Flashing

* USB: hold BOOTSEL while plugging in, or open
  `http://192.168.7.1/reboot?bootsel=1`; copy `build-w55rp20/picolldp.uf2`
  to `RPI-RP2` (`cp -X` on macOS; retry if the volume is still mounting).
* SWD with a Pico Debug Probe: `firmware/tools/flash.sh <file.uf2>`, or
  openocd `program ... verify` then reset.
* The firmware target is still called `picolldp` (the old project name).

## Things that bite

* **Settings layout.** `settings.c` stores a versioned payload. Adding a
  field means bumping `VERSION`, keeping decode of every older version, and
  extending `test_settings.c`. `reset=1` keeps the board's MAC.
* **MAC address.** The W55RP20's flash unique ID is not unique between
  boards, so the Ethernet MAC is random on first boot and saved in settings.
* **IPv6 on the USB netif.** lwIP's mDNS probes over IPv4 and IPv6 and
  stalls if a netif has no IPv6 link-local address. Every netif running mDNS
  needs one.
* **Read-only side.** The page is read-only when reached from the switch
  side. `http_server.c` decides by comparing the connection's local address
  with the USB netif's IPv4 and IPv6 addresses (iOS reaches picoplug.local
  over IPv6 link-local).
* **Releases.** Every push to `main` makes a GitHub release. Bumping
  `FW_VERSION` in `firmware/src/board_config.h` makes a full release;
  otherwise it is a prerelease tagged with the commit.
* **Phones.** Android may route the page over Wi-Fi or cellular; Airplane
  mode fixes it. The page is plain http, so browser APIs that need a secure
  context (Web Share, clipboard on some browsers) are unavailable.

## Style for the page and docs

* Show what the network says, not what to do about it: no diagnoses,
  guesses or advice in the UI, and no warning colours for network conditions.
* Show values exactly as the switch sends them (port names, VLAN names);
  don't normalise.
* No emoji. Short first, details behind an expander.

## Licensing

Code bundled into the firmware or the page keeps its own license, listed
in `THIRD_PARTY_LICENSES.md`. When you add or upgrade a dependency that ends up in the UF2 or the page, update that file
(name, version, license, notice). Releases attach it next to the `.uf2`.

## Git

* Branch, then open a PR to `main`; CI builds, runs the host tests and
  publishes the release on merge.
* `ref/` is gitignored and private. Don't commit anything from it or link to
  it from public docs.
