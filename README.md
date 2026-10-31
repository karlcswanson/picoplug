# picoplug - pocket switch-port tester

A WIZnet W55RP20-EVB-Pico (RP2040 + W5500, about $10) that plugs into a
switch port and tells you, on your phone, what that port is. No app, no
account, no extra hardware: the board appears over USB-C as a network
adapter and serves one web page at http://192.168.7.1/ (picoplug.local).

What the page answers:

* **Which switch and port am I on?** LLDP and CDP neighbor: switch name,
  port, port description, PVID, VLAN names, voice VLAN, management IP,
  PoE budget, chassis, capabilities, LAG state.
* **Does the port work?** DHCP lease with every option the server sent,
  gateway / internet / DNS checks, spanning-tree role, state and root
  bridge from the port's BPDUs, passive 802.1X detection.
* **Is the AV network behind it healthy?** IGMP querier (version, who it
  is, snooping hint), PIM multicast router, PTP v1 (Dante) and PTP v2
  (AES67 / SMPTE 2059) masters and grandmaster, shown in their own card
  when the port carries any of it.
* **Tools:** ping with a live RTT plot, traceroute, and a port blinker
  that flaps the link so the switch LED shows you the port.
* **Two probes:** probes find each other on the wire (no IP needed), show
  which switch and port the other one is on, flag a PVID mismatch, and
  test whether the run between them is a direct cable or goes through a
  switch.
* **History** of every port visited, kept in the browser, exportable as CSV.
* **Form link:** a Form button that opens your own web form (a NOC
  request form, a ticket) with the switch, port and VLAN filled in.

The same page is served read-only on the port's own DHCP address, so the
tester can be left on a far port with USB power and read from a laptop on
the network. A raw NDJSON status stream is on TCP :9000 (USB side) for
scripts.

```
Switch --Ethernet--> W5500 (MACRAW) --SPI--> RP2040 --USB NCM--> phone / laptop browser
                                              lwIP + DHCP + HTTP :80 + NDJSON :9000
```

* `firmware/` - Pico SDK + TinyUSB + lwIP firmware (C)
* `firmware/web/` - the built-in page, a Svelte app built to one file and embedded at build time
* `firmware/test/` - host-side unit tests and a headless-Chrome smoke test of the page
* `docs/PROTOCOL.md` - JSON schema of `/status.json` and the :9000 stream, HTTP endpoints
* `THIRD_PARTY_LICENSES.md` - notices for the bundled Pico SDK, lwIP, TinyUSB, WIZnet ioLibrary and Lucide icons

## Build

Needs CMake, Python 3 and the Arm GNU toolchain (14.x with newlib; the
Homebrew `arm-none-eabi-gcc` lacks newlib).

```
git clone <this repo> picoplug && cd picoplug
git submodule update --init firmware/lib/pico-sdk firmware/lib/ioLibrary_Driver
git -C firmware/lib/pico-sdk submodule update --init lib/tinyusb lib/lwip
cmake -S firmware -B build-w55rp20 -DPICO_BOARD=wiznet_w55rp20_evb_pico \
      -DPICO_TOOLCHAIN_PATH=/path/to/arm-gnu-toolchain
cmake --build build-w55rp20
```

Hold BOOTSEL while plugging in the board and copy
`build-w55rp20/picolldp.uf2` to the RPI-RP2 drive. Once running, Setup
can reboot it into the bootloader for the next update. Host tests:
`cmake -S firmware/test -B build-test && cmake --build build-test && ctest --test-dir build-test`.

## Status

Firmware 0.3.2, verified on a W55RP20-EVB-Pico with macOS hosts against
an FS S3200 and a FortiSwitch-108F; a real Dante PTPv1 master was picked
up on the bench LAN. iPhone works from the home screen. Android (moto g play 2026,
Android 16) gets the link and loads the page with Wi-Fi turned off: with
Wi-Fi on, Android routes the browser over Wi-Fi and the tester is not
reachable.
