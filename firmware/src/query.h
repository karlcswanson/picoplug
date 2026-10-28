/*
 * query.h - URL query-string helpers shared by the HTTP handlers and settings.
 *
 * Pure C, no lwIP: host-tested in test/test_query.c.
 */
#ifndef QUERY_H
#define QUERY_H

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* Walk "k=v&k2=v2&flag". Start with *cursor = query; each call sets key/klen
 * and val/vlen (pointers into the query, still URL-encoded; vlen 0 for a bare
 * key) and advances. Returns false when the string is exhausted. Empty
 * segments ("a=1&&b=2") are skipped. */
bool query_next(const char **cursor, const char **key, size_t *klen, const char **val, size_t *vlen);

/* URL-decode src[0..n) into dst (cap includes the terminator): %XX and '+'.
 * Leading/trailing spaces are trimmed. False on a bad escape or if it does
 * not fit; dst is then unspecified. */
bool query_decode(const char *src, size_t n, char *dst, size_t cap);

/* Find key in the query and decode its value into out.
 * 1 = found, 0 = absent, -1 = present but malformed or longer than cap-1. */
int query_get(const char *query, const char *key, char *out, size_t cap);

/* Convenience for dispatching on a key from query_next. */
static inline bool query_key_is(const char *key, size_t klen, const char *lit)
{
    return strlen(lit) == klen && memcmp(key, lit, klen) == 0;
}

#endif /* QUERY_H */
