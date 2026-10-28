/*
 * fault.c - hard fault handler: print the stacked registers to the debug
 * UART (bypassing stdio) and to the CDC port if possible, then let the
 * watchdog reboot. Overrides the SDK's weak isr_hardfault.
 */
#include "board_config.h"
#include "hardware/uart.h"
#include "hardware/structs/watchdog.h"
#include "pico/stdlib.h"
#include <stdio.h>

#define FAULT_MAGIC 0xFA17FA17u
#define PANIC_MAGIC 0x9A41C000u

static void put_hex(uint32_t v)
{
    static const char hx[] = "0123456789abcdef";
    char s[9];
    for (int i = 7; i >= 0; i--) { s[i] = hx[v & 0xF]; v >>= 4; }
    s[8] = '\0';
    uart_puts(uart0, s);
}

void __attribute__((used)) hardfault_report(uint32_t *sp)
{
    static const char *const names[] = {"r0", "r1", "r2", "r3", "r12", "lr", "pc", "xpsr"};
    uart_puts(uart0, "\r\n*** HARD FAULT ***\r\n");
    for (int i = 0; i < 8; i++) {
        uart_puts(uart0, names[i]);
        uart_puts(uart0, "=0x");
        put_hex(sp[i]);
        uart_puts(uart0, "\r\n");
    }
    /* Survive the watchdog reset: main() reports these next boot. */
    /* scratch 4..7 belong to the bootrom's watchdog-vector mechanism */
    watchdog_hw->scratch[2] = FAULT_MAGIC;
    watchdog_hw->scratch[3] = sp[6];   /* pc */
    uart_puts(uart0, "waiting for watchdog\r\n");
    while (1) tight_loop_contents();
}

uint32_t g_last_fault_pc, g_last_fault_lr, g_last_fault_r0;

/* Call early in main(): pick up a fault record left by the previous boot. */
void fault_report_previous(void)
{
    if (watchdog_hw->scratch[2] == PANIC_MAGIC) {
        g_last_fault_pc = watchdog_hw->scratch[3];       /* message pointer, not a PC */
        watchdog_hw->scratch[2] = 0;
        printf("main: previous boot PANICKED: %s\n", (const char *)g_last_fault_pc);
    }
    if (watchdog_hw->scratch[2] == FAULT_MAGIC) {
        g_last_fault_pc = watchdog_hw->scratch[3];
        watchdog_hw->scratch[2] = 0;
        printf("main: previous boot HARD FAULTED at pc=0x%08lx\n", (unsigned long)g_last_fault_pc);
    }
}

/* Naked: pick the active stack pointer and hand the exception frame over. */
void __attribute__((naked)) isr_hardfault(void)
{
    __asm volatile(
        "movs r0, #4\n"
        "mov r1, lr\n"
        "tst r0, r1\n"
        "beq 1f\n"
        "mrs r0, psp\n"
        "b 2f\n"
        "1: mrs r0, msp\n"
        "2: ldr r1, =hardfault_report\n"
        "bx r1\n");
}

/* ---- panic capture ------------------------------------------------------
 * The SDK routes panic() here (PICO_PANIC_FUNCTION). lwIP asserts land here
 * too (LWIP_PLATFORM_ASSERT -> panic). Keep the message pointer where a
 * debugger halt or the next boot can find it, print it on UART0, and wait
 * for the watchdog.
 */
#include <stdarg.h>
void __attribute__((noreturn)) picolldp_panic(const char *fmt, ...)
{
    watchdog_hw->scratch[2] = PANIC_MAGIC;
    watchdog_hw->scratch[3] = (uint32_t)fmt;
    uart_puts(uart0, "\r\n*** PANIC: ");
    if (fmt) {
        char buf[160];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(buf, sizeof buf, fmt, ap);
        va_end(ap);
        uart_puts(uart0, buf);
    }
    uart_puts(uart0, "\r\nwaiting for watchdog\r\n");
    while (1) tight_loop_contents();
}
