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

#ifndef REPORT_KEEPALIVE
#define REPORT_KEEPALIVE 0
#endif
#ifndef FAST_EP1
#define FAST_EP1 0
#endif
#if FAST_EP1 && REPORT_KEEPALIVE
#error "-FastEp1 and -KeepAlive cannot be combined"
#endif

#if FAST_EP1
/* Round 53 fast path (build.ps1 -FastEp1). The vendor IN endpoint (EP1) is
 * armed by direct USBFS (DWC2) register writes, not through TinyUSB:
 *   - "free" is the hardware state (DIEPCTL1.EPENA clear = the host took the
 *     last packet), not TinyUSB's busy flag, which is cleared only inside
 *     tud_task() and made the once-per-frame arm late by ~35 us;
 *   - the EP1 IN interrupt is masked (DAINTMSK bit 1) once per configuration,
 *     so a completion costs no ISR and no TinyUSB event;
 *   - the packet (report ID + 11-byte payload = 12 bytes, 3 FIFO words) is
 *     written straight into the EP1 TX FIFO.
 * TinyUSB still owns enumeration, EP0, bus reset and SET_CONFIGURATION (which
 * opens EP1 and resets its data toggle); it never arms EP1 in this build.
 * Every packet is a fresh report_publish() frame; nothing is ever repeated. */
#define FEP_BASE       0x50000000UL
#define FEP_DAINTMSK   (*(volatile uint32_t *)(FEP_BASE + 0x081CUL))
#define FEP_DIEPCTL1   (*(volatile uint32_t *)(FEP_BASE + 0x0920UL))
#define FEP_DIEPINT1   (*(volatile uint32_t *)(FEP_BASE + 0x0928UL))
#define FEP_DIEPTSIZ1  (*(volatile uint32_t *)(FEP_BASE + 0x0930UL))
#define FEP_DTXFSTS1   (*(volatile uint32_t *)(FEP_BASE + 0x0938UL))
#define FEP_FIFO1      (*(volatile uint32_t *)(FEP_BASE + 0x2000UL))
#define FEP_EPENA      (1UL << 31)
#define FEP_CNAK       (1UL << 26)
#define FEP_USBAEP     (1UL << 15)
#define FEP_XFRC       0x01UL
#define FEP_DAINT_IN1  (1UL << 1)
#define FEP_PKT_LEN    (VENDOR_REPORT_LEN + 1U)            /* 12 */
#define FEP_PKT_WORDS  ((FEP_PKT_LEN + 3U) / 4U)           /* 3 */

static uint8_t fast_configured;   /* EP1 IN interrupt masked for the current configuration */

void report_fast_reset(void) {
  fast_configured = 0U;
}

int report_fast_ready(void) {
  if (!tud_mounted() || tud_suspended()) {
    return 0;
  }
  const uint32_t ctl = FEP_DIEPCTL1;
  if ((ctl & FEP_USBAEP) == 0U) {
    return 0;   /* EP1 not opened by SET_CONFIGURATION yet */
  }
  if (!fast_configured) {
    uint32_t primask;
    __asm volatile ("mrs %0, primask" : "=r" (primask));
    __asm volatile ("cpsid i" ::: "memory");
    FEP_DAINTMSK &= ~FEP_DAINT_IN1;
    FEP_DIEPINT1 = FEP_XFRC;
    __asm volatile ("msr primask, %0" :: "r" (primask) : "memory");
    fast_configured = 1U;
  }
  if ((ctl & FEP_EPENA) != 0U) {
    return 0;   /* the previous packet is still waiting for the host */
  }
  return ((FEP_DTXFSTS1 & 0xFFFFU) >= FEP_PKT_WORDS) ? 1 : 0;
}

static void fast_send(const uint8_t p[VENDOR_REPORT_LEN]) {
  const uint32_t w0 = (uint32_t)REPORT_ID_VENDOR | ((uint32_t)p[0] << 8) |
                      ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 24);
  const uint32_t w1 = (uint32_t)p[3] | ((uint32_t)p[4] << 8) |
                      ((uint32_t)p[5] << 16) | ((uint32_t)p[6] << 24);
  const uint32_t w2 = (uint32_t)p[7] | ((uint32_t)p[8] << 8) |
                      ((uint32_t)p[9] << 16) | ((uint32_t)p[10] << 24);
  FEP_DIEPINT1 = FEP_XFRC;                        /* forget the previous completion */
  FEP_DIEPTSIZ1 = (1UL << 19) | FEP_PKT_LEN;      /* one packet, 12 bytes */
  FEP_DIEPCTL1 = FEP_DIEPCTL1 | FEP_EPENA | FEP_CNAK;
  FEP_FIFO1 = w0;
  FEP_FIFO1 = w1;
  FEP_FIFO1 = w2;
}
#endif

static uint8_t pending;
static uint8_t payload[VENDOR_REPORT_LEN];
static uint16_t sequence;
#if REPORT_KEEPALIVE
static uint8_t have_report;  /* at least one valid frame has been built */
#endif
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
#if REPORT_KEEPALIVE
  have_report = 0;
#endif
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
#if REPORT_KEEPALIVE
  have_report = 1;
#endif
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
#if FAST_EP1
  if (!pending || !report_fast_ready()) {
    return;
  }
  fast_send(payload);
  pending = 0;
  sequence++;
  return;
#elif REPORT_KEEPALIVE
  if (!tud_hid_n_ready(ITF_VENDOR)) {
    return;
  }
  if (!pending) {
    /* Re-arm with the last raw report so a host poll that arrives before the
     * next frame gets the current state instead of a NAK. Same payload and
     * sequence: this is a repeat, not a new frame. Never send the zeroed
     * startup buffer before the first valid frame. */
    if (!have_report) {
      return;
    }
    (void)tud_hid_n_report(ITF_VENDOR, REPORT_ID_VENDOR, payload,
                           VENDOR_REPORT_LEN);
    return;
  }
#else
  if (!pending || !tud_hid_n_ready(ITF_VENDOR)) {
    return;
  }
#endif
  if (tud_hid_n_report(ITF_VENDOR, REPORT_ID_VENDOR, payload,
                       VENDOR_REPORT_LEN)) {
    pending = 0;
    sequence++;
  }
}

int report_pending(void) {
  return pending != 0;
}
