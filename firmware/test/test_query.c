/* Host tests for query.c: iteration, decoding, lookup. */
#include "query.h"
#include <stdio.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)
#define CHECK_STR(a, b) do { if (strcmp((a), (b)) != 0) { fprintf(stderr, "%s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); return 1; } } while (0)

static int test_iterate(void)
{
    const char *cur = "ping=1.1.1.1&count=5&&stop&=x&trailing=";
    const char *k, *v; size_t kl, vl;
    CHECK(query_next(&cur, &k, &kl, &v, &vl)); CHECK(query_key_is(k, kl, "ping")); CHECK(vl == 7 && !memcmp(v, "1.1.1.1", 7));
    CHECK(query_next(&cur, &k, &kl, &v, &vl)); CHECK(query_key_is(k, kl, "count")); CHECK(vl == 1 && v[0] == '5');
    CHECK(query_next(&cur, &k, &kl, &v, &vl)); CHECK(query_key_is(k, kl, "stop")); CHECK(vl == 0);     /* bare key; "&&" skipped */
    CHECK(query_next(&cur, &k, &kl, &v, &vl)); CHECK(kl == 0 && vl == 1);                               /* "=x": empty key */
    CHECK(query_next(&cur, &k, &kl, &v, &vl)); CHECK(query_key_is(k, kl, "trailing")); CHECK(vl == 0);
    CHECK(!query_next(&cur, &k, &kl, &v, &vl));
    CHECK(!query_next(&cur, &k, &kl, &v, &vl));          /* stays exhausted */
    cur = ""; CHECK(!query_next(&cur, &k, &kl, &v, &vl));
    cur = NULL; CHECK(!query_next(&cur, &k, &kl, &v, &vl));
    CHECK(!query_key_is("pingx", 5, "ping")); CHECK(!query_key_is("pin", 3, "ping"));
    return 0;
}

static int test_decode(void)
{
    char out[16];
    CHECK(query_decode("a%20b+c", 7, out, sizeof out)); CHECK_STR(out, "a b c");
    CHECK(query_decode("%41%7a%7A", 9, out, sizeof out)); CHECK_STR(out, "Azz");
    CHECK(query_decode("  trim+me  ", 11, out, sizeof out)); CHECK_STR(out, "trim me");
    CHECK(query_decode("", 0, out, sizeof out)); CHECK_STR(out, "");
    CHECK(query_decode("+++", 3, out, sizeof out)); CHECK_STR(out, "");
    CHECK(!query_decode("%4", 2, out, sizeof out));          /* short escape */
    CHECK(!query_decode("%zz", 3, out, sizeof out));         /* bad hex */
    CHECK(!query_decode("abc%", 4, out, sizeof out));        /* escape at end */
    CHECK(query_decode("123456789012345", 15, out, sizeof out)); CHECK_STR(out, "123456789012345");
    CHECK(!query_decode("1234567890123456", 16, out, sizeof out));   /* does not fit with terminator */
    CHECK(!query_decode("x", 1, out, 0));
    return 0;
}

static int test_get(void)
{
    char out[32];
    const char *q = "host=one.one.one.one&count=0&name=a%26b&bad=%2&lldp=1";
    CHECK(query_get(q, "host", out, sizeof out) == 1); CHECK_STR(out, "one.one.one.one");
    CHECK(query_get(q, "count", out, sizeof out) == 1); CHECK_STR(out, "0");
    CHECK(query_get(q, "name", out, sizeof out) == 1); CHECK_STR(out, "a&b");
    CHECK(query_get(q, "lldp", out, sizeof out) == 1); CHECK_STR(out, "1");
    CHECK(query_get(q, "bad", out, sizeof out) == -1);
    CHECK(query_get(q, "missing", out, sizeof out) == 0);
    CHECK(query_get(q, "hos", out, sizeof out) == 0);
    CHECK(query_get("host=123456", "host", out, 4) == -1);     /* too long for the buffer */
    CHECK(query_get("", "host", out, sizeof out) == 0);
    CHECK(query_get(NULL, "host", out, sizeof out) == 0);
    CHECK(query_get("stop", "stop", out, sizeof out) == 1); CHECK_STR(out, "");
    return 0;
}

int main(int argc, char **argv)
{
    const char *t = argc > 1 ? argv[1] : "";
    if (!strcmp(t, "iterate")) return test_iterate();
    if (!strcmp(t, "decode")) return test_decode();
    if (!strcmp(t, "get")) return test_get();
    fprintf(stderr, "usage: test_query iterate|decode|get\n");
    return 2;
}
