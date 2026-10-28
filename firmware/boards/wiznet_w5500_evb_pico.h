/*
 * Board header for the WIZnet W5500-EVB-Pico (rp2040).
 * Derived from the SDK's wiznet_w5100s_evb_pico.h: same pinout, W5500 chip.
 * Selected with -DPICO_BOARD=wiznet_w5500_evb_pico (PICO_BOARD_HEADER_DIRS
 * points here from firmware/CMakeLists.txt).
 */
#ifndef _BOARDS_WIZNET_W5500_EVB_PICO_H
#define _BOARDS_WIZNET_W5500_EVB_PICO_H

// pico_cmake_set PICO_PLATFORM=rp2040
pico_board_cmake_set(PICO_PLATFORM, rp2040)

#define WIZNET_W5500_EVB_PICO


// --- W5500 ---
#ifndef W5500_EVB_PICO_INTN_PIN
#define W5500_EVB_PICO_INTN_PIN 21
#endif
#ifndef W5500_EVB_PICO_RSTN_PIN
#define W5500_EVB_PICO_RSTN_PIN 20
#endif

// --- LED ---
#ifndef PICO_DEFAULT_LED_PIN
#define PICO_DEFAULT_LED_PIN 25
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

// --- SPI (wired to the W5500) ---
#ifndef PICO_DEFAULT_SPI
#define PICO_DEFAULT_SPI 0
#endif
#ifndef PICO_DEFAULT_SPI_SCK_PIN
#define PICO_DEFAULT_SPI_SCK_PIN 18
#endif
#ifndef PICO_DEFAULT_SPI_TX_PIN
#define PICO_DEFAULT_SPI_TX_PIN 19
#endif
#ifndef PICO_DEFAULT_SPI_RX_PIN
#define PICO_DEFAULT_SPI_RX_PIN 16
#endif
#ifndef PICO_DEFAULT_SPI_CSN_PIN
#define PICO_DEFAULT_SPI_CSN_PIN 17
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

#define PICO_SMPS_MODE_PIN 23

#endif
