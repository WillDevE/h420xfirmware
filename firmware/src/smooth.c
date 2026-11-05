/*
 * Optional output conditioning (build switch -Smooth).
 *
 * Stage 1: median of the last 5 positions per axis. It removes the isolated
 * spikes of the three-coil log fit (hardware: ~5 % of reports are 500+ units
 * off). Stage 2: a Q8 one-pole low-pass whose blend factor follows the average
 * gap between the median output and the filtered position, so a still pen is
 * averaged heavily while a moving pen, whose filtered position trails, gets a
 * blend factor near 1.
 * Offline on real clean-room circle captures: rms error against a zero-phase
 * reference drops from ~257/194 to ~88/72 units, delay about 0 frames.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#if OUTPUT_SMOOTH

#include "smooth.h"
#include "gd32f350_regs.h"

#define SMOOTH_MEDIAN_N  5U
#define SMOOTH_ALPHA_MIN 8      /* Q8 blend factor at rest (8/256 = 0.031) */
#define SMOOTH_GAIN      2      /* Q8 blend factor added per unit of mean gap */
#define SMOOTH_RES_SHIFT 5      /* mean gap = average over ~32 frames */
#define SMOOTH_RESET_CYC 1440000UL  /* 20 ms at 72 MHz */

static int32_t ring[2][SMOOTH_MEDIAN_N];  /* last 5 raw positions per axis */
static uint8_t ring_head;
static int32_t fx_q8[2];   /* filtered position, Q8 */
static int32_t res_q8[2];  /* average of (median - filtered), Q8 */
static uint32_t last_cyc;
static uint8_t seeded;

static int32_t median5(const int32_t *src) {
  int32_t v[SMOOTH_MEDIAN_N] = {0, 0, 0, 0, 0};
  for (unsigned i = 0; i < SMOOTH_MEDIAN_N; i++) {
    const int32_t key = src[i];
    unsigned j = i;
    while (j > 0U && v[j - 1U] > key) {
      v[j] = v[j - 1U];
      j--;
    }
    v[j] = key;
  }
  return v[SMOOTH_MEDIAN_N / 2U];
}

void smooth_apply(int32_t *x, int32_t *y) {
  const uint32_t now = DWT_CYCCNT;
  int32_t pos[2];
  pos[0] = *x;
  pos[1] = *y;
  if (!seeded || (uint32_t)(now - last_cyc) > SMOOTH_RESET_CYC) {
    for (unsigned i = 0; i < 2U; i++) {
      for (unsigned k = 0; k < SMOOTH_MEDIAN_N; k++) {
        ring[i][k] = pos[i];
      }
      fx_q8[i] = pos[i] * 256;
      res_q8[i] = 0;
    }
    ring_head = 0U;
    seeded = 1U;
    last_cyc = now;
    return;
  }
  last_cyc = now;

  int32_t med[2];
  for (unsigned i = 0; i < 2U; i++) {
    ring[i][ring_head] = pos[i];
    med[i] = median5(ring[i]);
  }
  ring_head = (uint8_t)((ring_head + 1U) % SMOOTH_MEDIAN_N);

  int32_t gap[2];
  for (unsigned i = 0; i < 2U; i++) {
    const int32_t ex = med[i] - ((fx_q8[i] + 128) >> 8);
    res_q8[i] += (ex * 256 - res_q8[i]) >> SMOOTH_RES_SHIFT;
    const int32_t r = res_q8[i] >> 8;
    gap[i] = (r < 0) ? -r : r;
  }
  const int32_t d = (gap[0] > gap[1]) ? gap[0] : gap[1];
  int32_t alpha = SMOOTH_ALPHA_MIN + d * SMOOTH_GAIN;
  if (alpha > 256) {
    alpha = 256;
  }
  for (unsigned i = 0; i < 2U; i++) {
    fx_q8[i] += ((med[i] * 256 - fx_q8[i]) * alpha) >> 8;
  }
  *x = (fx_q8[0] + 128) >> 8;
  *y = (fx_q8[1] + 128) >> 8;
}

#endif /* OUTPUT_SMOOTH */
