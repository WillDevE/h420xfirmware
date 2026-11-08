/*
 * Clean-room pen functions: resonance fit, pressure, tip and barrel buttons.
 *
 * The pen's resonant frequency encodes its state. With no barrel button the
 * resonance sits in bins 8..11 and tip pressure raises it above 516.8 kHz.
 * Each barrel button moves the resonance down into its own band (bins 5..7,
 * tip above 495 kHz; bins 2..4, tip above 475 kHz), where tip pressure again
 * raises it. The frequency is fitted from reads that excite the X center coil
 * and receive on the Y center coil at neighbouring bins.
 *
 * Behavior of the released pen500 tier: one probe every second tracked frame
 * (bins cur-2..cur+2 in turn), a three-point parabolic fit after five probes,
 * a nine-bin fallback sweep when the peak is weak or at the edge, and linear,
 * unfiltered pressure.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef H420X_PEN_H
#define H420X_PEN_H

#include <stddef.h>
#include <stdint.h>

/* Probe when (++frame_count & PEN_PROBE_MASK) == 0: 1 = every 2nd tracked
 * frame (pen500), 0 = every tracked frame (-FastPressure). */
#ifndef PEN_PROBE_MASK
#define PEN_PROBE_MASK 1U
#endif

#define PEN_STATUS_IN_RANGE   0x80U
#define PEN_STATUS_TIP        0x01U
#define PEN_STATUS_BUTTON_LOW 0x02U  /* resonance in bins 2..4 */
#define PEN_STATUS_BUTTON_MID 0x04U  /* resonance in bins 5..7 */

typedef struct {
  uint8_t bin;            /* resonance bin 1..12 */
  uint8_t probe_index;    /* next probe slot 0..4 */
  uint8_t frame_count;    /* tracked-frame counter for the probe cadence */
  uint8_t reserved;
  uint32_t freq_hz;       /* fitted resonance frequency */
  uint32_t peak;          /* amplitude at the fitted peak */
  uint32_t prev_peak;     /* peak of the previous fit */
  uint16_t probe_amp[5];  /* probe amplitudes at bins bin-2..bin+2 */
} pen_res;

_Static_assert(offsetof(pen_res, freq_hz) == 4U, "pen_res layout");
_Static_assert(offsetof(pen_res, peak) == 8U, "pen_res layout");
_Static_assert(offsetof(pen_res, prev_peak) == 12U, "pen_res layout");
_Static_assert(offsetof(pen_res, probe_amp) == 16U, "pen_res layout");
_Static_assert(sizeof(pen_res) == 28U, "pen_res layout");

void pen_init(void);
pen_res *pen_state(void);
void pen_set_bin(uint8_t bin);
uint8_t pen_bin(void);
void pen_reset_probe(void);

/* Pure helpers (no hardware access). */
uint32_t pen_interp(uint8_t bin, uint32_t a_lo, uint32_t a_mid, uint32_t a_hi);
int pen_fit5(pen_res *r, const uint16_t amp[5]);
void pen_fit_fallback(pen_res *r, const uint16_t amp[12]);
void pen_fields(const pen_res *r, uint8_t *status, uint16_t *pressure);

/* Reads: five-bin classifier at acquisition; per-frame probe maintenance. */
uint8_t pen_acquire(uint8_t x_coil, uint8_t y_coil);
uint8_t pen_maintain(uint8_t x_coil, uint8_t y_coil);

#endif /* H420X_PEN_H */
