/*
 * usb_trace.c - TinyUSB debug log into a RAM ring buffer (build with
 * -DUSB_TRACE=ON). Nothing reads it on the device; dump it over SWD:
 *
 *   openocd ... -c "dump_image trace.bin <usb_trace_buf> <USB_TRACE_SIZE>"
 *
 * (usb_trace_pos bytes are valid; the buffer stops filling when full). Used to diagnose USB enumeration problems
 * that leave the network link dead (the CDC log would be dead too).
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hardware/sync.h"
#include "pico/time.h"

#define USB_TRACE_SIZE 32768

char usb_trace_buf[USB_TRACE_SIZE];
volatile uint32_t usb_trace_pos;      /* next write offset (mod size) */
volatile uint32_t usb_trace_total;    /* bytes ever written; > size means wrapped */
static bool at_line_start = true;

/* Keeps the first USB_TRACE_SIZE bytes after boot (enumeration is what we
 * want to see); later output is counted in usb_trace_total but dropped. */
static void put(const char *s, int n)
{
    for (int i = 0; i < n; i++) {
        if (usb_trace_pos < USB_TRACE_SIZE) usb_trace_buf[usb_trace_pos++] = s[i];
        usb_trace_total++;
    }
}

int usb_trace_printf(const char *fmt, ...)
{
    char tmp[160];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n <= 0) return 0;
    if ((size_t)n >= sizeof tmp) n = sizeof tmp - 1;

    uint32_t irq = save_and_disable_interrupts();
    if (at_line_start) {
        char ts[16];
        int tn = snprintf(ts, sizeof ts, "%7lu ", (unsigned long)to_ms_since_boot(get_absolute_time()));
        put(ts, tn);
    }
    put(tmp, n);
    at_line_start = tmp[n - 1] == '\n';
    restore_interrupts(irq);
    return n;
}
