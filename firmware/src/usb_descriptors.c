/*
 * usb_descriptors.c - device, configuration, and string descriptors for the
 * CDC-NCM (default) or CDC-ECM (USB_NET_USE_ECM) network gadget.
 * Adapted from TinyUSB's net_lwip_webserver example (MIT, Peter Lawrence).
 */
#include "tusb.h"
#include "pico/unique_id.h"
#include "board_config.h"

#include <string.h>

extern uint8_t tud_network_mac_address[6];

enum {
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
    STRID_INTERFACE,
    STRID_MAC,
    STRID_DEBUG,
};

enum {
    ITF_NUM_CDC = 0,          /* network control + data (the name is TinyUSB's) */
    ITF_NUM_CDC_DATA,
#if CFG_TUD_CDC
    ITF_NUM_DBG,              /* CDC-ACM debug serial */
    ITF_NUM_DBG_DATA,
#endif
    ITF_NUM_TOTAL,
};

#define USB_VID   0xCafe            /* TinyUSB test VID; not for resale */
#define USB_PID   ((USB_NET_USE_ECM ? 0x4021 : 0x4020) + (CFG_TUD_CDC ? 0x10 : 0))

static const tusb_desc_device_t desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
#if CFG_TUD_NCM
    .bcdUSB             = 0x0201,   /* BOS descriptor present (Windows NCM binding) */
#else
    .bcdUSB             = 0x0200,
#endif
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = 0x0100,
    .iManufacturer      = STRID_MANUFACTURER,
    .iProduct           = STRID_PRODUCT,
    .iSerialNumber      = STRID_SERIAL,
    .bNumConfigurations = 1,
};

uint8_t const *tud_descriptor_device_cb(void)
{
    return (uint8_t const *)&desc_device;
}

#define EPNUM_NET_NOTIF   0x81
#define EPNUM_NET_OUT     0x02
#define EPNUM_NET_IN      0x82
#define EPNUM_DBG_NOTIF   0x83
#define EPNUM_DBG_OUT     0x04
#define EPNUM_DBG_IN      0x84

#if CFG_TUD_CDC
#define DBG_DESC_LEN      TUD_CDC_DESC_LEN
#define DBG_DESC          , TUD_CDC_DESCRIPTOR(ITF_NUM_DBG, STRID_DEBUG, EPNUM_DBG_NOTIF, 8, EPNUM_DBG_OUT, EPNUM_DBG_IN, 64)
#else
#define DBG_DESC_LEN      0
#define DBG_DESC
#endif

#if CFG_TUD_ECM_RNDIS
#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_CDC_ECM_DESC_LEN + DBG_DESC_LEN)
static const uint8_t desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0, 100),
    TUD_CDC_ECM_DESCRIPTOR(ITF_NUM_CDC, STRID_INTERFACE, STRID_MAC, EPNUM_NET_NOTIF, 64,
                           EPNUM_NET_OUT, EPNUM_NET_IN, CFG_TUD_NET_ENDPOINT_SIZE, CFG_TUD_NET_MTU)
    DBG_DESC
};
#else
#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_CDC_NCM_DESC_LEN + DBG_DESC_LEN)
static const uint8_t desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0, 100),
    TUD_CDC_NCM_DESCRIPTOR(ITF_NUM_CDC, STRID_INTERFACE, STRID_MAC, EPNUM_NET_NOTIF, 64,
                           EPNUM_NET_OUT, EPNUM_NET_IN, CFG_TUD_NET_ENDPOINT_SIZE, CFG_TUD_NET_MTU)
    DBG_DESC
};
#endif

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return desc_configuration;
}

#if CFG_TUD_NCM
/* BOS + MS OS 2.0 descriptor so Windows binds the inbox NCM driver. Not
 * needed by macOS/iOS/Linux but harmless. */
#define BOS_TOTAL_LEN      (TUD_BOS_DESC_LEN + TUD_BOS_MICROSOFT_OS_DESC_LEN)
#define MS_OS_20_DESC_LEN  0xB2

static const uint8_t desc_bos[] = {
    TUD_BOS_DESCRIPTOR(BOS_TOTAL_LEN, 1),
    TUD_BOS_MS_OS_20_DESCRIPTOR(MS_OS_20_DESC_LEN, 1),
};

uint8_t const *tud_descriptor_bos_cb(void) { return desc_bos; }

static const uint8_t desc_ms_os_20[] = {
    U16_TO_U8S_LE(0x000A), U16_TO_U8S_LE(MS_OS_20_SET_HEADER_DESCRIPTOR), U32_TO_U8S_LE(0x06030000), U16_TO_U8S_LE(MS_OS_20_DESC_LEN),
    U16_TO_U8S_LE(0x0008), U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_CONFIGURATION), 0, 0, U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A),
    U16_TO_U8S_LE(0x0008), U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_FUNCTION), ITF_NUM_CDC, 0, U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A - 0x08),
    U16_TO_U8S_LE(0x0014), U16_TO_U8S_LE(MS_OS_20_FEATURE_COMPATBLE_ID), 'W', 'I', 'N', 'N', 'C', 'M', 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A - 0x08 - 0x08 - 0x14), U16_TO_U8S_LE(MS_OS_20_FEATURE_REG_PROPERTY),
    U16_TO_U8S_LE(0x0007), U16_TO_U8S_LE(0x002A),
    'D', 0x00, 'e', 0x00, 'v', 0x00, 'i', 0x00, 'c', 0x00, 'e', 0x00, 'I', 0x00, 'n', 0x00, 't', 0x00, 'e', 0x00,
    'r', 0x00, 'f', 0x00, 'a', 0x00, 'c', 0x00, 'e', 0x00, 'G', 0x00, 'U', 0x00, 'I', 0x00, 'D', 0x00, 's', 0x00, 0x00, 0x00,
    U16_TO_U8S_LE(0x0050),
    '{', 0x00, '9', 0x00, '3', 0x00, '3', 0x00, 'C', 0x00, '7', 0x00, 'B', 0x00, '2', 0x00, '1', 0x00, '-', 0x00,
    '0', 0x00, 'D', 0x00, '0', 0x00, '8', 0x00, '-', 0x00, '4', 0x00, '3', 0x00, 'F', 0x00, 'D', 0x00, '-', 0x00,
    '8', 0x00, 'B', 0x00, '3', 0x00, 'E', 0x00, '-', 0x00, '1', 0x00, '2', 0x00, '7', 0x00, 'C', 0x00, 'A', 0x00,
    '8', 0x00, 'A', 0x00, 'F', 0x00, 'F', 0x00, 'F', 0x00, '9', 0x00, 'D', 0x00, '}', 0x00, 0x00, 0x00, 0x00, 0x00,
};
TU_VERIFY_STATIC(sizeof(desc_ms_os_20) == MS_OS_20_DESC_LEN, "Incorrect size");

bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request)
{
    if (stage != CONTROL_STAGE_SETUP) return true;
    if (request->bmRequestType_bit.type == TUSB_REQ_TYPE_VENDOR && request->bRequest == 1 &&
        request->wIndex == 7) {
        uint16_t total_len;
        memcpy(&total_len, desc_ms_os_20 + 8, 2);
        return tud_control_xfer(rhport, request, (void *)(uintptr_t)desc_ms_os_20, total_len);
    }
    return false;
}
#endif

static const char *const string_desc_arr[] = {
    [STRID_LANGID]       = (const char[]){0x09, 0x04},
    [STRID_MANUFACTURER] = "picoplug",
    [STRID_PRODUCT]      = "picoplug port tester",
    [STRID_SERIAL]       = NULL,
    [STRID_INTERFACE]    = "picoplug USB Ethernet",
    [STRID_MAC]          = NULL,                          /* handled below */
    [STRID_DEBUG]        = "picoplug debug console",
};

static uint16_t _desc_str[32 + 1];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;
    unsigned chr_count = 0;
    static const char hex[] = "0123456789ABCDEF";

    switch (index) {
    case STRID_LANGID:
        memcpy(&_desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
        break;
    case STRID_SERIAL: {
        char s[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
        pico_get_unique_board_id_string(s, sizeof s);
        chr_count = (unsigned)strlen(s);
        for (unsigned i = 0; i < chr_count; i++) _desc_str[1 + i] = (uint16_t)s[i];
        break;
    }
    case STRID_MAC:
        for (unsigned i = 0; i < 6; i++) {
            _desc_str[1 + chr_count++] = (uint16_t)hex[(tud_network_mac_address[i] >> 4) & 0xF];
            _desc_str[1 + chr_count++] = (uint16_t)hex[tud_network_mac_address[i] & 0xF];
        }
        break;
    default: {
        if (index >= sizeof string_desc_arr / sizeof string_desc_arr[0]) return NULL;
        const char *str = string_desc_arr[index];
        if (!str) return NULL;
        chr_count = (unsigned)strlen(str);
        if (chr_count > 31) chr_count = 31;
        for (unsigned i = 0; i < chr_count; i++) _desc_str[1 + i] = (uint16_t)str[i];
        break;
    }
    }
    _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return _desc_str;
}
