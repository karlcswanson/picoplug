/*
 * settings_store.c - settings in the last 4 KB sector of the flash.
 *
 * The erase takes ~50 ms (program ~1 ms) with interrupts off, because every
 * interrupt handler lives in flash and XIP is unusable meanwhile. USB and
 * lwIP ride it out; the watchdog is fed first.
 */
#include "settings.h"

#include <stdio.h>
#include <string.h>

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"

#define SETTINGS_FLASH_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)

static settings_t cur;
static uint32_t generation;
static const uint8_t *const flash_ptr = (const uint8_t *)(XIP_BASE + SETTINGS_FLASH_OFFSET);

void settings_init(void)
{
    if (settings_decode(flash_ptr, SETTINGS_BLOB_MAX, &cur)) {
        printf("settings: loaded from flash (ping %s, dns %s%s%s)\n", cur.ping_target, cur.dns_name,
               cur.dns_server[0] ? " via " : "", cur.dns_server);
    } else {
        settings_defaults(&cur);
        printf("settings: none in flash, using defaults\n");
    }
    generation = 1;
}

const settings_t *settings_get(void) { return &cur; }
uint32_t settings_generation(void) { return generation; }

bool settings_save(const settings_t *s)
{
    static uint8_t page[2 * FLASH_PAGE_SIZE];    /* SETTINGS_BLOB_MAX */
    memset(page, 0xFF, sizeof page);
    size_t n = settings_encode(s, page, sizeof page);
    if (!n) return false;
    watchdog_update();
    uint32_t irq = save_and_disable_interrupts();
    flash_range_erase(SETTINGS_FLASH_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(SETTINGS_FLASH_OFFSET, page, sizeof page);
    restore_interrupts(irq);
    watchdog_update();
    bool ok = memcmp(flash_ptr, page, n) == 0;
    if (ok) { cur = *s; generation++; }
    printf("settings: save %s\n", ok ? "ok" : "FAILED (verify)");
    return ok;
}
