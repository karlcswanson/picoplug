/*
 * board_config.h - every pin, address, and tunable in one place.
 *
 * Pins default to the W5500-EVB-Pico / -Pico2 layout from the board header
 * (firmware/boards/). Override any of them with -D on the CMake command line.
 */
#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include "pico.h"

#define FW_VERSION              "0.3.2"

/* ---- W5500 SPI wiring ------------------------------------------------- */
#if defined(WIZNET_W55RP20_EVB_PICO)
/* W55RP20: W5500 is inside the package on GPIO20-25; PIO SPI required. */
#define W5500_USE_PIO_SPI       1
#define W5500_PIN_CS            W55RP20_W5500_CS_PIN
#define W5500_PIN_SCK           W55RP20_W5500_SCK_PIN
#define W5500_PIN_MISO          W55RP20_W5500_MISO_PIN
#define W5500_PIN_MOSI          W55RP20_W5500_MOSI_PIN
#define W5500_PIN_INT           W55RP20_W5500_INTN_PIN
#define W5500_PIN_RST           W55RP20_W5500_RSTN_PIN
#ifndef W5500_SPI_HZ
#define W5500_SPI_HZ            (15 * 1000 * 1000)   /* WIZnet's port uses ~31 MHz */
#endif
#else
/* W5500-EVB-Pico / -Pico2: hardware SPI0 */
#define W5500_USE_PIO_SPI       0
#ifndef W5500_SPI
#define W5500_SPI               spi0
#endif
#ifndef W5500_PIN_MISO
#define W5500_PIN_MISO          16
#endif
#ifndef W5500_PIN_CS
#define W5500_PIN_CS            17
#endif
#ifndef W5500_PIN_SCK
#define W5500_PIN_SCK           18
#endif
#ifndef W5500_PIN_MOSI
#define W5500_PIN_MOSI          19
#endif
#ifndef W5500_PIN_RST
#define W5500_PIN_RST           20
#endif
#ifndef W5500_PIN_INT
#define W5500_PIN_INT           21
#endif
/* W5500 SPI is rated to 80 MHz; the RP2040 SPI peripheral tops out at
 * clk_peri/2. 30 MHz is comfortable on the EVB's short traces. */
#ifndef W5500_SPI_HZ
#define W5500_SPI_HZ            (30 * 1000 * 1000)
#endif
#endif
/* 1 = enable the W5500 MAC filter (own MAC + broadcast only; drops the
 * LLDP multicast). Must stay 0 for LLDP to be received. Kept as an option
 * in case a busy port floods the RX buffer and you want to experiment. */
#ifndef W5500_MAC_FILTER
#define W5500_MAC_FILTER        0
#endif
/* Largest Ethernet frame we will copy out of the W5500. Larger frames are
 * discarded (LLDP frames are never this big). */
#define W5500_MAX_FRAME         1536
/* Frames pulled from the W5500 per poll() call, to keep USB serviced. */
#define W5500_FRAMES_PER_POLL   8

/* ---- Ethernet-side identity ------------------------------------------ */
/* MAC used for the W5500 (source of any frames we transmit). Locally
 * administered; the low 3 bytes are replaced by the RP2 unique ID. */
#define ETH_MAC_PREFIX          { 0x02, 0x50, 0x4C }

/* ---- USB network ----------------------------------------------------- */
#define USB_NET_IP              192, 168, 7, 1
#define USB_NET_MASK            255, 255, 255, 0
#define USB_NET_CLIENT_IP       192, 168, 7, 2
#define USB_NET_DHCP_LEASE_S    (24 * 60 * 60)
#define USB_NET_HOSTNAME        "picoplug"
/* Link watchdog: if the host enables the NCM data interface and drops it again
 * within DROP_WINDOW ms (macOS and iOS do this on roughly 1 in 3 plug-ins and
 * then never recover), detach from the bus after SETTLE ms, re-attach after
 * OFF ms, up to MAX times per plug-in. A link that stays up for GOOD_S resets
 * the budget so the same board can be moved between hosts. */
#define USB_NET_REENUM_DROP_WINDOW_MS   1000
#define USB_NET_REENUM_SETTLE_MS        400
#define USB_NET_REENUM_OFF_MS           700
#define USB_NET_REENUM_MAX              4
#define USB_NET_REENUM_GOOD_S           5
/* MAC prefixes for the USB link; low 3 bytes come from the unique ID.
 * The host-side MAC is what the phone/PC sees as its own adapter. */
#define USB_MAC_HOST_PREFIX     { 0x02, 0x55, 0x48 }
#define USB_MAC_DEV_PREFIX      { 0x02, 0x55, 0x44 }

#define API_TCP_PORT            9000
#define API_MAX_CLIENTS         4
#define API_HEARTBEAT_MS        2000
#define HTTP_TCP_PORT           80
#define HTTP_MAX_CLIENTS        4

/* ---- Our own LLDP / CDP advertisements (advertise.c) ------------------ */
/* Boot-time advertise defaults live in settings.h (SETTINGS_DEFAULT_*): they are user settings now. */
#define ADV_PORT_ID             "eth0"
#define ADV_PORT_DESC           "RJ45 tester port"
#define ADV_SYS_DESC            "picoplug " FW_VERSION " on " PICO_BOARD
#define ADV_PLATFORM            "picoplug"
/* LLDP-MED device class we claim: 1 generic endpoint, 2 media endpoint,
 * 3 communication device (phone). Class III gets the full voice VLAN
 * policy from most switches, which is what a tester wants to see. */
#define ADV_MED_DEVICE_TYPE     3
#define ADV_LLDP_INTERVAL_S     30      /* 802.1AB msgTxInterval default */
#define ADV_LLDP_TTL_S          120     /* 4 x interval                   */
#define ADV_CDP_INTERVAL_S      60      /* Cisco default                  */
#define ADV_CDP_TTL_S           180
#define ADV_FAST_START_COUNT    4       /* LLDP-MED fast start: frames 1 s apart on link-up / re-enable */
#define ADV_FAST_START_MS       1000

/* ---- Neighbor table --------------------------------------------------- */
#define NEIGHBOR_MAX            4

/* ---- Misc ------------------------------------------------------------- */
#define WATCHDOG_TIMEOUT_MS     4000
#ifndef STATUS_LED_PIN
#define STATUS_LED_PIN          PICO_DEFAULT_LED_PIN
#endif

#endif /* BOARD_CONFIG_H */
