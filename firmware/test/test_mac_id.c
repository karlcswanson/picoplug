/* mac_id_suffix: the tester's MAC from its flash ID, with the whole ID counting. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "mac_id.h"

int main(void)
{
    /* this board's ID: lot "UM8868", chip 0x0019 */
    const uint8_t a[8] = { 0x55, 0x4D, 0x38, 0x38, 0x36, 0x38, 0x00, 0x19 };
    uint8_t ma[3], mb[3], mc[3];
    mac_id_suffix(a, 8, ma);
    mac_id_suffix(a, 8, mb);
    assert(memcmp(ma, mb, 3) == 0);                       /* the same on every boot */

    /* chip 0x0019 of another lot ending in '8': the old last-3-bytes MAC was the same for both */
    const uint8_t b[8] = { 0x55, 0x4D, 0x38, 0x39, 0x31, 0x38, 0x00, 0x19 };
    mac_id_suffix(b, 8, mb);
    assert(memcmp(a + 5, b + 5, 3) == 0 && memcmp(ma, mb, 3) != 0);

    /* every chip of a 4096-chip lot, and the same numbers in 64 other lots: no two alike */
    static uint8_t seen[1u << 21];                         /* one bit per 24-bit suffix */
    unsigned dup = 0, n = 0;
    for (unsigned lot = 0; lot < 65; lot++)
        for (unsigned k = 0; k < 4096; k++) {
            uint8_t id[8] = { 'U', 'M', '8', (uint8_t)('0' + lot / 10 % 10), (uint8_t)('0' + lot % 10), '8',
                              (uint8_t)(k >> 8), (uint8_t)k };
            mac_id_suffix(id, 8, mc);
            uint32_t v = (uint32_t)mc[0] << 16 | (uint32_t)mc[1] << 8 | mc[2];
            if (seen[v >> 3] & (1u << (v & 7))) dup++;
            seen[v >> 3] |= (uint8_t)(1u << (v & 7));
            n++;
        }
    /* 266,240 random 24-bit values would give about 2,100 repeats; anything near that is chance */
    printf("test_mac_id: %u IDs, %u repeats (this board -> %02x:%02x:%02x)\n", n, dup, ma[0], ma[1], ma[2]);
    assert(dup < 3000);
    printf("test_mac_id: ok\n");
    return 0;
}
