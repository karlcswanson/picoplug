#!/bin/sh
# Flash a UF2 onto an RP2040 board via a Pico Debug Probe (SWD) without
# touching BOOTSEL: jump into the bootloader over SWD, wait for the
# RPI-RP2 / RP2350 volume, copy the file.
#   firmware/tools/flash.sh build-w55rp20/picolldp.uf2
set -e
UF2=${1:?usage: flash.sh <file.uf2>}
HERE=$(cd "$(dirname "$0")" && pwd)
if [ ! -d /Volumes/RPI-RP2 ] && [ ! -d /Volumes/RP2350 ]; then
    openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg -f "$HERE/bootsel.cfg" >/dev/null 2>&1 || true
fi
for i in $(seq 1 30); do
    for v in /Volumes/RPI-RP2 /Volumes/RP2350; do
        if [ -d "$v" ]; then
            sleep 1
            for t in 1 2 3 4 5; do
                if cp "$UF2" "$v/" 2>/dev/null; then echo "flashed $UF2 via $v"; exit 0; fi
                sleep 1
            done
        fi
    done
    sleep 1
done
echo "no bootloader volume appeared" >&2
exit 1
