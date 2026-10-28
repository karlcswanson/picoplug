/*
 * w5500_raw.h - W5500 bring-up and MACRAW receive on socket 0.
 *
 * Uses the WIZnet ioLibrary for register access (wizchip_conf.c, w5500.c)
 * with SPI/CS callbacks implemented here on the Pico SDK.
 */
#ifndef W5500_RAW_H
#define W5500_RAW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    bool     link;      /* PHY link up                       */
    uint16_t speed;     /* 10 or 100 (Mbit/s), 0 if no link  */
    bool     full;      /* full duplex                       */
} w5500_link_t;

/* Called for every received frame. `frame` starts at the destination MAC. */
typedef void (*w5500_frame_cb_t)(const uint8_t *frame, size_t len, void *ctx);

/*
 * Reset and initialise the chip, set the MAC, open socket 0 in MACRAW mode.
 * Returns false if the chip does not answer (VERSIONR != 4) or the socket
 * fails to open. Safe to call again to recover.
 */
bool w5500_init(const uint8_t mac[6]);

/* True once w5500_init() has succeeded. */
bool w5500_ready(void);

/*
 * Drain up to W5500_FRAMES_PER_POLL frames from the RX buffer, calling cb for
 * each. Also re-checks the socket state about once a second and reopens it if
 * the chip has been reset underneath us. Returns the number of frames read.
 */
int w5500_poll(w5500_frame_cb_t cb, void *ctx);

/*
 * Transmit one Ethernet frame (starting at the destination MAC) on socket 0.
 * Frames shorter than 60 bytes are zero padded. Blocks for at most ~20 ms
 * waiting for the chip. Returns false if not ready or the send failed.
 */
bool w5500_send(const uint8_t *frame, size_t len);

/* Read the PHY status register. */
void w5500_get_link(w5500_link_t *out);

/*
 * Power the PHY down (link drops, the switch port LED goes dark) or back
 * up. Powering up restores the operating mode the chip booted with
 * (normally auto-negotiation from the PMODE pins) and pulses the PHY
 * reset, so the link renegotiates. Used by the port blinker.
 */
void w5500_phy_power(bool on);

/* Counters for diagnostics. */
typedef struct {
    uint32_t frames;       /* frames delivered to the callback   */
    uint32_t oversize;     /* frames discarded as too large      */
    uint32_t resyncs;      /* socket reopened due to bad state   */
    uint32_t init_attempts;
    uint32_t polls;        /* w5500_poll() calls while ready          */
    uint32_t tx_frames;
    uint32_t tx_fail;
    uint32_t tx_nolink;    /* sends skipped because the PHY link was down */
    uint32_t cr_timeouts;  /* Sn_CR did not clear within 50 ms           */
    uint32_t bad_headers;  /* implausible frame length headers seen   */
    uint32_t lldp_frames;  /* frames with EtherType 0x88CC            */
    uint16_t last_rsr;     /* last non-zero Sn_RX_RSR                 */
    uint16_t last_hdr;     /* last frame length header read           */
    uint8_t  last_dst[6];  /* destination MAC of the last frame       */
    uint8_t  version;      /* last VERSIONR read (0x04 = W5500)  */
    uint8_t  probe_w5100s; /* W5100S VERSIONR probe (0x51 if a W5100S is fitted) */
    uint8_t  phycfgr;      /* last PHYCFGR read                  */
    uint8_t  sn_sr;        /* last socket 0 status (0x42 MACRAW) */
    uint32_t spi_hz;       /* SPI clock in use                   */
} w5500_stats_t;
const w5500_stats_t *w5500_stats(void);

#endif /* W5500_RAW_H */
