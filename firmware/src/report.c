/*
 * Vendor report builder and non-blocking USB handoff.
 *
 * One-deep latest-wins mailbox: a busy endpoint never queues a backlog of
 * stale absolute positions. The report is queued only when TinyUSB says the
 * interrupt IN endpoint is ready.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "report.h"
#include "usb_descriptors.h"
#include "tusb.h"

static uint8_t pending;
static uint8_t payload[VENDOR_REPORT_LEN];
static uint16_t sequence;
#ifndef LOOP_MODES
#define LOOP_MODES 0
#endif
#ifndef LOOP_DIAG
#define LOOP_DIAG 0
#endif
#ifndef LOOP_HEARTBEAT
#define LOOP_HEARTBEAT 0
#endif
#if H420X_PROFILE == 4 || LOOP_MODES
static uint16_t report_aux;  /* rhythm1000 / -Modes frame diagnostics, wire bytes 10-11 */

void report_set_aux(uint16_t value) {
  report_aux = value;
}
#endif

void report_init(void) {
  pending = 0;
  sequence = 0;
  for (unsigned i = 0; i < VENDOR_REPORT_LEN; i++) {
    payload[i] = 0;
  }
}

void report_publish(int32_t x, int32_t y, uint16_t pressure, uint8_t status) {
  if (x < 0) x = 0;
  if (x > 21200) x = 21200;
  if (y < 0) y = 0;
  if (y > 13200) y = 13200;
  if (pressure > 8191U) pressure = 8191U;

  payload[0] = status;
  payload[1] = (uint8_t)(x & 0xFF);
  payload[2] = (uint8_t)((x >> 8) & 0xFF);
  payload[3] = (uint8_t)(y & 0xFF);
  payload[4] = (uint8_t)((y >> 8) & 0xFF);
  payload[5] = (uint8_t)(pressure & 0xFF);
  payload[6] = (uint8_t)((pressure >> 8) & 0xFF);
  payload[7] = (uint8_t)(sequence & 0xFF);
  payload[8] = (uint8_t)((sequence >> 8) & 0xFF);
#if H420X_PROFILE == 4 || LOOP_MODES
  payload[9] = (uint8_t)(report_aux & 0xFFU);
  payload[10] = (uint8_t)(report_aux >> 8);
#else
  payload[9] = 0;
  payload[10] = 0;
#endif
  pending = 1;
}

#if LOOP_DIAG || LOOP_HEARTBEAT
void report_publish_frame(uint8_t status, const uint8_t body[6], uint16_t aux) {
  payload[0] = status;
  for (unsigned i = 0; i < 6U; i++) {
    payload[1U + i] = body[i];
  }
  payload[7] = (uint8_t)(sequence & 0xFF);
  payload[8] = (uint8_t)((sequence >> 8) & 0xFF);
  payload[9] = (uint8_t)(aux & 0xFFU);
  payload[10] = (uint8_t)(aux >> 8);
  pending = 1;
}
#endif

void report_task(void) {
  if (!pending || !tud_hid_n_ready(ITF_VENDOR)) {
    return;
  }
  if (tud_hid_n_report(ITF_VENDOR, REPORT_ID_VENDOR, payload,
                       VENDOR_REPORT_LEN)) {
    pending = 0;
    sequence++;
  }
}

int report_pending(void) {
  return pending != 0;
}
