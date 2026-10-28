#!/bin/zsh
# Exercise USB plug-in N times without touching the cable, using the Pico
# Debug Probe: drop the USB pull-up (the host sees a detach), reset the
# RP2040, then wait for the Mac's "picolldp LLDP viewer" interface to come
# up and answer HTTP. With a -DUSB_TRACE=ON build the TinyUSB log of each
# cycle is dumped from RAM to usb_trace_<n>_<yes|no>.txt.
#
#   firmware/tools/usb_replug_test.sh build-w55rp20-trace/picolldp.elf 12 [outdir]
#
# macOS only (ifconfig/ipconfig). Needs openocd 0.12 and the toolchain's nm
# (set NM=/path/to/arm-none-eabi-nm if it is not on PATH).
ELF=${1:?usage: usb_replug_test.sh <picolldp.elf> [cycles] [outdir]}; N=${2:-10}; OUT=${3:-.}
NM=${NM:-arm-none-eabi-nm}
IF=$(networksetup -listallhardwareports | awk '/picolldp/{f=1} f&&/^Device:/{print $2; exit}')
sym() { $NM "$ELF" | awk -v s="$1" '$3==s{print "0x"$1}'; }
BUF=$(sym usb_trace_buf); POS=$(sym usb_trace_pos); NCM=$(sym ncm_interface)
oo() { openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg -c "adapter speed 2000" -c init "$@" -c shutdown 2>&1 | grep -E "^0x|dumped|Error"; }
echo "interface ${IF:-?} trace ${BUF:-off}"
fails=0
for c in $(seq 1 $N); do
  oo -c halt -c "mww 0x5011304c 0x00010000" -c "sleep 1500" -c "mww 0xe000ed0c 0x05fa0004" >/dev/null   # pull-up off, SYSRESETREQ
  ok=no
  for i in $(seq 1 25); do sleep 1; ip=$(ipconfig getifaddr $IF 2>/dev/null); if [ -n "$ip" ] && curl -s -m 2 -o /dev/null http://192.168.7.1/status.json; then ok=yes; break; fi; done
  sleep 3
  alt=$(oo -c "mdb $NCM 8" | awk '{print $6}')
  enums=""
  if [ -n "$BUF" ]; then
    oo -c "dump_image $OUT/usb_trace_$c.bin $BUF 32768" -c "mdw $POS" > $OUT/usb_trace_$c.meta
    pos=$((16#$(awk '/^0x/{print $2}' $OUT/usb_trace_$c.meta | tail -1)))
    head -c $pos $OUT/usb_trace_$c.bin > $OUT/usb_trace_${c}_$ok.txt; rm -f $OUT/usb_trace_$c.bin $OUT/usb_trace_$c.meta
    enums=" enumerations=$(grep -c 'USBD Bus Reset' $OUT/usb_trace_${c}_$ok.txt)"
  fi
  echo "cycle $c: $ok after ${i}s ip=${ip:-none} itf_data_alt=$alt$enums"
  [ $ok = no ] && fails=$((fails+1))
done
echo "$fails of $N cycles failed"
