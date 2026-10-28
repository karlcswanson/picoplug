#include "query.h"

bool query_next(const char **cursor, const char **key, size_t *klen, const char **val, size_t *vlen)
{
    const char *q = *cursor;
    if (!q) return false;
    for (;;) {
        if (!*q) { *cursor = q; return false; }
        const char *amp = strchr(q, '&');
        size_t n = amp ? (size_t)(amp - q) : strlen(q);
        const char *next = amp ? amp + 1 : q + n;
        if (n == 0) { q = next; continue; }           /* "a=1&&b=2" */
        const char *eq = memchr(q, '=', n);
        *key = q;
        *klen = eq ? (size_t)(eq - q) : n;
        *val = eq ? eq + 1 : q + n;
        *vlen = eq ? n - *klen - 1 : 0;
        *cursor = next;
        return true;
    }
}

static int hexval(char c)
{
    return (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
}

bool query_decode(const char *src, size_t n, char *dst, size_t cap)
{
    if (!cap) return false;
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        char c = src[i];
        if (c == '+') c = ' ';
        else if (c == '%') {
            if (i + 2 >= n) return false;
            int h = hexval(src[i + 1]), l = hexval(src[i + 2]);
            if (h < 0 || l < 0) return false;
            c = (char)(h << 4 | l); i += 2;
        }
        if (o + 1 >= cap) return false;
        dst[o++] = c;
    }
    dst[o] = '\0';
    while (o && dst[o - 1] == ' ') dst[--o] = '\0';
    size_t lead = 0;
    while (dst[lead] == ' ') lead++;
    if (lead) memmove(dst, dst + lead, o - lead + 1);
    return true;
}

int query_get(const char *query, const char *key, char *out, size_t cap)
{
    const char *cur = query ? query : "", *k, *v; size_t kl, vl;
    while (query_next(&cur, &k, &kl, &v, &vl)) {
        if (!query_key_is(k, kl, key)) continue;
        return query_decode(v, vl, out, cap) ? 1 : -1;
    }
    return 0;
}
