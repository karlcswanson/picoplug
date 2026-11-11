#ifndef MAC_ID_H
#define MAC_ID_H
#include <stddef.h>
#include <stdint.h>

/* The low 3 bytes of the tester's Ethernet MAC, from the flash chip's unique ID. The ID is a lot
 * code plus a number within the lot (this board's: "UM8868" then 0x0019), so its last bytes
 * repeat across lots: all of it is hashed (FNV-1a), so every byte of the ID counts. */
void mac_id_suffix(const uint8_t *id, size_t len, uint8_t out[3]);

#endif
