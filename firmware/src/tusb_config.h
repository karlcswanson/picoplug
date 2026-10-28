/*
 * tusb_config.h - TinyUSB configuration: a single CDC-NCM (or CDC-ECM)
 * network interface, full speed, no RTOS.
 */
#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

#include "lwipopts.h"   /* TCP_MSS for the NTB sizing below */

#ifndef CFG_TUSB_MCU
#error "CFG_TUSB_MCU must be defined (the Pico SDK sets it)"
#endif
#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS             OPT_OS_NONE
#endif
#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG          0
#endif
#if USB_TRACE                   /* -DUSB_TRACE=ON: TinyUSB log into RAM, see usb_trace.c */
#undef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG          2
#endif

#define CFG_TUD_ENABLED         1
#define CFG_TUD_MAX_SPEED       OPT_MODE_FULL_SPEED
#define BOARD_TUD_RHPORT        0

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif
#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN      __attribute__((aligned(4)))
#endif

#define CFG_TUD_ENDPOINT0_SIZE  64

/* Only the network class is enabled. Everything else stays 0. */
/* USB_DEBUG_CDC (CMake option, default ON) adds a CDC-ACM serial port next
 * to the network interface carrying the firmware log (printf). */
#ifndef USB_DEBUG_CDC
#define USB_DEBUG_CDC           1
#endif
#define CFG_TUD_CDC             USB_DEBUG_CDC
#define CFG_TUD_CDC_RX_BUFSIZE  256
#define CFG_TUD_CDC_TX_BUFSIZE  2048
#define CFG_TUD_MSC             0
#define CFG_TUD_HID             0
#define CFG_TUD_MIDI            0
#define CFG_TUD_VENDOR          0

/* USB_NET_USE_ECM is set from CMake (-DUSB_NET_ECM=ON). NCM is the default;
 * ECM is kept as an escape hatch in case NCM misbehaves on a given host. */
#ifndef USB_NET_USE_ECM
#define USB_NET_USE_ECM         0
#endif
#define CFG_TUD_ECM_RNDIS       USB_NET_USE_ECM
#define CFG_TUD_NCM             (1 - USB_NET_USE_ECM)

/* NCM transfer block sizing (same as the TinyUSB net_lwip_webserver example) */
#define CFG_TUD_NCM_IN_NTB_MAX_SIZE     (2 * TCP_MSS + 100)
#define CFG_TUD_NCM_OUT_NTB_MAX_SIZE    (2 * TCP_MSS + 100)
#define CFG_TUD_NCM_IN_NTB_N            1
#define CFG_TUD_NCM_OUT_NTB_N           1

#endif /* TUSB_CONFIG_H */
