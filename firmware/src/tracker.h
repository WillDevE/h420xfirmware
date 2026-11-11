/*
 * Clean-room three-coil tracker.
 *
 * Same-frame log-domain fit; no temporal smoothing and no previous-frame
 * blend. The only state is the current center, ownership hysteresis, and the
 * optional slot-gain correction.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef H420X_TRACKER_H
#define H420X_TRACKER_H

#include <stdint.h>

#define TRACK_VALID      0x01U
#define TRACK_WEAK       0x02U
#define TRACK_RECENTER_X 0x04U
#define TRACK_RECENTER_Y 0x08U

typedef struct {
  uint8_t x_center;
  uint8_t y_center;
  int32_t e[2];
  int32_t prev_pos[2];
  int32_t prev_delta[2];
  uint8_t history[2];
  uint8_t last_center[2];
} tracker_state;

typedef struct {
  int32_t x;
  int32_t y;
  uint8_t flags;
} tracker_output;

void tracker_init(tracker_state *state);
void tracker_forget(tracker_state *state);
void tracker_set_centers(tracker_state *state, uint8_t x_center,
                         uint8_t y_center);

/*
 * x_used/y_used are the three used amplitudes (left, middle, right) of the
 * windows read at state->x_center / state->y_center. Result flags:
 *   TRACK_VALID                  out->x/out->y hold this frame's position
 *                                (a deferred handoff may have moved a center
 *                                for the next frame);
 *   TRACK_WEAK                   no usable window: the pen is lost;
 *   TRACK_RECENTER_X/_Y          the pen is far past a coil boundary: the
 *                                flagged centers moved; re-read those axes
 *                                and call again (no state was updated).
 * tracker_forget() clears the motion history and keeps the learned
 * slot-gain correction.
 */
void tracker_update(tracker_state *state, const uint16_t x_used[3],
                    const uint16_t y_used[3], tracker_output *out);

#if defined(LOOP_MODES) && LOOP_MODES
/* Round 9 loop modes only: coil-ownership hysteresis 1/2^shift (2..8; 5 = 1/32, the default). */
void tracker_set_hyst_shift(uint8_t shift);

/* Round 24 loop modes only: enable/disable the online slot-gain learner. */
void tracker_set_slot_learn(uint8_t enable);

/* Round 25 loop modes only: 0 = log-domain fit, 1 = stock-style three-point
 * linear interpolation. */
void tracker_set_fit_mode(uint8_t mode);

#if defined(R60_MODES) && R60_MODES
/* Round 60 loop modes only: Y odd cubic correction weight of the log fit (Q16). */
void tracker_set_cubic_k_y(uint16_t k);
#endif

#if defined(R61_MODES) && R61_MODES
/* Round 61 loop modes only: scale correction of the fitted offset per axis
 * (Q16, positive shrinks), fixed (sx, sy) or learned at the coil handoffs
 * (slot 1..2, gain 1/2^shift, axes bit 0 X / bit 1 Y). tracker_r61_scale()
 * returns the scale in use for an axis. */
void tracker_set_r61(int16_t sx, int16_t sy, uint8_t slot, uint8_t shift, uint8_t axes);
int32_t tracker_r61_scale(uint8_t axis);
#endif
#endif

#endif /* H420X_TRACKER_H */
