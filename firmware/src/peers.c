/*
 * peers.c - see peers.h.
 */
#include "peers.h"

#include <string.h>
#include <strings.h>

void peers_reset(peers_table_t *t)
{
    uint32_t g = t->generation;
    memset(t, 0, sizeof *t);
    t->generation = g + 1;
}

int peers_count(const peers_table_t *t)
{
    int n = 0;
    for (int i = 0; i < PEERS_MAX; i++) if (t->p[i].used) n++;
    return n;
}

const peers_entry_t *peers_find(const peers_table_t *t, const char *name)
{
    for (int i = 0; i < PEERS_MAX; i++)
        if (t->p[i].used && strcasecmp(t->p[i].name, name) == 0) return &t->p[i];
    return NULL;
}

bool peers_label(const uint8_t *dom, size_t len, int i, char *out, size_t cap)
{
    size_t o = 0;
    for (int k = 0; o < len; k++) {
        size_t l = dom[o];
        if (l == 0 || l > 63 || o + 1 + l > len) return false;
        if (k == i) {
            if (l >= cap) return false;
            memcpy(out, dom + o + 1, l);
            out[l] = '\0';
            return true;
        }
        o += 1 + l;
    }
    return false;
}

static int label_count(const uint8_t *dom, size_t len)
{
    size_t o = 0;
    int n = 0;
    while (o < len && dom[o]) {
        if (dom[o] > 63) return -1;
        o += 1u + dom[o];
        n++;
    }
    return o < len ? n : -1;
}

static bool label_is(const uint8_t *dom, size_t len, int i, const char *want)
{
    char b[64];
    return peers_label(dom, len, i, b, sizeof b) && strcasecmp(b, want) == 0;
}

/* <instance>._picoplug._tcp.local -> instance */
static bool instance_of(const uint8_t *dom, size_t len, char *out, size_t cap)
{
    return label_count(dom, len) == 4 && label_is(dom, len, 1, PEERS_SERVICE) && label_is(dom, len, 2, "_tcp")
        && label_is(dom, len, 3, "local") && peers_label(dom, len, 0, out, cap) && out[0];
}

static peers_entry_t *find(peers_table_t *t, const char *name)
{
    return (peers_entry_t *)peers_find(t, name);
}

static peers_entry_t *get(peers_table_t *t, const char *name)
{
    peers_entry_t *e = find(t, name);
    if (e) return e;
    for (int i = 0; i < PEERS_MAX; i++) {
        if (t->p[i].used) continue;
        e = &t->p[i];
        memset(e, 0, sizeof *e);
        e->used = true;
        e->probe = PEERS_PROBE_NONE;
        memcpy(e->name, name, strnlen(name, PEERS_NAME_MAX));
        return e;
    }
    return NULL;
}

static void copy_val(char *dst, size_t cap, const uint8_t *v, size_t vl)
{
    if (vl >= cap) vl = cap - 1;
    memcpy(dst, v, vl);
    dst[vl] = '\0';
}

/* TXT: a run of <len><key=value> strings; each record carries the whole set */
static void read_txt(peers_entry_t *e, const uint8_t *rd, size_t rdlen)
{
    e->probe = PEERS_PROBE_NONE;
    e->chassis[0] = e->sys_name[0] = e->port[0] = e->fw[0] = '\0';
    e->has_pvid = false; e->pvid = 0;
    size_t o = 0;
    while (o < rdlen) {
        size_t l = rd[o++];
        if (o + l > rdlen) break;
        const uint8_t *s = rd + o, *eq = memchr(s, '=', l);
        o += l;
        if (!eq) continue;
        size_t kl = (size_t)(eq - s), vl = l - kl - 1;
        const uint8_t *v = eq + 1;
        if (kl == 5 && !memcmp(s, "probe", 5)) { if (vl == 1 && v[0] >= '0' && v[0] <= '9') e->probe = (uint8_t)(v[0] - '0'); }
        else if (kl == 2 && !memcmp(s, "ch", 2)) copy_val(e->chassis, sizeof e->chassis, v, vl);
        else if (kl == 2 && !memcmp(s, "sw", 2)) copy_val(e->sys_name, sizeof e->sys_name, v, vl);
        else if (kl == 4 && !memcmp(s, "port", 4)) copy_val(e->port, sizeof e->port, v, vl);
        else if (kl == 2 && !memcmp(s, "fw", 2)) copy_val(e->fw, sizeof e->fw, v, vl);
        else if (kl == 4 && !memcmp(s, "pvid", 4) && vl && vl <= 4) {
            unsigned n = 0; bool ok = true;
            for (size_t i = 0; i < vl; i++) { if (v[i] < '0' || v[i] > '9') ok = false; else n = n * 10 + (unsigned)(v[i] - '0'); }
            if (ok && n && n < 4095) { e->has_pvid = true; e->pvid = (uint16_t)n; }
        }
    }
}

static void drop(peers_table_t *t, peers_entry_t *e)
{
    memset(e, 0, sizeof *e);
    t->generation++;
}

bool peers_record(peers_table_t *t, const char *self, uint16_t type, const uint8_t *dom, size_t domlen,
                  const uint8_t *rd, size_t rdlen, uint32_t ttl, uint32_t now_ms)
{
    char name[PEERS_NAME_MAX + 1];
    peers_entry_t *e = NULL;
    switch (type) {
    case PEERS_RR_PTR:          /* _picoplug._tcp.local -> <instance>._picoplug._tcp.local */
        if (label_count(dom, domlen) != 3 || !label_is(dom, domlen, 0, PEERS_SERVICE)) return false;
        if (!instance_of(rd, rdlen, name, sizeof name)) return false;
        break;
    case PEERS_RR_SRV:
    case PEERS_RR_TXT:
        if (!instance_of(dom, domlen, name, sizeof name)) return false;
        break;
    case PEERS_RR_A:
    case PEERS_RR_AAAA:         /* <host>.local; our host name is the instance name */
        if (label_count(dom, domlen) != 2 || !label_is(dom, domlen, 1, "local") || !peers_label(dom, domlen, 0, name, sizeof name))
            return false;
        if (rdlen != (type == PEERS_RR_A ? 4u : 16u)) return false;
        break;
    default:
        return false;
    }
    if (self && strcasecmp(name, self) == 0) return false;
    if (type == PEERS_RR_A || type == PEERS_RR_AAAA) {
        e = find(t, name);
        if (!e) return false;
    } else if (ttl == 0) {
        e = find(t, name);
        if (e && (type == PEERS_RR_PTR || type == PEERS_RR_SRV)) { drop(t, e); return true; }
        return false;
    } else {
        e = get(t, name);
        if (!e) return false;
    }
    peers_entry_t before;
    memcpy(&before, e, sizeof before);
    bool fresh = before.last_ms == 0;
    if (type == PEERS_RR_TXT) read_txt(e, rd, rdlen);
    else if (type == PEERS_RR_SRV && rdlen >= 6) e->http_port = (uint16_t)(rd[4] << 8 | rd[5]);
    else if (type == PEERS_RR_A && ttl) { e->has_ip4 = true; memcpy(e->ip4, rd, 4); }
    else if (type == PEERS_RR_A) e->has_ip4 = false;
    else if (type == PEERS_RR_AAAA && ttl) {
        /* a tester only has a link-local address; keep that one if a global ever shows up too */
        bool ll = rd[0] == 0xfe && (rd[1] & 0xc0) == 0x80;
        if (ll || !e->has_ip6) { e->has_ip6 = true; memcpy(e->ip6, rd, 16); }
    } else if (type == PEERS_RR_AAAA && !memcmp(e->ip6, rd, 16)) e->has_ip6 = false;
    e->last_ms = now_ms ? now_ms : 1;
    before.last_ms = e->last_ms;
    if (fresh || memcmp(&before, e, sizeof before) != 0) { t->generation++; return true; }
    return false;
}

bool peers_expire(peers_table_t *t, uint32_t now_ms, uint32_t max_age_ms)
{
    bool any = false;
    for (int i = 0; i < PEERS_MAX; i++) {
        peers_entry_t *e = &t->p[i];
        if (e->used && now_ms - e->last_ms > max_age_ms) { drop(t, e); any = true; }
    }
    return any;
}
