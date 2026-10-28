/*
 * eapol.c - see eapol.h.
 */
#include "eapol.h"

#include <string.h>

/* Offset of the EAPOL header, 0 if the frame is not EAPOL. */
static size_t eapol_offset(const uint8_t *f, size_t len)
{
    if (!f || len < 14) return 0;
    size_t o = 12;
    if (f[o] == 0x81 && f[o + 1] == 0x00) { o += 4; if (len < o + 2) return 0; }
    if (f[o] != (EAPOL_ETHERTYPE >> 8) || f[o + 1] != (EAPOL_ETHERTYPE & 0xFF)) return 0;
    return o + 2;
}

bool eapol_is_eapol(const uint8_t *frame, size_t len) { return eapol_offset(frame, len) != 0; }

bool eapol_parse(const uint8_t *frame, size_t len, eapol_pkt_t *out)
{
    if (!out) return false;
    memset(out, 0, sizeof *out);
    size_t o = eapol_offset(frame, len);
    if (!o || len < o + 4) return false;
    memcpy(out->src_mac, frame + 6, 6);
    out->version = frame[o];
    out->type = frame[o + 1];
    out->body_len = (uint16_t)(frame[o + 2] << 8 | frame[o + 3]);
    o += 4;
    if (out->type != EAPOL_TYPE_EAP) return true;          /* Start/Logoff/Key: header is enough */
    if (out->body_len < 4 || len < o + 4) return false;
    out->eap_code = frame[o];
    out->eap_id = frame[o + 1];
    uint16_t eap_len = (uint16_t)(frame[o + 2] << 8 | frame[o + 3]);
    if ((out->eap_code == EAP_CODE_REQUEST || out->eap_code == EAP_CODE_RESPONSE) && eap_len >= 5 && len >= o + 5) {
        out->eap_type = frame[o + 4];
        out->has_eap_type = true;
    }
    return true;
}

const char *eapol_type_name(uint8_t t)
{
    switch (t) {
    case EAPOL_TYPE_EAP: return "eap"; case EAPOL_TYPE_START: return "start"; case EAPOL_TYPE_LOGOFF: return "logoff";
    case EAPOL_TYPE_KEY: return "key"; case EAPOL_TYPE_ALERT: return "alert"; default: return "unknown";
    }
}

const char *eap_code_name(uint8_t c)
{
    switch (c) {
    case EAP_CODE_REQUEST: return "request"; case EAP_CODE_RESPONSE: return "response";
    case EAP_CODE_SUCCESS: return "success"; case EAP_CODE_FAILURE: return "failure"; default: return "unknown";
    }
}

const char *eap_type_name(uint8_t t)
{
    switch (t) {
    case 1: return "identity"; case 2: return "notification"; case 3: return "nak"; case 4: return "md5";
    case 5: return "otp"; case 6: return "gtc"; case 13: return "tls"; case 17: return "leap"; case 18: return "sim";
    case 21: return "ttls"; case 23: return "aka"; case 25: return "peap"; case 26: return "mschapv2"; case 43: return "fast";
    case 52: return "pwd"; case 55: return "teap"; default: return "unknown";
    }
}

void eapol_state_reset(eapol_state_t *s) { memset(s, 0, sizeof *s); }

void eapol_state_update(eapol_state_t *s, const eapol_pkt_t *p, uint32_t now_ms)
{
    if (p->type != EAPOL_TYPE_EAP) return;                 /* a client's Start/Logoff on a hub is not the port talking */
    if (p->eap_code != EAP_CODE_REQUEST && p->eap_code != EAP_CODE_FAILURE && p->eap_code != EAP_CODE_SUCCESS) return;
    if (!s->seen) { s->seen = true; s->first_ms = now_ms; }
    if (p->eap_code == EAP_CODE_REQUEST) {
        if (s->requests) s->interval_ms = now_ms - s->last_ms;
        s->requests++;
        s->last_ms = now_ms;
        memcpy(s->authenticator, p->src_mac, 6);
        s->version = p->version;
        if (p->has_eap_type) s->eap_type = p->eap_type;
    } else if (p->eap_code == EAP_CODE_FAILURE) {
        s->failures++;
    }
    s->generation++;
}
