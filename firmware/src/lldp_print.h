#ifndef LLDP_PRINT_H
#define LLDP_PRINT_H
#include "lldp.h"
/* Dump a parsed neighbor to stdout (debug UART). */
void lldp_print(const lldp_neighbor_t *n);
#endif
