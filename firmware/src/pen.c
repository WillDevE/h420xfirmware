/*
 * Clean-room pen functions (see pen.h).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "pen.h"
#include "sensing.h"

#define PEN_TIP_HZ             516800U  /* no button: tip above this frequency */
#define PEN_BUTTON_MID_HZ      495000U  /* bins 5..7 */
#define PEN_BUTTON_LOW_HZ      475000U  /* bins 2..4 */
#define PEN_PRESSURE_MAX       8191U
#define PEN_FALLBACK_MIN_AMP   300U     /* weaker fallback peak: no pen resonance */
#define PEN_FALLBACK_BIN       8U
#define PEN_FALLBACK_OFFSET_HZ 300U
#define PEN_QUIET_US           300U     /* hold-off after a detuned probe */

static pen_res res;

void pen_init(void) {
  res.bin = SENSING_DEFAULT_BIN;
  res.probe_index = 0U;
  res.frame_count = 0U;
  res.reserved = 0U;
  res.freq_hz = resonance_hz[SENSING_DEFAULT_BIN];
  res.peak = 0U;
  res.prev_peak = 0U;
  for (unsigned i = 0; i < 5U; i++) {
    res.probe_amp[i] = 0U;
  }
}

pen_res *pen_state(void) {
  return &res;
}

void pen_set_bin(uint8_t bin) {
  if (bin >= 1U && bin <= 12U) {
    res.bin = bin;
  }
}

uint8_t pen_bin(void) {
  return res.bin;
}

void pen_reset_probe(void) {
  res.probe_index = 0U;
}

/* First index of the largest value. */
static unsigned argmax(const uint16_t *a, unsigned n) {
  unsigned m = 0U;
  for (unsigned i = 1U; i < n; i++) {
    if (a[i] > a[m]) {
      m = i;
    }
  }
  return m;
}

/* Vertex of the parabola through (f[bin-1], a_lo), (f[bin], a_mid),
 * (f[bin+1], a_hi) on the nonuniform bin table, clamped to f[bin-1..bin+1].
 * Both divisions truncate toward zero; a zero denominator gives f[bin].
 * The end bins return the midpoint of their two table frequencies. */
__attribute__((noinline))
uint32_t pen_interp(uint8_t bin, uint32_t a_lo, uint32_t a_mid, uint32_t a_hi) {
  if (bin == 1U) {
    return (resonance_hz[1] + resonance_hz[2]) >> 1;
  }
  if (bin == 12U) {
    return (resonance_hz[11] + resonance_hz[12]) >> 1;
  }
  if (bin < 1U || bin > 12U) {
    return resonance_hz[SENSING_DEFAULT_BIN];   /* unreachable */
  }
  const int64_t da_lo = (int64_t)a_lo - (int64_t)a_mid;
  const int64_t da_hi = (int64_t)a_hi - (int64_t)a_mid;
  const int64_t df_lo = (int64_t)resonance_hz[bin - 1U] - (int64_t)resonance_hz[bin];
  const int64_t df_hi = (int64_t)resonance_hz[bin + 1U] - (int64_t)resonance_hz[bin];
  const int64_t den = da_hi * df_lo - da_lo * df_hi;
  const int64_t num = da_hi * df_lo * df_lo - da_lo * df_hi * df_hi;
  const int64_t x = (den != 0) ? (num / den) / 2 : 0;
  int64_t f = (int64_t)resonance_hz[bin] + x;
  if (f > (int64_t)resonance_hz[bin + 1U]) {
    f = (int64_t)resonance_hz[bin + 1U];
  } else if (f < (int64_t)resonance_hz[bin - 1U]) {
    f = (int64_t)resonance_hz[bin - 1U];
  }
  return (uint32_t)f;
}

/* Fit five amplitudes read at bins r->bin-2..r->bin+2. Returns 1 when the
 * peak is at an edge or weaker than a third of the previous peak (the caller
 * must run the fallback sweep); otherwise moves the bin to the peak, fits the
 * frequency and returns 0. */
__attribute__((noinline))
int pen_fit5(pen_res *r, const uint16_t amp[5]) {
  const unsigned m = argmax(amp, 5U);
  const uint32_t peak = amp[m];
  if (peak < r->prev_peak / 3U || m == 0U || m == 4U) {
    return 1;
  }
  r->bin = (uint8_t)(r->bin + m - 2U);
  r->freq_hz = pen_interp(r->bin, amp[m - 1U], amp[m], amp[m + 1U]);
  r->peak = peak;
  r->prev_peak = peak;
  return 0;
}

/* amp[i] is the read at bin i+1; amp[0], amp[1] and amp[11] are zero and
 * amp[2..10] are bins 3..11. */
__attribute__((noinline))
void pen_fit_fallback(pen_res *r, const uint16_t amp[12]) {
  const unsigned m = argmax(amp, 12U);
  r->bin = (uint8_t)(m + 1U);
  r->peak = amp[m];
  if (amp[m] < PEN_FALLBACK_MIN_AMP) {
    r->bin = PEN_FALLBACK_BIN;
    r->freq_hz = resonance_hz[PEN_FALLBACK_BIN];
  } else if (m <= 1U) {
    r->freq_hz = (resonance_hz[1] + resonance_hz[2]) >> 1;
  } else if (m == 2U) {
    r->freq_hz = (resonance_hz[2] + resonance_hz[3]) >> 1;
  } else if (m == 10U) {
    r->freq_hz = (resonance_hz[10] + resonance_hz[11]) >> 1;
  } else if (m == 11U) {
    r->freq_hz = (resonance_hz[11] + resonance_hz[12]) >> 1;
  } else {
    r->freq_hz = pen_interp(r->bin, amp[m - 1U], amp[m], amp[m + 1U]) -
                 PEN_FALLBACK_OFFSET_HZ;
  }
  r->prev_peak = r->peak;
}

/* Status byte and pressure for the current fit. 16-bit pressure arithmetic,
 * then clamped to 8191. */
__attribute__((noinline))
void pen_fields(const pen_res *r, uint8_t *status, uint16_t *pressure) {
  uint8_t s = PEN_STATUS_IN_RANGE;
  uint16_t p = 0U;
  const uint32_t f = r->freq_hz;
  if (r->bin >= 2U && r->bin <= 4U) {
    s |= PEN_STATUS_BUTTON_LOW;
    if (f > PEN_BUTTON_LOW_HZ) {
      s |= PEN_STATUS_TIP;
      p = (uint16_t)(f - PEN_BUTTON_LOW_HZ);
    }
  } else if (r->bin >= 5U && r->bin <= 7U) {
    s |= PEN_STATUS_BUTTON_MID;
    if (f > PEN_BUTTON_MID_HZ) {
      s |= PEN_STATUS_TIP;
      p = (uint16_t)(f - PEN_BUTTON_MID_HZ);
    }
  } else if (r->bin >= 8U && r->bin <= 11U) {
    if (f > PEN_TIP_HZ) {
      const uint32_t d = f - PEN_TIP_HZ;
      s |= PEN_STATUS_TIP;
      p = (uint16_t)(d - (d >> 3));
    }
  }
  if (p > PEN_PRESSURE_MAX) {
    p = PEN_PRESSURE_MAX;
  }
  *status = s;
  *pressure = p;
}

/* Excite X coil x_coil, receive Y coil y_coil; bins outside 1..12 read 0. */
static uint16_t probe_read(uint8_t x_coil, uint8_t y_coil, int bin,
                           uint8_t long_gap) {
  if (bin < 1 || bin > 12) {
    return 0U;
  }
  return sensing_read_coil_gap(AXIS_Y, y_coil, x_coil, (uint8_t)bin, long_gap);
}

static void fallback_sweep(uint8_t x_coil, uint8_t y_coil) {
  uint16_t amp[12];
  amp[0] = 0U;
  amp[1] = 0U;
  amp[11] = 0U;
  for (unsigned i = 2U; i <= 10U; i++) {
    amp[i] = probe_read(x_coil, y_coil, (int)i + 1, (uint8_t)(i != 10U));
  }
  pen_fit_fallback(&res, amp);
}

uint8_t pen_acquire(uint8_t x_coil, uint8_t y_coil) {
  uint16_t amp[5];
  for (unsigned u = 0U; u < 5U; u++) {
    amp[u] = probe_read(x_coil, y_coil, (int)res.bin + (int)u - 2,
                        (uint8_t)(u < 4U));
  }
  if (pen_fit5(&res, amp)) {
    fallback_sweep(x_coil, y_coil);
  }
  res.probe_index = 0U;
  return res.bin;
}

uint8_t pen_maintain(uint8_t x_coil, uint8_t y_coil) {
  res.frame_count = (uint8_t)(res.frame_count + 1U);
  if ((res.frame_count & PEN_PROBE_MASK) != 0U) {
    return 0U;
  }
  if (res.probe_index >= 5U) {
    res.probe_index = 0U;
  }
  const unsigned i = res.probe_index;
  res.probe_amp[i] = probe_read(x_coil, y_coil, (int)res.bin + (int)i - 2, 0U);
  res.probe_index = (uint8_t)(i + 1U);
  if (res.probe_index == 5U) {
    res.probe_index = 0U;
    if (pen_fit5(&res, res.probe_amp)) {
      fallback_sweep(x_coil, y_coil);
    }
  }
  sensing_hold_off_us(PEN_QUIET_US);
  return 1U;
}
