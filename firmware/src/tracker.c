/*
 * Clean-room three-coil tracker.
 *
 * The math is the same-frame Gaussian/log-domain fit used by the accepted
 * 750/900 experiments, re-expressed as clean-room C. It uses only this
 * frame's three amplitudes. No averaging, interpolation across frames,
 * previous-frame blend, or rate limiter is present.
 * Rescan rule: if a moving axis's neighbor coil exceeds the center by more
 * than 25 %, tracker_update() re-centers and asks the caller to re-read.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "tracker.h"

#define HYST_SHIFT 5U          /* 1/32 ownership hysteresis */
#ifndef LOOP_MODES
#define LOOP_MODES 0
#endif
#if LOOP_MODES
/* Round 9 loop modes: run-time ownership hysteresis (tracker_set_hyst_shift). */
static uint8_t hyst_shift = HYST_SHIFT;
static uint8_t slot_learn = 1U;   /* round 24: online slot-gain learner on/off */
static uint8_t fit_mode = 0U;     /* round 25: 0 = log fit, 1 = stock-style linear fit */

void tracker_set_hyst_shift(uint8_t shift) {
  hyst_shift = (shift >= 2U && shift <= 8U) ? shift : (uint8_t)HYST_SHIFT;
}

void tracker_set_slot_learn(uint8_t enable) {
  slot_learn = (enable != 0U) ? 1U : 0U;
}

void tracker_set_fit_mode(uint8_t mode) {
  fit_mode = (mode != 0U) ? 1U : 0U;
}
#define HYST_NOW hyst_shift
#else
#define HYST_NOW HYST_SHIFT
#endif
#define CAL_LIMIT 8192         /* |slot-gain correction| */
#define LOG_NUM_LIMIT (1 << 20)
#define WEAK_PEAK 150U         /* peak coil amplitude at or below: weak window */
/* Defer a handoff (report from the off-peak window, move the center for the
 * next frame) only if the neighbor is <= 125 % of the center. build.ps1
 * -EarlyRecenter sets TRACK_DEFER_SHIFT 5 (3 %, the same as the ownership
 * hysteresis): never defer, re-center and re-read at once. */
#ifndef TRACK_DEFER_SHIFT
#define TRACK_DEFER_SHIFT 2U
#endif
#define DEFER_SHIFT TRACK_DEFER_SHIFT

/* Center limits. A window is the four coils center-2 .. center+1 and the fit
 * uses center-1 .. center+1. The plain limits (3..17 X, 3..11 Y) leave the
 * outermost coil out of every fit, so a pen over the first or last coil
 * strip (about 0.5 coil pitch at each edge of the report range) had no
 * valid fit. build.ps1 -EdgeExt (TRACK_EDGE_EXT 1) widens them by one coil
 * on each side (2..18 X, 2..12 Y); sensing_read_window() reads three coils
 * at center 2. */
#ifndef TRACK_EDGE_EXT
#define TRACK_EDGE_EXT 0
#endif
#if TRACK_EDGE_EXT
#define CENTER_LO 2
#define CENTER_HI_X 18
#define CENTER_HI_Y 12
#else
#define CENTER_LO 3
#define CENTER_HI_X 17
#define CENTER_HI_Y 11
#endif

static const uint16_t log2_frac[65] = {
    0, 1466, 2909, 4331, 5732, 7112, 8473, 9814, 11136, 12440, 13727,
    14996, 16248, 17484, 18704, 19909, 21098, 22272, 23433, 24579,
    25711, 26830, 27936, 29029, 30109, 31178, 32234, 33279, 34312,
    35334, 36346, 37346, 38336, 39316, 40286, 41246, 42196, 43137,
    44068, 44990, 45904, 46809, 47705, 48593, 49472, 50344, 51207,
    52063, 52911, 53751, 54584, 55410, 56229, 57040, 57845, 58643,
    59434, 60219, 60997, 61769, 62534, 63294, 64047, 64794, 0,
};

/* log2(a) in Q16 for a >= 1. The 0 entry at index 64 is restored from the
 * index itself, keeping the table in halfwords. */
static int32_t log2q(uint32_t a) {
  unsigned n = 31U - (unsigned)__builtin_clz(a | 1U);
  uint32_t m = (a | 1U) << (31U - n);
  unsigned i = (m >> 25) & 63U;
  uint32_t r = (m >> 9) & 0xFFFFU;
  uint32_t lo = log2_frac[i];
  uint32_t hi = log2_frac[i + 1U] | (((i + 1U) & 64U) << 10);
  return (int32_t)((n << 16) + lo + (((hi - lo) * r) >> 16));
}

static uint8_t peak_index(const uint16_t a[3]) {
  uint32_t keep = (uint32_t)a[1] + ((uint32_t)a[1] >> HYST_NOW);
  if ((uint32_t)a[0] <= keep && (uint32_t)a[2] <= keep) {
    return 1U;
  }
  return ((uint32_t)a[2] > (uint32_t)a[0]) ? 2U : 0U;
}

static int32_t log_position(uint8_t center, const uint16_t a[3],
                            uint16_t pitch, int32_t k, int32_t e,
                            int32_t *curvature_out) {
  int32_t ll = log2q(a[0]);
  int32_t lm = log2q(a[1]);
  int32_t lr = log2q(a[2]);

  int32_t curvature = 2 * lm - ll - lr + e;
  if (curvature_out) {
    *curvature_out = curvature;
  }

  int32_t den = 2 * curvature;
  if (den <= 0) {
    den = 1;
  }
  int32_t num = lr - ll;
  if (num > LOG_NUM_LIMIT) {
    num = LOG_NUM_LIMIT;
  } else if (num < -LOG_NUM_LIMIT) {
    num = -LOG_NUM_LIMIT;
  }

  int32_t p = (int32_t)pitch;
  int32_t o = (p * num) / den;
  if (o > p) {
    o = p;
  } else if (o < -p) {
    o = -p;
  }

  /* Fixed odd cubic correction for the measured profile. */
  int32_t q = (o * o - (p * p) / 4) / p;
  o += (k * (q * o / p)) >> 16;

  return ((int32_t)center - 2) * p + p / 2 + o;
}

static int32_t axis_index_position(uint8_t axis, uint8_t center,
                                   const uint16_t a[3], int32_t e,
                                   int32_t *curvature) {
#if LOOP_MODES
  if (fit_mode != 0U) {
    /* Round 25: stock-style three-point linear interpolation. a[0..2] are the
     * left, centre and right used coils. The stock reconstructs the peak from
     * the two amplitude differences; this is the same-frame linear fit, with
     * no temporal state or correction. */
    if (curvature) {
      *curvature = 0;
    }
    const int32_t p = (axis == 0U) ? 1267 : 1248;
    const int32_t left = (int32_t)a[1] - (int32_t)a[0];
    const int32_t right = (int32_t)a[1] - (int32_t)a[2];
    int32_t den = left + right;
    if (den <= 0) {
      den = 1;
    }
    int32_t l = (p * left) / den;
    if (l < 0) {
      l = 0;
    } else if (l > p) {
      l = p;
    }
    return ((int32_t)center - 2) * p + l;
  }
#endif
  if (axis == 0U) {
    return log_position(center, a, 1267U, 15185, e, curvature);
  }
  return log_position(center, a, 1248U, 39675, e, curvature);
}

static void learn_axis(tracker_state *state, uint8_t axis, int32_t pos,
                       int32_t curvature) {
#if LOOP_MODES
  if (!slot_learn) {
    return;
  }
#endif
  int32_t p = (axis == 0U) ? 1267 : 1248;
  uint8_t center = (axis == 0U) ? state->x_center : state->y_center;
  uint8_t last = state->last_center[axis];

  if (state->history[axis] >= 2U &&
      (center == (uint8_t)(last + 1U) || (uint8_t)(center + 1U) == last)) {
    int32_t dir = (center > last) ? 1 : -1;
    int32_t jump = (pos - (state->prev_pos[axis] + state->prev_delta[axis])) * dir;
    int32_t along = state->prev_delta[axis] * dir;

    if (along > p / 32 && along < p / 8 &&
        jump < p / 4 && jump > -p / 4) {
      if (jump > p / 16) {
        jump = p / 16;
      } else if (jump < -p / 16) {
        jump = -p / 16;
      }
      const int32_t c = (curvature > 0) ? curvature : 0;
      int32_t e = state->e[axis] - (jump * c) / (16 * p);
      if (e > CAL_LIMIT) {
        e = CAL_LIMIT;
      } else if (e < -CAL_LIMIT) {
        e = -CAL_LIMIT;
      }
      state->e[axis] = e;
    }
  }
}

void tracker_init(tracker_state *state) {
  state->x_center = 10U;
  state->y_center = 7U;
  for (unsigned i = 0; i < 2U; i++) {
    state->e[i] = 0;
    state->prev_pos[i] = 0;
    state->prev_delta[i] = 0;
    state->history[i] = 0;
    state->last_center[i] = 0;
  }
}

/* Pen lost or re-acquired: restart the motion history, keep the learned
 * slot-gain correction (it describes the board, not the stroke). */
void tracker_forget(tracker_state *state) {
  state->history[0] = 0U;
  state->history[1] = 0U;
}

void tracker_set_centers(tracker_state *state, uint8_t x_center,
                         uint8_t y_center) {
  state->x_center = x_center;
  state->y_center = y_center;
  tracker_forget(state);
}

/* Position from the current window (checks already done), learner update,
 * deferred handoff (the center moves for the next frame) and history. */
static int32_t fit_axis(tracker_state *state, uint8_t axis,
                        const uint16_t a[3], uint8_t peak) {
  uint8_t center = (axis == 0U) ? state->x_center : state->y_center;

  int32_t curvature = 0;
  int32_t pos = axis_index_position(axis, center, a, state->e[axis],
                                    &curvature);
  learn_axis(state, axis, pos, curvature);

  if (peak != 1U) {
    int32_t next = (int32_t)center + (int32_t)peak - 1;
    const int32_t hi = (axis == 0U) ? CENTER_HI_X : CENTER_HI_Y;
    if (next < CENTER_LO) next = CENTER_LO;
    if (next > hi) next = hi;
    if (axis == 0U) {
      state->x_center = (uint8_t)next;
    } else {
      state->y_center = (uint8_t)next;
    }
  }

  state->prev_delta[axis] = state->history[axis]
      ? pos - state->prev_pos[axis] : 0;
  state->prev_pos[axis] = pos;
  state->last_center[axis] = center;
  if (state->history[axis] < 2U) {
    state->history[axis]++;
  }
  return pos;
}

void tracker_update(tracker_state *state, const uint16_t x_used[3],
                    const uint16_t y_used[3], tracker_output *out) {
  const uint16_t *used[2] = {x_used, y_used};
  uint8_t peak[2];
  out->flags = 0;
  out->x = 0;
  out->y = 0;

  for (unsigned axis = 0; axis < 2U; axis++) {
    peak[axis] = peak_index(used[axis]);
    if ((uint32_t)used[axis][peak[axis]] <= WEAK_PEAK) {
      out->flags |= TRACK_WEAK;
      return;
    }
  }

  /* A handoff is reported from this window (deferred) only while every
   * moving axis's neighbor is within 25 % of the center. Otherwise the pen
   * is far past the boundary, so re-center now and let the caller re-read
   * the moved axes in this frame. */
  int defer = 1;
  for (unsigned axis = 0; axis < 2U; axis++) {
    const uint16_t *a = used[axis];
    if (peak[axis] != 1U &&
        (uint32_t)a[peak[axis]] > (uint32_t)a[1] + ((uint32_t)a[1] >> DEFER_SHIFT)) {
      defer = 0;
    }
  }
  if (!defer) {
    for (unsigned axis = 0; axis < 2U; axis++) {
      if (peak[axis] == 1U) {
        continue;
      }
      uint8_t *c = (axis == 0U) ? &state->x_center : &state->y_center;
      int32_t next = (int32_t)*c + (int32_t)peak[axis] - 1;
      const int32_t hi = (axis == 0U) ? CENTER_HI_X : CENTER_HI_Y;
      if (next < CENTER_LO) next = CENTER_LO;
      if (next > hi) next = hi;
      if (next != (int32_t)*c) {
        *c = (uint8_t)next;
        out->flags |= (axis == 0U) ? TRACK_RECENTER_X : TRACK_RECENTER_Y;
      }
    }
    if (out->flags != 0U) {
      return;
    }
    /* Both moves were clamped at the table edge: report from these windows. */
  }

  for (unsigned axis = 0; axis < 2U; axis++) {
    const uint16_t *a = used[axis];
    if (2U * (uint32_t)a[1] <= (uint32_t)a[0] + (uint32_t)a[2]) {
      out->flags |= TRACK_WEAK;
      return;
    }
  }

  int32_t x = fit_axis(state, 0U, x_used, peak[0]);
  int32_t y = fit_axis(state, 1U, y_used, peak[1]);

  if (x < 0) x = 0;
  if (x > 21200) x = 21200;
  if (y < 0) y = 0;
  if (y > 13200) y = 13200;

  out->x = x;
  out->y = y;
  out->flags |= TRACK_VALID;
}
