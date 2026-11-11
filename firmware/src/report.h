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

/* Vendor endpoint free for the next report. -FastEp1 reads the hardware
 * endpoint state; otherwise this is TinyUSB's tud_hid_n_ready() (callers must
 * include tusb.h and usb_descriptors.h; unchanged code in default builds). */
#if defined(FAST_EP1) && FAST_EP1
int report_fast_ready(void);
void report_fast_reset(void);   /* call on mount / unmount */
#define REPORT_EP_READY() (report_fast_ready() != 0)
#else
#define REPORT_EP_READY() tud_hid_n_ready(ITF_VENDOR)
#endif

/* rhythm1000 (profile 4) only: the u16 placed in wire bytes 10-11 by the
 * next report_publish(). */
void report_set_aux(uint16_t value);

/* -Diag18 only: a non-pen frame. Wire bytes: 1 status, 2..7 body[0..5], 8..9 the
 * current sequence, 10..11 aux (low byte first). Latest wins, like report_publish(). */
void report_publish_frame(uint8_t status, const uint8_t body[6], uint16_t aux);

#endif /* H420X_REPORT_H */
