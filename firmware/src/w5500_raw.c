/*
 * w5500_raw.c - W5500 init and MACRAW receive. See w5500_raw.h.
 */
#include "w5500_raw.h"
#include "board_config.h"

#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#if W5500_USE_PIO_SPI
#include "pio_spi.h"
#else
#include "hardware/spi.h"
#endif

#include "wizchip_conf.h"   /* ioLibrary: pulls in W5500/w5500.h */

#include <stdio.h>
#include <string.h>

#define SN 0                /* socket used for MACRAW */

static bool ready;
static uint8_t our_mac[6];
static w5500_stats_t stats;
static absolute_time_t next_health_check;
static uint8_t frame_buf[W5500_MAX_FRAME];

/* ---- bus backend: hardware SPI or PIO SPI ----------------------------- */

#if W5500_USE_PIO_SPI
static pio_spi_t bus;
static bool bus_init(uint32_t hz)
{
    return pio_spi_init(&bus, W5500_PIN_SCK, W5500_PIN_MOSI, W5500_PIN_MISO, hz);
}
static uint32_t bus_set_hz(uint32_t hz) { return pio_spi_set_hz(&bus, hz); }
static void bus_write(const uint8_t *buf, size_t n) { pio_spi_xfer(&bus, buf, NULL, n); }
static void bus_read(uint8_t *buf, size_t n) { pio_spi_xfer(&bus, NULL, buf, n); }
#else
static bool bus_init(uint32_t hz)
{
    spi_init(W5500_SPI, hz);
    spi_set_format(W5500_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(W5500_PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(W5500_PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(W5500_PIN_MOSI, GPIO_FUNC_SPI);
    return true;
}
static uint32_t bus_set_hz(uint32_t hz) { return spi_set_baudrate(W5500_SPI, hz); }
static void bus_write(const uint8_t *buf, size_t n) { spi_write_blocking(W5500_SPI, buf, n); }
static void bus_read(uint8_t *buf, size_t n) { spi_read_blocking(W5500_SPI, 0x00, buf, n); }
#endif

/* ---- ioLibrary callbacks --------------------------------------------- */

static void cs_select(void)   { gpio_put(W5500_PIN_CS, 0); }
static void cs_deselect(void) { gpio_put(W5500_PIN_CS, 1); }

static uint8_t spi_read_byte(void) { uint8_t b; bus_read(&b, 1); return b; }
static void spi_write_byte(uint8_t b) { bus_write(&b, 1); }
static void spi_read_burst(uint8_t *buf, uint16_t len) { bus_read(buf, len); }
static void spi_write_burst(uint8_t *buf, uint16_t len) { bus_write(buf, len); }

/* ---- helpers --------------------------------------------------------- */

static void hw_reset(void)
{
    gpio_put(W5500_PIN_RST, 0);
    sleep_ms(10);           /* datasheet: >= 500 us low; WIZnet examples use far more */
    gpio_put(W5500_PIN_RST, 1);
    sleep_ms(50);           /* PLL lock is ~1 ms; be generous */
}

/* Read the W5100S version register (0x0080) with the W5100S 3-byte SPI
 * frame. A W5100S-EVB-Pico has the same pinout as the W5500 board; this
 * tells the two apart from the diagnostics alone. Returns 0x51 on W5100S. */
static uint8_t probe_w5100s(void)
{
    uint8_t hdr[3] = {0x00, 0x80, 0x00};   /* addr hi, addr lo, control (RWB=0 read) */
    uint8_t v;
    cs_select();
    bus_write(hdr, 3);
    bus_read(&v, 1);
    cs_deselect();
    return v;
}

/* Sn_CR clears when the chip has taken the command; normally within microseconds.
 * Bounded so a confused chip (or a PHY mid-reset) can never hold the main loop. */
static void wait_cr(void)
{
    absolute_time_t deadline = make_timeout_time_ms(50);
    while (getSn_CR(SN)) {
        if (time_reached(deadline)) { stats.cr_timeouts++; return; }
    }
}

static bool open_macraw(void)
{
    setSn_CR(SN, Sn_CR_CLOSE);
    wait_cr();
    setSn_IR(SN, 0xFF);

    uint8_t mr = Sn_MR_MACRAW;
#if W5500_MAC_FILTER
    mr |= Sn_MR_MFEN;
#endif
    setSn_MR(SN, mr);
    setSn_CR(SN, Sn_CR_OPEN);
    wait_cr();

    for (int i = 0; i < 100; i++) {
        if (getSn_SR(SN) == SOCK_MACRAW) return true;
        sleep_ms(1);
    }
    return false;
}

/* ---- public API ------------------------------------------------------ */

bool w5500_init(const uint8_t mac[6])
{
    static bool gpio_done;
    ready = false;
    memcpy(our_mac, mac, 6);

    if (!gpio_done) {
        gpio_done = true;
        gpio_init(W5500_PIN_CS);
        gpio_set_dir(W5500_PIN_CS, GPIO_OUT);
        gpio_put(W5500_PIN_CS, 1);
        gpio_init(W5500_PIN_RST);
        gpio_pull_up(W5500_PIN_RST);       /* WIZnet does this for the in-package W5500 */
        gpio_put(W5500_PIN_RST, 1);
        gpio_set_dir(W5500_PIN_RST, GPIO_OUT);
        gpio_init(W5500_PIN_INT);
        gpio_set_dir(W5500_PIN_INT, GPIO_IN);
        gpio_pull_up(W5500_PIN_INT);

        if (!bus_init(W5500_SPI_HZ)) {
            printf("w5500: SPI bus init failed\n");
            return false;
        }

        reg_wizchip_cs_cbfunc(cs_select, cs_deselect);
        reg_wizchip_spi_cbfunc(spi_read_byte, spi_write_byte);
        reg_wizchip_spiburst_cbfunc(spi_read_burst, spi_write_burst);
    }

    stats.init_attempts++;
    hw_reset();

    /* Probe at the configured SPI clock; fall back to slower clocks if the
     * chip does not answer (long wires, marginal SPI timing). */
    static const uint32_t rates[] = {W5500_SPI_HZ, 10 * 1000 * 1000, 2 * 1000 * 1000};
    uint8_t ver = 0;
    for (size_t r = 0; r < sizeof rates / sizeof rates[0]; r++) {
        stats.spi_hz = bus_set_hz(rates[r]);
        ver = getVERSIONR();
        if (ver == 0x04) break;
        printf("w5500: VERSIONR=0x%02x at %lu Hz\n", ver, (unsigned long)stats.spi_hz);
    }
    stats.version = ver;
    if (ver != 0x04) {
        stats.probe_w5100s = probe_w5100s();
        printf("w5500: no chip found (expected VERSIONR 0x04; SPI wiring / reset pin?), w5100s probe 0x%02x\n",
               stats.probe_w5100s);
        return false;
    }

    /* Socket 0 gets the whole 16 KB RX buffer and 4 KB TX (for a future
     * LLDP transmitter). Other sockets get nothing; we don't use them. */
    uint8_t txsize[8] = {4, 0, 0, 0, 0, 0, 0, 0};
    uint8_t rxsize[8] = {16, 0, 0, 0, 0, 0, 0, 0};
    if (wizchip_init(txsize, rxsize) != 0) {
        printf("w5500: wizchip_init failed\n");
        return false;
    }

    setSHAR((uint8_t *)our_mac);

    if (!open_macraw()) {
        printf("w5500: socket 0 did not enter MACRAW (SR=0x%02x)\n", getSn_SR(SN));
        return false;
    }

    ready = true;
    next_health_check = make_timeout_time_ms(1000);
    printf("w5500: ready, mac %02x:%02x:%02x:%02x:%02x:%02x, macraw%s, spi %lu Hz\n",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
           W5500_MAC_FILTER ? " (MAC filter ON)" : " (promiscuous)", (unsigned long)stats.spi_hz);
    return true;
}

bool w5500_ready(void) { return ready; }

static void resync(const char *why)
{
    stats.resyncs++;
    printf("w5500: resync (%s)\n", why);
    if (!open_macraw()) {
        /* Chip may have been reset (brown-out on PoE hot-plug). Full init. */
        w5500_init(our_mac);
    }
}

int w5500_poll(w5500_frame_cb_t cb, void *ctx)
{
    static bool bad_since_valid;
    static absolute_time_t bad_since;
    if (!ready) return 0;
    stats.polls++;

    if (time_reached(next_health_check)) {
        next_health_check = make_timeout_time_ms(1000);
        stats.sn_sr = getSn_SR(SN);
        stats.version = getVERSIONR();
        if (stats.sn_sr != SOCK_MACRAW) {
            resync("socket not in MACRAW");
            return 0;
        }
    }

    int n = 0;
    while (n < W5500_FRAMES_PER_POLL) {
        uint16_t rsr = getSn_RX_RSR(SN);
        if (rsr < 2) break;
        stats.last_rsr = rsr;

        /* Sn_RX_RD reads back the *committed* pointer until a RECV command
         * is processed, so never re-read it between the header and the
         * payload: read it once, address everything from it, write it back
         * once, then commit with RECV. (The first version of this loop did
         * re-read it and every frame came back shifted by the 2-byte header.) */
        uint16_t ptr = getSn_RX_RD(SN);
        uint32_t block = (uint32_t)(WIZCHIP_RXBUF_BLOCK(SN) << 3);
        uint8_t head[2];
        WIZCHIP_READ_BUF(((uint32_t)ptr << 8) + block, head, 2);
        uint16_t total = (uint16_t)(((unsigned)head[0] << 8) | head[1]);   /* includes itself */
        stats.last_hdr = total;

        if (total < 2 + 14 || total - 2 > W5500_MAX_FRAME + 4 || total > rsr) {
            stats.bad_headers++;
            /* RSR can lag a RECV briefly; only give up if it stays wrong. */
            if (!bad_since_valid) { bad_since_valid = true; bad_since = get_absolute_time(); }
            else if (absolute_time_diff_us(bad_since, get_absolute_time()) > 200 * 1000) {
                bad_since_valid = false;
                resync("bad frame length");
            }
            return n;
        }
        bad_since_valid = false;
        uint16_t flen = (uint16_t)(total - 2);

        if (flen <= sizeof frame_buf) {
            WIZCHIP_READ_BUF(((uint32_t)(uint16_t)(ptr + 2) << 8) + block, frame_buf, flen);
        } else {
            stats.oversize++;
        }
        setSn_RX_RD(SN, (uint16_t)(ptr + total));
        setSn_CR(SN, Sn_CR_RECV);
        wait_cr();
        if (flen > sizeof frame_buf) continue;

        stats.frames++;
        memcpy(stats.last_dst, frame_buf, 6);
        if (flen >= 14 && frame_buf[12] == 0x88 && frame_buf[13] == 0xCC) stats.lldp_frames++;
        n++;
        if (cb) cb(frame_buf, flen, ctx);
    }
    return n;
}

bool w5500_send(const uint8_t *frame, size_t len)
{
    static uint8_t txbuf[1536];
    if (!ready || len < 14 || len > sizeof txbuf) return false;
    size_t n = len;
    if (n < 60) {                      /* minimum Ethernet frame without FCS */
        memcpy(txbuf, frame, len);
        memset(txbuf + len, 0, 60 - len);
        frame = txbuf;
        n = 60;
    }

    if (!(getPHYCFGR() & PHYCFGR_LNK_ON)) { stats.tx_nolink++; return false; }   /* nothing to send into; the chip would hold the frame */
    absolute_time_t deadline = make_timeout_time_ms(20);
    while (getSn_TX_FSR(SN) < n) {
        if (time_reached(deadline)) { stats.tx_fail++; return false; }
    }
    wiz_send_data(SN, (uint8_t *)frame, (uint16_t)n);
    setSn_IR(SN, (Sn_IR_SENDOK | Sn_IR_TIMEOUT));
    setSn_CR(SN, Sn_CR_SEND);
    wait_cr();
    while (!(getSn_IR(SN) & Sn_IR_SENDOK)) {
        if (time_reached(deadline)) { stats.tx_fail++; return false; }
    }
    setSn_IR(SN, Sn_IR_SENDOK);
    stats.tx_frames++;
    return true;
}

void w5500_get_link(w5500_link_t *out)
{
    memset(out, 0, sizeof *out);
    if (!ready) return;
    uint8_t phy = getPHYCFGR();
    stats.phycfgr = phy;
    out->link = (phy & PHYCFGR_LNK_ON) != 0;
    out->speed = out->link ? ((phy & PHYCFGR_SPD_100) ? 100 : 10) : 0;
    out->full = (phy & PHYCFGR_DPX_FULL) != 0;
}

static uint8_t phy_boot_cfg;      /* PHYCFGR mode bits before the first power-down, 0 = not saved */
static bool phy_boot_saved;

void w5500_phy_power(bool on)
{
    if (!ready) return;
    uint8_t cfg = getPHYCFGR();
    if (!phy_boot_saved) { phy_boot_cfg = cfg & (PHYCFGR_OPMD | PHYCFGR_OPMDC_ALLA); phy_boot_saved = true; }
    uint8_t mode = on ? phy_boot_cfg : (uint8_t)(PHYCFGR_OPMD | PHYCFGR_OPMDC_PDOWN);
    /* bit 7 is the active-low PHY reset: write the mode with reset asserted, then release it */
    setPHYCFGR((uint8_t)(mode & (uint8_t)PHYCFGR_RST));
    sleep_us(100);
    setPHYCFGR((uint8_t)(mode | 0x80));
    stats.phycfgr = getPHYCFGR();
}

const w5500_stats_t *w5500_stats(void) { return &stats; }
