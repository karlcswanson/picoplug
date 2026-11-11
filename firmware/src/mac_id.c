#include "mac_id.h"

void mac_id_suffix(const uint8_t *id, size_t len, uint8_t out[3])
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) { h ^= id[i]; h *= 16777619u; }
    h ^= h >> 24;   /* fold the top byte in: FNV's low bits mix least */
    out[0] = (uint8_t)(h >> 16);
    out[1] = (uint8_t)(h >> 8);
    out[2] = (uint8_t)h;
}
