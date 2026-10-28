/*
 * usb_stdio.c - route the Pico SDK stdio (printf) to the CDC-ACM interface.
 *
 * Non-blocking: bytes that do not fit in the CDC FIFO are dropped, so a
 * missing or slow terminal never stalls the main loop, and printf is safe
 * to call from TinyUSB callbacks (no re-entrant tud_task()).
 */
#include "tusb.h"
#if CFG_TUD_CDC
#include "pico/stdio/driver.h"
#include "pico/stdio.h"

static void cdc_out_chars(const char *buf, int len)
{
    /* tud_cdc_connected() requires DTR, which plain `cat` never asserts;
     * write whenever the host has configured the device instead. */
    if (!tud_mounted()) return;
    uint32_t avail = tud_cdc_write_available();
    if (avail == 0) return;
    if ((uint32_t)len > avail) len = (int)avail;
    tud_cdc_write(buf, (uint32_t)len);
    tud_cdc_write_flush();
}

static void cdc_out_flush(void) { if (tud_mounted()) tud_cdc_write_flush(); }

static stdio_driver_t cdc_driver = {
    .out_chars = cdc_out_chars,
    .out_flush = cdc_out_flush,
    .in_chars = NULL,
#if PICO_STDIO_ENABLE_CRLF_SUPPORT
    .crlf_enabled = true,
#endif
};

void usb_stdio_init(void)
{
    stdio_set_driver_enabled(&cdc_driver, true);
}
#else
void usb_stdio_init(void) {}
#endif
