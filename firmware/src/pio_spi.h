/*
 * pio_spi.h - blocking SPI master on a PIO state machine (mode 0, 8-bit).
 */
#ifndef PIO_SPI_H
#define PIO_SPI_H

#include <stddef.h>
#include <stdint.h>
#include "hardware/pio.h"

typedef struct {
    PIO   pio;
    uint  sm;
    uint  offset;
    float clkdiv;
} pio_spi_t;

/* Claim a state machine and start the program. Returns false if PIO is full. */
bool pio_spi_init(pio_spi_t *s, uint pin_sck, uint pin_mosi, uint pin_miso, uint32_t hz);

/* Change the clock. Returns the actual frequency. */
uint32_t pio_spi_set_hz(pio_spi_t *s, uint32_t hz);

/* Full-duplex transfer; tx or rx may be NULL (0x00 is sent / data discarded). */
void pio_spi_xfer(pio_spi_t *s, const uint8_t *tx, uint8_t *rx, size_t n);

#endif /* PIO_SPI_H */
