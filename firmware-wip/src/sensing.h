/*
 * Clean-room H420X sensing front end.
 *
 * The mux selectors and frequency bins are hardware wiring data. The
 * excitation/ADC sequence is inferred from the measured stock timing; it is
 * deliberately table-driven so the first hardware session can calibrate the
 * mux order and pulse timing without rewriting the tracker.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef H420X_SENSING_H
#define H420X_SENSING_H

#include <stdint.h>

#define AXIS_X 0U
#define AXIS_Y 1U

#ifndef FRAME_AUX
#define FRAME_AUX 0
#endif

#define COIL_COUNT_X 19U
#define COIL_COUNT_Y 13U

/* Tracking center limits (see tracker.c). TRACK_EDGE_EXT (build.ps1 -EdgeExt)
 * widens them by one coil each side so the first and last coil strips can
 * be fitted; the window at center 2 reads coils 1..3 only. */
#ifndef TRACK_EDGE_EXT
#define TRACK_EDGE_EXT 0
#endif
#if TRACK_EDGE_EXT
#define X_CENTER_MIN 2U
#define X_CENTER_MAX 18U
#define Y_CENTER_MIN 2U
#define Y_CENTER_MAX 12U
#define WINDOW_CENTER_MIN 2U
#else
#define X_CENTER_MIN 3U
#define X_CENTER_MAX 17U
#define Y_CENTER_MIN 3U
#define Y_CENTER_MAX 11U
#define WINDOW_CENTER_MIN 3U
#endif

#define SENSING_DEFAULT_BIN 9U

/* Read variants (bit flags). Every normal read uses SENSING_VARIANT_DEFAULT;
 * the telemetry lab reads all four combinations.
 *   ANCHOR:    every deadline after the burst start is taken from the
 *              observed start (DWT read after the first wait), so each edge
 *              has the same offset on every read. Without it the deadlines
 *              chain from the requested start (0.5.1 behavior).
 *   PA15_FAST: PA15 output speed 50 MHz (OSPD0 = 11) and OSPD1 bit 15 clear,
 *              as stock. Without it: OSPD0 = 00 and OSPD1 bit 15 set (0.5.1). */
#define SENSING_VARIANT_ANCHOR    0x01U
#define SENSING_VARIANT_PA15_FAST 0x02U
/* CTL_SLOW (0.6.3 lab): the control pins PA2, PA3, PA5-PA7 and PB3-PB7 get
 * the stock speed (OSPD0 = 00, PB7 = 01) and the OSPD1 bits of PA3/PA5 are
 * cleared. Without it: speed 2 as 0.5.1. Only telemetry builds apply the pin
 * variants (SENSING_TELEMETRY); plain builds always run the default. */
#define SENSING_VARIANT_CTL_SLOW  0x04U
/* ADC_STOCK (0.6.5): the released image reprograms the ADC regular-sequence
 * registers before EVERY conversion (RSQ0 = 0 twice, RSQ2 = channel 4,
 * SAMPT1; plus CTL0 = SM before the dummy conversion), about 17 ADC writes per
 * read; the clean room programmed them once at init. With this variant the
 * same values are written before each conversion, in the idle time ahead of
 * its start (the conversion start instant does not move). Found by diffing
 * the emulated event timeline of one read (development/buildbin_re/
 * read_timeline.py), which the end-state register diff could not see.
 * ADDR_CLEAR (0.6.5): the released image clears the PA5-PA7 mux address bits
 * (PA_ADDR = 0) before writing the new address; the clean room writes the new
 * address directly. */
#define SENSING_VARIANT_ADC_STOCK  0x08U
#define SENSING_VARIANT_ADDR_CLEAR 0x10U
#define SENSING_VARIANT_MASK      0x1FU
/* Variants honoured by sensing_read_coil_variant(): all of them in telemetry
 * builds, only the default in plain builds (keeps the plain read path
 * constant-folded exactly as in 0.6.2). */
#if SENSING_TELEMETRY
#define SENSING_VARIANT_ALLOWED   SENSING_VARIANT_MASK
#else
#define SENSING_VARIANT_ALLOWED   0x00U
#endif
/* build.ps1 -AdcStock / -AddrClear turn the two 0.6.5 variants on for every
 * read; a plain build keeps 0 (0.6.1: 0.5.1 timing). */
#ifndef SENSING_ADC_STOCK
#define SENSING_ADC_STOCK 0
#endif
#ifndef SENSING_ADDR_CLEAR
#define SENSING_ADDR_CLEAR 0
#endif
/* 0.6.5e (round 4 lab result): build.ps1 -Pa15Fast turns PA15_FAST on for every
 * read, -PinsStock turns PA15_FAST and CTL_SLOW on (the lab's "stock pins" setup j6,
 * the least noisy of the 8 setups at both test positions). Plain builds keep 0. */
#ifndef SENSING_PA15_FAST
#define SENSING_PA15_FAST 0
#endif
#ifndef SENSING_PINS_STOCK
#define SENSING_PINS_STOCK 0
#endif
/* 0.6.5g (round 7): build.ps1 -Anchor turns the ANCHOR variant on for every read
 * (never tested end to end before). */
#ifndef SENSING_ANCHOR
#define SENSING_ANCHOR 0
#endif
#define SENSING_VARIANT_DEFAULT \
  ((SENSING_ANCHOR ? SENSING_VARIANT_ANCHOR : 0U) | \
   (SENSING_ADC_STOCK ? SENSING_VARIANT_ADC_STOCK : 0U) | \
   (SENSING_ADDR_CLEAR ? SENSING_VARIANT_ADDR_CLEAR : 0U) | \
   ((SENSING_PA15_FAST || SENSING_PINS_STOCK) ? SENSING_VARIANT_PA15_FAST : 0U) | \
   (SENSING_PINS_STOCK ? SENSING_VARIANT_CTL_SLOW : 0U))

typedef struct {
  uint32_t selector;
  uint8_t mask;
} mux_entry_t;

extern const mux_entry_t x_mux[COIL_COUNT_X];
extern const mux_entry_t y_mux[COIL_COUNT_Y];
extern const uint32_t resonance_hz[13];
extern const uint16_t excite_period_cyc[13];
extern const uint16_t axis_pitch[2];

typedef void (*sensing_service_fn)(void);

void sensing_init(void);

/* Called after each completed coil read, outside the per-read critical
 * section. The firmware uses this to keep TinyUSB serviced during full-coil
 * searches and startup calibration. */
void sensing_set_service_hook(sensing_service_fn fn);

/* rhythm1000 (profile 4) only. The prep hook runs inside the integration
 * wait of each window's discarded slot-0 read, with interrupts enabled.
 * sensing_frame_begin() masks interrupts and suppresses the per-read service
 * hook until sensing_frame_end(). */
void sensing_set_prep_hook(sensing_service_fn fn);
void sensing_frame_begin(void);
void sensing_frame_end(void);

/* -Modes / -LoopMode builds only (round 9 loop-mode diagnostic, rhythm900; the
 * frame calls above exist there too). sensing_set_loop(): cycles added to the
 * normal read gap, extra discarded reads per window (0..2) and SENSING_LOOP_*
 * flags for every later tracking read. SENSING_LOOP_DWELL: a read that has to
 * wait for its burst start waits with every mux deselected and selects the TX
 * mux only just before the burst. SENSING_LOOP_ADC_STOCK: the released
 * firmware's ADC register writes before every conversion
 * (SENSING_VARIANT_ADC_STOCK). SENSING_LOOP_PULSES28: 28 excitation pulses
 * instead of EXCITE_PULSES.
 * sensing_frame_gap(): the next burst starts `cyc` after the end of the latest
 * read. sensing_window_late() / sensing_window_first_late(): for the latest
 * window of `axis`, the max lateness of slots 1..3 and the lateness of its
 * first read, in cycles (how far the code before a burst ran past the
 * requested start; 0 = on time). */
#define SENSING_LOOP_DWELL     0x01U
#define SENSING_LOOP_ADC_STOCK 0x02U
#define SENSING_LOOP_PULSES28  0x04U
void sensing_set_loop(uint32_t extra_gap_cyc, uint8_t extra_prep, uint8_t flags);
#if AXIS_GAP_MODES
void sensing_set_loop_gap_y(uint16_t extra_cyc);
#endif
void sensing_frame_gap(uint32_t cyc);
uint32_t sensing_window_late(uint8_t axis);
uint32_t sensing_window_first_late(uint8_t axis);
/* Round 10 (-Modes / -LoopMode builds only). sensing_set_loop_analog():
 * excitation pulses per burst (8..40, anything else = EXCITE_PULSES) and the
 * delay from PA2 low to the first used conversion in cycles (1440..5000,
 * anything else = the profile default) for every later read. Call it after
 * sensing_set_loop(), which resets the pulse count.
 * sensing_frame_start_at(): the next burst starts at DWT time `t` (or as soon
 * as the code gets there, if that is later). */
void sensing_set_loop_analog(uint8_t pulses, uint32_t adc1_after_cyc);
void sensing_frame_start_at(uint32_t t);
/* Round 11/12 (-Modes / -LoopMode builds only). sensing_set_loop_read(): used
 * conversions per read (1 = the first conversion only, returned as is; anything
 * else = three, median), skip_prep_y / skip_prep_x (nonzero = that axis' window
 * has no slot-0 preparation read; its amps[0] is 0) for every later read.
 * sensing_last_integ_cyc(): PA2 low -> PA2 high of the latest read, cycles.
 * sensing_frame_first_late_cyc(): lateness of the first read of the latest
 * quiet frame (0 = it started on time), cycles.
 * sensing_in_mask(): for the latest quiet frame, bit k = the vendor EP1 IN
 * transfer was complete when read k started (k = 0..14), bit 15 = at
 * sensing_frame_end(). */
void sensing_set_loop_read(uint8_t nconv, uint8_t skip_prep_y, uint8_t skip_prep_x);
uint32_t sensing_last_integ_cyc(void);
uint32_t sensing_frame_first_late_cyc(void);
uint32_t sensing_in_mask(void);
/* Round 13 (-Modes / -LoopMode builds only). sensing_read_frame(): one whole
 * tracking frame (X window, then Y window, the same slots, coils and amps
 * layout as two sensing_read_window() calls) with every burst at an exact DWT
 * cadence: consecutive PA3 rises are one read period apart (pulses, bin
 * period, conversions, the normal read gap and the offset set by
 * sensing_set_cadence()), and the next frame's first read is scheduled one
 * read period after this frame's last (round 14: no extra). Returns 0, or -1
 * for bad centers / bin or an ADC timeout. sensing_set_cadence(): read-period
 * offset in sixtieths of the excitation period (0..59); absolute = 1 makes
 * (read period mod excitation period) equal that offset in every bin; forgets
 * the schedule.
 * sensing_frame_max_late_cyc(): max lateness of reads 2.. of the latest cadence
 * frame, cycles (sensing_frame_first_late_cyc() has the first read's). */
int sensing_read_frame(uint8_t x_center, uint8_t y_center, uint8_t bin,
                       uint16_t xa[4], uint16_t ya[4]);
void sensing_set_cadence(uint8_t phase_60ths, uint8_t absolute);
/* Round 19: excitation period (cycles) of cadence frames; 0 = the bin's period. */
void sensing_set_cadence_period(uint8_t period_cyc);
/* Round 20: PA3 fall -> PA2 low (integration start) in cycles for every later
 * read (468..1200, anything else = 756, the profile default); the dummy
 * conversion keeps its distance (457 cycles) to PA2 low. */
void sensing_set_loop_pa2(uint32_t pa2_low_cyc);
/* Round 35: ADC sample-time code for the used conversions (0..7). */
void sensing_set_sample_time(uint8_t code);
/* Round 42: force the first read of every cadence frame to start at a DWT value
 * with (DWT mod 12) == align (0..11, the ADC clock is 12 CPU cycles); anything
 * else = no alignment. Forgets the cadence schedule. Only built with PHASE_MODES. */
void sensing_set_start_align(uint8_t align);
/* Round 44: read order 0 = X first, 1 = Y first, 2/3 = the same reversed;
 * frame gap = extra idle cycles before the first burst of every frame
 * (0..4000; it forgets the cadence schedule). Only built with ORDER_MODES or
 * SETTLE_MODES. */
void sensing_set_read_order(uint8_t order);
void sensing_set_frame_gap(uint16_t gap_cyc);
/* Round 21: idle cycles added to the cadence read period (0..4000, anything
 * else = 0), before the absolute-phase rounding; forgets the schedule. */
void sensing_set_cadence_pad(uint32_t pad_cyc);
/* Round 22: frame period floor in cycles (36000..108000, anything else = 0 =
 * off): the next cadence frame's first read starts no earlier than this long
 * after this frame's first read. */
void sensing_set_cadence_frame(uint32_t frame_cyc);
uint32_t sensing_frame_max_late_cyc(void);

#if FRAME_AUX
/* Measured first-read -> first-read cadence frame period, microseconds
 * (-FrameAux diagnostic; 0 until the second frame). */
uint32_t sensing_frame_period_us(void);
#endif

/* Nonzero when ADC calibration completed without timing out. */
uint8_t sensing_adc_ok(void);

/*
 * Bring-up probe using the same channel-4 path as normal tracking.
 * out[0] = PA2 high (reset), all muxes deselected
 * out[1] = PA2 low (integrate), all muxes deselected
 * out[2] = PA2 high (reset), X coil 10 selected
 * out[3] = PA2 low (integrate), X coil 10 selected
 * No excitation is applied in any phase.
 */
void sensing_adc_pa2_probe(uint16_t out[4]);
/* Telemetry builds only: one sampling window with no excitation (spectrum lab). */
uint16_t sensing_read_idle(uint8_t axis, uint8_t coil);
void sensing_led_hold(void);   /* telemetry builds: constant LED output (no 100 Hz PWM edges) */
/* Telemetry builds only (spectrum lab): sensing_read_coil_variant() with an explicit gap
 * (CPU cycles from the end of this read to the earliest start of the next one; the
 * normal gap is 387, capped at 21000). */
uint16_t sensing_read_coil_spaced(uint8_t axis, uint8_t receive_coil, uint8_t excite_center,
                                  uint8_t bin, uint8_t variant, uint32_t gap_cyc);

/* Frequency bin used for excitation, 1..12. */
void sensing_set_bin(uint8_t bin);
uint8_t sensing_bin(void);

/*
 * Read one coil.
 *   axis          AXIS_X or AXIS_Y
 *   receive_coil  1-based coil id on that axis
 *   excite_center 1-based center coil id on the other axis
 *   bin           1..12
 *
 * Returns the median of three ADC conversions, or 0 on ADC timeout.
 */
uint16_t sensing_read_coil(uint8_t axis, uint8_t receive_coil,
                           uint8_t excite_center, uint8_t bin);

/*
 * Same as sensing_read_coil(). When long_gap is nonzero the next burst starts
 * no earlier than 115.9 us after PA2 goes high (the long tail used by the
 * resonance sweeps) instead of the normal profile gap.
 */
uint16_t sensing_read_coil_gap(uint8_t axis, uint8_t receive_coil,
                               uint8_t excite_center, uint8_t bin,
                               uint8_t long_gap);

/* Same as sensing_read_coil() with an explicit SENSING_VARIANT_* set
 * (telemetry lab). Returns 0 on invalid arguments or ADC timeout. */
uint16_t sensing_read_coil_variant(uint8_t axis, uint8_t receive_coil,
                                   uint8_t excite_center, uint8_t bin,
                                   uint8_t variant);

/* Status LED (TIMER1 CH1 on PA1): stock compare 59 while a pen is tracked,
 * 899 otherwise. */
void sensing_led_tracking(uint8_t tracking);

/* Hold off the next excitation burst for at least `us` microseconds from now
 * (capped at 300 us). Used for the quiet time after a detuned probe. */
void sensing_hold_off_us(uint32_t us);

/*
 * Self-excited read: excite and receive on the same coil (used for the
 * full-axis acquisition search). Returns the median of three conversions.
 */
uint16_t sensing_read_self(uint8_t axis, uint8_t coil, uint8_t bin);

/*
 * Read the four-read tracking window for one axis:
 *   slot 0 = center-2, discarded preparation read
 *   slot 1 = center-1, left
 *   slot 2 = center,   middle
 *   slot 3 = center+1, right
 * Returns 0 on success, -1 if the window crosses the coil table.
 * amps[1..3] are the used amplitudes; amps[0] is the preparation read.
 */
int sensing_read_window(uint8_t axis, uint8_t center, uint8_t excite_center,
                        uint8_t bin, uint16_t amps[4]);

/*
 * Self-excited search over every coil of one axis at one bin. Returns the
 * 1-based coil with the largest response (0 if every read was 0) and stores
 * that response in *peak_amplitude and the median of all the axis reads
 * (the floor) in *floor_amplitude, each when not NULL.
 */
uint8_t sensing_self_search(uint8_t axis, uint8_t bin, uint16_t *peak_amplitude,
                            uint16_t *floor_amplitude);

/*
 * Resonance sweep after acquisition: excite X coil x_coil, receive Y coil
 * y_coil, once per bin 1..12. Selects and returns the bin with the largest
 * response (the current bin is kept if every read is 0) and stores that
 * response in *peak_amplitude when it is not NULL.
 */
uint8_t sensing_sweep_bin(uint8_t x_coil, uint8_t y_coil, uint16_t *peak_amplitude);

/*
 * One-step resonance maintenance: compare the current bin with its immediate
 * neighbours at the current X coil. Returns the best bin. Intended to run
 * every 64..128 frames, not every frame.
 */
uint8_t sensing_maintain_bin(uint8_t x_center, uint8_t y_center, uint8_t bin);

/* Telemetry builds only: the three ADC conversions of the latest read and
 * the burst-start wait overshoot in cycles (requested start to the DWT read
 * after the wait), clamped to 255. */
void sensing_last_read_info(uint16_t samples[3], uint8_t *overshoot);

#endif /* H420X_SENSING_H */
