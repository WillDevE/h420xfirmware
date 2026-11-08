/*
 * Board support: 72 MHz clock tree, 921600 baud debug UART, non-blocking TX.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef H420X_BOARD_H
#define H420X_BOARD_H

#include <stddef.h>
#include <stdint.h>

extern uint32_t SystemCoreClock;

void board_init(void);

void uart_init(void);
void uart_write(const void *data, size_t len);
void uart_puts(const char *s);
void uart_u32(uint32_t value);
void uart_hex32(uint32_t value);
void uart_poll(void);

#endif /* H420X_BOARD_H */
