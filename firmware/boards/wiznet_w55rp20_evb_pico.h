/*
 * Board header for the WIZnet W55RP20-EVB-Pico.
 * The W55RP20 is an RP2040 + W5500 in one package; the W5500 is wired
 * internally to GPIO20-25 (CS 20, SCK 21, MISO 22, MOSI 23, INT 24, RST 25)
 * and needs a PIO-based SPI (those pins do not map to an SPI block).
 * GPIO17 and 20-25 are not available on the edge connector.
 */
#ifndef _BOARDS_WIZNET_W55RP20_EVB_PICO_H
#define _BOARDS_WIZNET_W55RP20_EVB_PICO_H

// pico_cmake_set PICO_PLATFORM=rp2040
pico_board_cmake_set(PICO_PLATFORM, rp2040)

#define WIZNET_W55RP20_EVB_PICO

// --- W5500 (internal) ---
#define W55RP20_W5500_CS_PIN    20
#define W55RP20_W5500_SCK_PIN   21
#define W55RP20_W5500_MISO_PIN  22
#define W55RP20_W5500_MOSI_PIN  23
#define W55RP20_W5500_INTN_PIN  24
#define W55RP20_W5500_RSTN_PIN  25

// --- LED ---
#ifndef PICO_DEFAULT_LED_PIN
#define PICO_DEFAULT_LED_PIN 19
#endif

// --- UART ---
#ifndef PICO_DEFAULT_UART
#define PICO_DEFAULT_UART 0
#endif
#ifndef PICO_DEFAULT_UART_TX_PIN
#define PICO_DEFAULT_UART_TX_PIN 0
#endif
#ifndef PICO_DEFAULT_UART_RX_PIN
#define PICO_DEFAULT_UART_RX_PIN 1
#endif

// --- I2C ---
#ifndef PICO_DEFAULT_I2C
#define PICO_DEFAULT_I2C 0
#endif
#ifndef PICO_DEFAULT_I2C_SDA_PIN
#define PICO_DEFAULT_I2C_SDA_PIN 4
#endif
#ifndef PICO_DEFAULT_I2C_SCL_PIN
#define PICO_DEFAULT_I2C_SCL_PIN 5
#endif

// --- FLASH ---
#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1
#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 2
#endif
// pico_cmake_set_default PICO_FLASH_SIZE_BYTES = (2 * 1024 * 1024)
pico_board_cmake_set_default(PICO_FLASH_SIZE_BYTES, (2 * 1024 * 1024))
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (2 * 1024 * 1024)
#endif

#define PICO_SMPS_MODE_PIN 16
#define PICO_VBUS_PIN 18

#endif
