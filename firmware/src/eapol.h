/*
 * eapol.h - passive 802.1X detection.
 *
 * A switch port with 802.1X port authentication sends EAPOL frames
 * (EtherType 0x888E, to 01:80:c2:00:00:03) carrying EAP Request/Identity
 * as soon as the link comes up and whenever it hears a frame from an
 * unauthenticated client. We only listen; the tracker counts what the
 * port sends so the page can say "this port wants 802.1X" and explain a
 * silent DHCP. No EAPOL-Start is sent and no response is given.
 */
#ifndef EAPOL_H
#define EAPOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define EAPOL_ETHERTYPE 0x888E

enum { EAPOL_TYPE_EAP = 0, EAPOL_TYPE_START = 1, EAPOL_TYPE_LOGOFF = 2, EAPOL_TYPE_KEY = 3, EAPOL_TYPE_ALERT = 4 };
enum { EAP_CODE_REQUEST = 1, EAP_CODE_RESPONSE = 2, EAP_CODE_SUCCESS = 3, EAP_CODE_FAILURE = 4 };

typedef struct {
    uint8_t  src_mac[6];
    uint8_t  version;            /* EAPOL protocol version (1, 2 or 3)      */
    uint8_t  type;               /* EAPOL_TYPE_*                            */
    uint16_t body_len;
    /* type == EAPOL_TYPE_EAP only */
    uint8_t  eap_code;           /* EAP_CODE_*                              */
    uint8_t  eap_id;
    uint8_t  eap_type;           /* request/response only: 1 identity ...   */
    bool     has_eap_type;
} eapol_pkt_t;

/* EtherType 0x888E, plain or behind one 802.1Q tag. */
bool eapol_is_eapol(const uint8_t *frame, size_t len);
/* Returns false if the frame is not EAPOL or is truncated. */
bool eapol_parse(const uint8_t *frame, size_t len, eapol_pkt_t *out);

const char *eapol_type_name(uint8_t t);   /* "eap" "start" "logoff" "key" "alert" "unknown" */
const char *eap_code_name(uint8_t c);     /* "request" "response" "success" "failure" "unknown" */
const char *eap_type_name(uint8_t t);     /* "identity" "notification" "nak" "md5" "tls" "ttls" "peap" "mschapv2" ... */

/* What the port has sent since link-up. */
typedef struct {
    bool     seen;               /* any EAP packet from the authenticator  */
    uint32_t requests;           /* EAP Requests (the port asking us)      */
    uint32_t failures;           /* EAP Failure (the port gave up on us)   */
    uint32_t first_ms, last_ms;
    uint32_t interval_ms;        /* between the last two requests          */
    uint8_t  authenticator[6];   /* source MAC of the last request         */
    uint8_t  version;
    uint8_t  eap_type;           /* of the last request                    */
    uint32_t generation;         /* bumps on every counted packet          */
} eapol_state_t;

void eapol_state_reset(eapol_state_t *s);
void eapol_state_update(eapol_state_t *s, const eapol_pkt_t *p, uint32_t now_ms);

#endif /* EAPOL_H */
