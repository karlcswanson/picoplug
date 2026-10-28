/*
 * pio_spi.c - see pio_spi.h.
 */
#include "pio_spi.h"
#include "pio_spi.pio.h"

#include "hardware/clocks.h"

static float clkdiv_for(uint32_t hz)
{
    float div = (float)clock_get_hz(clk_sys) / (4.0f * (float)hz);   /* 4 PIO cycles per bit */
    if (div < 1.0f) div = 1.0f;
    if (div > 65535.0f) div = 65535.0f;
    return div;
}

bool pio_spi_init(pio_spi_t *s, uint pin_sck, uint pin_mosi, uint pin_miso, uint32_t hz)
{
    if (!pio_claim_free_sm_and_add_program(&pio_spi_mode0_program, &s->pio, &s->sm, &s->offset))
        return false;
    s->clkdiv = clkdiv_for(hz);
    pio_spi_mode0_program_init(s->pio, s->sm, s->offset, s->clkdiv, pin_sck, pin_mosi, pin_miso);
    return true;
}

uint32_t pio_spi_set_hz(pio_spi_t *s, uint32_t hz)
{
    s->clkdiv = clkdiv_for(hz);
    pio_sm_set_clkdiv(s->pio, s->sm, s->clkdiv);
    pio_sm_clkdiv_restart(s->pio, s->sm);
    return (uint32_t)((float)clock_get_hz(clk_sys) / (4.0f * s->clkdiv));
}

void pio_spi_xfer(pio_spi_t *s, const uint8_t *tx, uint8_t *rx, size_t n)
{
    io_rw_8 *txfifo = (io_rw_8 *)&s->pio->txf[s->sm];
    io_rw_8 *rxfifo = (io_rw_8 *)&s->pio->rxf[s->sm];
    size_t tx_left = n, rx_left = n;

    /* Keep the TX FIFO fed while draining RX; never let RX overflow
     * (FIFO depth is 4 in each direction). */
    while (tx_left || rx_left) {
        if (tx_left && !pio_sm_is_tx_fifo_full(s->pio, s->sm) && rx_left - tx_left < 4) {
            *txfifo = tx ? *tx++ : 0x00;
            tx_left--;
        }
        if (rx_left && !pio_sm_is_rx_fifo_empty(s->pio, s->sm)) {
            uint8_t b = *rxfifo;
            if (rx) *rx++ = b;
            rx_left--;
        }
    }
}
