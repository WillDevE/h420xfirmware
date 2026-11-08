/*
 * Vendor report builder and non-blocking USB handoff.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef H420X_REPORT_H
#define H420X_REPORT_H

#include <stdint.h>

void report_init(void);
void report_publish(int32_t x, int32_t y, uint16_t pressure, uint8_t status);
void report_task(void);

/* True when a report is waiting for the endpoint. */
int report_pending(void);

/* rhythm1000 (profile 4) only: the u16 placed in wire bytes 10-11 by the
 * next report_publish(). */
void report_set_aux(uint16_t value);

/* -Diag18 only: a non-pen frame. Wire bytes: 1 status, 2..7 body[0..5], 8..9 the
 * current sequence, 10..11 aux (low byte first). Latest wins, like report_publish(). */
void report_publish_frame(uint8_t status, const uint8_t body[6], uint16_t aux);

#endif /* H420X_REPORT_H */
