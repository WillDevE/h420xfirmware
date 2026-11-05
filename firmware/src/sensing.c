/*
 * Clean-room H420X sensing front end.
 *
 * Every analog interval is an absolute DWT deadline measured from the end of
 * the excitation burst (PA3 fall). The values reproduce the runtime sequence
 * of the hardware-validated rhythm750/rhythm900 firmware:
 *   - PA15 output-low for 168 cycles, then PA3 (driver enable) high
 *   - first PA15 rise at PA3 rise + T/2 + 9 cycles; 29 pulses of period T
 *     (bin table), 50% duty; PA3 fall at last PA15 fall + T + 167 cycles
 *   - receive coil selected, 55.5-cycle dummy conversion at +4.15 us
 *   - PA2 low (integrate) at +10.5 us
 *   - three 13.5-cycle conversions 3.58 us apart, the first at PA2 low +
 *     53.33 us (rhythm750, pen500) or + 35.33 us (rhythm900); median of three
 *   - PA2 high 3.63 us after the last conversion start, all muxes
 *     deselected; the next burst starts no earlier than 12.37 us (rhythm750, pen500)
 *     or 5.38 us (rhythm900) later
 * 0.6.1: pin setup and read timing are those of 0.5.1, the only build that
 * tracked on hardware (0.6.0's stock pin speeds read no pen at all). The
 * re-anchored timing exists as lab variant bit 0 but is off by default.
 * Acquisition uses self-excited reads (transmit and receive on the same
 * coil) over the whole axis.
 *
 * rhythm1000 (profile 4) uses the rhythm900 timing with interrupts masked
 * across each tracked frame; see sensing_frame_begin().
 *
 * The mux selector values are hardware wiring data; sensing_read_raw() is
 * the single place that writes the receive selection.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "sensing.h"
#include "gd32f350_regs.h"

#ifndef H420X_PROFILE
#define H420X_PROFILE 2
#endif

#ifndef SENSING_TELEMETRY
#define SENSING_TELEMETRY 0
#endif

/* Timing in CPU cycles at 72 MHz (DWT). */
#define PA15_SETTLE_CYC          168U  /* PA15 output-low before PA3 rises */
#define EXCITE_LEAD_EXTRA_CYC    9U    /* first PA15 rise = PA3 rise + T/2 + 9 */
#define EXCITE_TRAIL_EXTRA_CYC   167U  /* PA3 fall = last PA15 fall + T + 167 */
#define DUMMY_AFTER_BURST_CYC    299U  /* 4.15 us */
#define PA2_LOW_AFTER_BURST_CYC  756U  /* 10.5 us */
#define ADC_STEP_CYC             258U  /* 3.58 us between used conversions */
#define PA2_HIGH_AFTER_LAST_CYC  261U  /* 3.63 us after the last start */
#if H420X_PROFILE == 1 || H420X_PROFILE == 3
#define ADC1_AFTER_PA2_LOW_CYC   3840U /* 53.33 us (rhythm750) */
#define READ_GAP_CYC             891U  /* 12.37 us PA2 high -> next PA3 rise */
#else
#define ADC1_AFTER_PA2_LOW_CYC   2544U /* 35.33 us (rhythm900) */
#define READ_GAP_CYC             387U  /* 5.38 us */
#endif
#ifdef READ_GAP_CYC_OVERRIDE
/* build.ps1 -ReadGap <cycles> (round 7): the spectrum lab found that reads separated by a slightly
 * longer gap are quiet, so the normal read-to-read gap is tunable per build. */
#undef READ_GAP_CYC
#define READ_GAP_CYC             READ_GAP_CYC_OVERRIDE
#endif
#define LONG_GAP_CYC             8345U  /* 115.9 us PA2 high -> next PA3 rise after a long-tail read */
#define MAX_GAP_CYC              21600U /* 300 us: longest hold-off (post-probe quiet) */

#ifndef EXCITE_PULSES
#define EXCITE_PULSES 29U  /* build.ps1 -Pulses overrides (rhythm1000 only) */
#endif

static uint8_t current_bin = SENSING_DEFAULT_BIN;
static uint8_t adc_ok;
static sensing_service_fn service_hook;
static uint32_t next_burst_at;
static uint8_t next_burst_valid;
#if SENSING_TELEMETRY
static uint16_t last_samples[3];   /* conversions of the latest read */
static uint8_t last_overshoot;     /* DWT cycles from the requested burst start
                                    * to the read after its wait, clamped 255 */
#endif

void sensing_set_service_hook(sensing_service_fn fn) {
  service_hook = fn;
}

uint8_t sensing_adc_ok(void) {
  return adc_ok;
}

static inline uint32_t irq_save(void) {
  uint32_t mask;
  __asm volatile ("mrs %0, primask\n cpsid i" : "=r"(mask) :: "memory");
  return mask;
}

static inline void irq_restore(uint32_t mask) {
  __asm volatile ("msr primask, %0" :: "r"(mask) : "memory");
}

#if H420X_PROFILE == 4
/* rhythm1000: the slot-0 preparation read of each window is discarded, so USB
 * work can run inside its integration wait without touching a used sample.
 * Between sensing_frame_begin() and sensing_frame_end() interrupts stay
 * masked and the per-read service hook does not run, so every inter-read gap
 * is exactly READ_GAP_CYC. */
#define PREP_GUARD_CYC 2000U  /* run the prep hook only if this much of the wait remains */
static sensing_service_fn prep_hook;
static uint8_t prep_pending;  /* the next sample_window() belongs to a slot-0 read */
static uint8_t frame_active;  /* between sensing_frame_begin() and sensing_frame_end() */

void sensing_set_prep_hook(sensing_service_fn fn) {
  prep_hook = fn;
}

void sensing_frame_begin(void) {
  __asm volatile ("cpsid i" ::: "memory");
  frame_active = 1U;
}

void sensing_frame_end(void) {
  frame_active = 0U;
  __asm volatile ("cpsie i" ::: "memory");
}
#endif

#ifndef LOOP_MODES
#define LOOP_MODES 0
#endif
#ifndef FRAME_AUX
#define FRAME_AUX 0
#endif
#if LOOP_MODES
/* -Modes / -LoopMode (round 8 diagnostic, rhythm900): loop parameters set by
 * main.c at run time, and how late each read started. A read is late when the
 * code before its burst (service hook, prologue) ran past the requested burst
 * start, i.e. the gap before it was longer than requested. */
static uint32_t loop_extra_gap;              /* cycles added to READ_GAP_CYC for normal reads */
static uint8_t loop_extra_prep;              /* extra discarded reads at the start of each window */
static uint32_t loop_last_late;              /* lateness of the latest read, cycles */
static uint32_t loop_last_pa2_high;          /* DWT at the end (PA2 rise) of the latest read */
static uint32_t loop_window_late[2];         /* per axis: max lateness of slots 1..3, latest window */
static uint32_t loop_window_first_late[2];   /* per axis: lateness of the first read of that window */
static uint8_t loop_dwell;                   /* SENSING_LOOP_DWELL is on */
static uint8_t loop_variant;                 /* SENSING_VARIANT_ADC_STOCK when SENSING_LOOP_ADC_STOCK is on, else 0 */
static uint8_t loop_pulses = EXCITE_PULSES;  /* excitation pulses per burst */
static uint32_t loop_adc1 = ADC1_AFTER_PA2_LOW_CYC;  /* PA2 low -> first used conversion, cycles */
static uint32_t loop_pa2_low = PA2_LOW_AFTER_BURST_CYC;  /* round 20: PA3 fall -> PA2 low, cycles */
#define LOOP_DWELL_LEAD_CYC 288U             /* DWELL: TX mux select about 4 us before the burst start */
static uint8_t loop_nconv = 3U;              /* used conversions per read: 3 (median of three) or 1 (the first only) */
static uint8_t loop_sampt = ADC_SAMPT_13_5;  /* round 35: used-conversion ADC sample-time code */
static uint8_t loop_skip_prep_y;             /* 1 = the Y window has no slot-0 preparation read */
static uint8_t loop_skip_prep_x;             /* 1 = the X window has no slot-0 preparation read */
static uint32_t loop_frame_first_late;       /* lateness of the first read of the latest quiet frame, cycles */
#if FRAME_AUX
static uint32_t loop_prev_frame_first;       /* actual first read of the previous cadence frame, DWT */
static uint8_t loop_prev_frame_valid;        /* loop_prev_frame_first is set */
static uint32_t loop_frame_period_cyc;       /* measured first-read -> first-read cadence frame period */
#endif
static uint32_t loop_last_integ;             /* PA2 low -> PA2 high of the latest read, cycles */
static uint32_t loop_in_mask;                /* bit k: EP1 IN transfer complete at the start of read k of this frame; bit 15: at frame end */
static uint32_t loop_read_count;             /* reads started since sensing_frame_begin() */
static uint8_t loop_cad_phase;               /* read-period offset, sixtieths of the excitation period (0..59) */
static uint8_t loop_cad_abs;                 /* round 15: 1 = read period mod excitation period = phase/60 of the period */
static uint8_t loop_cad_period;             /* round 19: cadence-frame excitation period in cycles, 0 = the bin's */
static uint32_t loop_cad_pad;                /* round 21: idle cycles added to the cadence read period */
static uint32_t loop_cad_frame;              /* round 22: frame period floor in cycles, 0 = off */
static uint32_t loop_cad_next;               /* round 13: scheduled PA3 rise of the next cadence frame's first read (DWT) */
static uint8_t loop_cad_next_valid;          /* loop_cad_next is set */
static uint32_t loop_frame_max_late;         /* round 13: max lateness of reads 2.. of the latest cadence frame, cycles */
/* Round 13 cadence budget (hardware, round 11 aux: PA2 low -> PA2 high 39 us with one
 * conversion, 47 us with three, i.e. a conversion takes about 255 cycles plus code). */
#ifndef CAD_CONV_BUDGET_CYC
#define CAD_CONV_BUDGET_CYC  440U   /* first used conversion start -> PA2 high, worst case (measured 264..335) */
#endif
#define CAD_CONV_STEP_CYC    330U   /* each further used conversion (end-of-conversion bound) */
#define CAD_FRAME_EXTRA_CYC  0U     /* round 14: the next frame's first read is scheduled one read
                                     * period after this frame's last, i.e. it starts as soon as
                                     * the work between frames (about 55 us) is done; that read
                                     * is the X preparation read, so its lateness does not move
                                     * the used reads, which follow it on the cadence */
#define CAD_ABS_SLACK_CYC    50U    /* round 15 absolute cadence: the read period is at least the
                                     * offset-0 period minus this (the code between reads has
                                     * about 200 cycles of slack) */
#if H420X_PROFILE != 4
static uint8_t frame_active;  /* between sensing_frame_begin() and sensing_frame_end() */

void sensing_frame_begin(void) {
  __asm volatile ("cpsid i" ::: "memory");
  frame_active = 1U;
  loop_in_mask = 0U;
  loop_read_count = 0U;
  loop_frame_first_late = 0U;
}

void sensing_frame_end(void) {
  loop_in_mask |= (USBFS_DIEPINT1 & USBFS_DIEPINT_XFRC) << 15;
  frame_active = 0U;
  __asm volatile ("cpsie i" ::: "memory");
}
#endif

void sensing_set_loop(uint32_t extra_gap_cyc, uint8_t extra_prep, uint8_t flags) {
  loop_extra_gap = (extra_gap_cyc > 14400U) ? 14400U : extra_gap_cyc;
  loop_extra_prep = (extra_prep > 2U) ? 2U : extra_prep;
  loop_dwell = ((flags & SENSING_LOOP_DWELL) != 0U) ? 1U : 0U;
  loop_variant = ((flags & SENSING_LOOP_ADC_STOCK) != 0U) ? SENSING_VARIANT_ADC_STOCK : 0U;
  loop_pulses = ((flags & SENSING_LOOP_PULSES28) != 0U) ? 28U : EXCITE_PULSES;
}

/* The next burst starts `cyc` after the end of the latest read (or as soon as
 * the code before it gets there, if that is later). */
void sensing_frame_gap(uint32_t cyc) {
  next_burst_at = loop_last_pa2_high + cyc;
  next_burst_valid = 1U;
}

void sensing_set_loop_analog(uint8_t pulses, uint32_t adc1_after_cyc) {
  loop_pulses = (pulses >= 8U && pulses <= 40U) ? pulses : (uint8_t)EXCITE_PULSES;
  loop_adc1 = (adc1_after_cyc >= 1440U && adc1_after_cyc <= 5000U) ? adc1_after_cyc
                                                                   : ADC1_AFTER_PA2_LOW_CYC;
}

#if LOOP_MODES
void sensing_set_sample_time(uint8_t code) {
  loop_sampt = (code <= ADC_SAMPT_239_5) ? code : ADC_SAMPT_13_5;
}
#endif

/* Round 20: PA3 fall -> PA2 low in cycles (468..1200, anything else =
 * PA2_LOW_AFTER_BURST_CYC); sample_window() moves the dummy conversion with it. */
void sensing_set_loop_pa2(uint32_t pa2_low_cyc) {
  loop_pa2_low = (pa2_low_cyc >= 468U && pa2_low_cyc <= 1200U) ? pa2_low_cyc
                                                                : PA2_LOW_AFTER_BURST_CYC;
}

/* SOF modes: the next burst starts at `t` (DWT). */
void sensing_frame_start_at(uint32_t t) {
  next_burst_at = t;
  next_burst_valid = 1U;
}

/* Round 11/12: conversions per read (1 = only the first, no median; anything
 * else = 3) and whether the Y / X window skips its slot-0 preparation read. */
void sensing_set_loop_read(uint8_t nconv, uint8_t skip_prep_y, uint8_t skip_prep_x) {
  loop_nconv = (nconv == 1U) ? 1U : 3U;
  loop_skip_prep_y = (skip_prep_y != 0U) ? 1U : 0U;
  loop_skip_prep_x = (skip_prep_x != 0U) ? 1U : 0U;
}

uint32_t sensing_last_integ_cyc(void) {
  return loop_last_integ;
}

/* Round 12: lateness of the first read of the latest quiet frame, cycles
 * (how far the work between frames ran past the requested frame start). */
uint32_t sensing_frame_first_late_cyc(void) {
  return loop_frame_first_late;
}

/* Read-period offset of cadence frames in sixtieths of the excitation period
 * (0..59, anything else = 0; round 13's fifths k are 12 k). absolute = 0: the
 * offset is added to the read period (rounds 13-14); absolute = 1 (round 15):
 * the read period is the shortest one, not below the offset-0 period minus
 * CAD_ABS_SLACK_CYC, whose remainder modulo the excitation period is the
 * offset, so every bin gets the same burst-to-burst phase. Forgets the cadence
 * schedule, so the next cadence frame starts as soon as possible. */
void sensing_set_cadence(uint8_t phase_60ths, uint8_t absolute) {
  loop_cad_phase = (phase_60ths <= 59U) ? phase_60ths : 0U;
  loop_cad_abs = (absolute != 0U) ? 1U : 0U;
  loop_cad_next_valid = 0U;
}

/* Round 19: excitation period of sensing_read_frame() in cycles (120..160);
 * anything else = 0 = the bin's period from excite_period_cyc[]. Forgets the
 * cadence schedule like sensing_set_cadence(). */
void sensing_set_cadence_period(uint8_t period_cyc) {
  loop_cad_period = (period_cyc >= 120U && period_cyc <= 160U) ? period_cyc : 0U;
  loop_cad_next_valid = 0U;
}

/* Round 21: idle cycles added to the cadence read period (0..4000, anything
 * else = 0). cadence_period() adds them before the absolute-phase rounding,
 * so the read waits that much longer (muxes deselected) before the next burst.
 * Forgets the cadence schedule like sensing_set_cadence(). */
void sensing_set_cadence_pad(uint32_t pad_cyc) {
  loop_cad_pad = (pad_cyc <= 4000U) ? pad_cyc : 0U;
  loop_cad_next_valid = 0U;
}

/* Round 22: frame period floor in cycles (36000..108000, anything else = 0 =
 * off). sensing_read_frame() schedules the next frame's first read no earlier
 * than this long after the current frame's first read, so frames that finish
 * early still start at a steady period. Forgets the cadence schedule. */
void sensing_set_cadence_frame(uint32_t frame_cyc) {
  loop_cad_frame = (frame_cyc >= 36000U && frame_cyc <= 108000U) ? frame_cyc : 0U;
  loop_cad_next_valid = 0U;
}

/* Round 13: max lateness of the reads after the first of the latest cadence
 * frame, cycles (0 = every one started on its schedule). */
uint32_t sensing_frame_max_late_cyc(void) {
  return loop_frame_max_late;
}

#if FRAME_AUX
uint32_t sensing_frame_period_us(void) {
  return loop_frame_period_cyc / (CPU_HZ / 1000000UL);
}
#endif

uint32_t sensing_in_mask(void) {
  return loop_in_mask;
}

uint32_t sensing_window_late(uint8_t axis) {
  return (axis <= AXIS_Y) ? loop_window_late[axis] : 0U;
}

uint32_t sensing_window_first_late(uint8_t axis) {
  return (axis <= AXIS_Y) ? loop_window_first_late[axis] : 0U;
}
#endif

static inline void wait_until(uint32_t deadline) {
  while ((int32_t)(DWT_CYCCNT - deadline) < 0) {
  }
}

/* 0.6.5 variants (see sensing.h). Code for them is only emitted in telemetry
 * builds (all variants selectable at run time for the lab) or when a build
 * switch turns one on, so a plain build is unchanged. */
#if SENSING_TELEMETRY || SENSING_ADC_STOCK || SENSING_ADDR_CLEAR || LOOP_MODES
#define SENSING_VARIANT_CODE 1
/* sample_window() is not inlined: in plain builds it keeps its two-argument
 * form so the plain image stays byte-identical to 0.6.3. */
#define SW_PARAM , uint8_t variant
#define SW_ARG(v) , (v)
/* Store `value` to the register at `reg_addr` when `mask` is all ones, to a
 * private sink word when it is 0. Branch-free (emulator ITSTATE rule) and the
 * same cycles either way. */
static volatile uint32_t variant_sink;
static inline void store_if(uint32_t reg_addr, uint32_t mask, uint32_t value) {
  const uint32_t sink = (uint32_t)&variant_sink;
  *(volatile uint32_t *)(sink ^ ((sink ^ reg_addr) & mask)) = value;
}
static inline uint32_t variant_mask(uint8_t variant, uint8_t bit) {
  return 0U - (uint32_t)((variant & bit) != 0U);
}
#else
#define SENSING_VARIANT_CODE 0
#define SW_PARAM
#define SW_ARG(v)
#endif

/* The selector high half is the GPIOB output-data AND mask. The separate
 * gpio_mask field is the PA5..PA7 mux address, already in bit position. The
 * selector low half is the 1-based coil id used only as a table index. */
static inline void mux_select(const mux_entry_t *entry SW_PARAM) {
#if SENSING_VARIANT_CODE
  store_if((uint32_t)&GPIOA_OCTL, variant_mask(variant, SENSING_VARIANT_ADDR_CLEAR),
           GPIOA_OCTL & ~0xE0U);
#endif
  GPIOB_OCTL &= (entry->selector >> 16);
  GPIOA_OCTL = (GPIOA_OCTL & ~0xE0U) | (entry->mask & 0xE0U);
}

/* The stock receive path updates the PA address before the GPIOB chip mask. */
static inline void mux_select_receive(const mux_entry_t *entry SW_PARAM) {
#if SENSING_VARIANT_CODE
  store_if((uint32_t)&GPIOA_OCTL, variant_mask(variant, SENSING_VARIANT_ADDR_CLEAR),
           GPIOA_OCTL & ~0xE0U);
#endif
  GPIOA_OCTL = (GPIOA_OCTL & ~0xE0U) | (entry->mask & 0xE0U);
  GPIOB_OCTL &= (entry->selector >> 16);
}

static inline const mux_entry_t *mux_table(uint8_t axis) {
  return (axis == AXIS_X) ? x_mux : y_mux;
}

static inline uint8_t mux_count(uint8_t axis) {
  return (axis == AXIS_X) ? COIL_COUNT_X : COIL_COUNT_Y;
}

#ifndef ADC_NO_CALIBRATE
#define ADC_NO_CALIBRATE 0
#endif

static void adc_init(void) {
  adc_ok = 0U;
  RCU_APB2EN |= RCU_APB2EN_ADC;

  /* ADCPSC[1:0] = 10, ADCPSC[2] = 0, ADCSEL = 1: the stock image's register
   * values. On GD32 semantics this is APB2 / 6 = 12 MHz (APB2 undivided);
   * the HV901 ADC clock is not proven to follow GD32 RCU semantics. ADCPSC2
   * must stay clear: with ADCPSC=10 it selects AHB/7 instead. */
  RCU_CFG0 = (RCU_CFG0 & ~RCU_CFG0_ADCPSC_MASK) | (2UL << 14);
  RCU_CFG2 = (RCU_CFG2 & ~(RCU_CFG2_ADCPSC2 | RCU_CFG2_ADCSEL)) |
             RCU_CFG2_ADCSEL;

  gpio_mode(GPIOA_BASE, 4U, GPIO_MODE_ANALOG);

  ADC_CTL1 = ADC_CTL1_ADCON;
  delay_us(3U);                    /* >= 14 ADC clocks down to 6 MHz */

#if !ADC_NO_CALIBRATE
  /* Bounded calibration. A wrong ADC clock or a missing peripheral clock
   * must not hold the application before USB service starts. */
  uint32_t timeout = 200000U;
  ADC_CTL1 |= ADC_CTL1_RSTCLB;
  while ((ADC_CTL1 & ADC_CTL1_RSTCLB) != 0U) {
    if (--timeout == 0U) {
      return;
    }
  }
  timeout = 200000U;
  ADC_CTL1 |= ADC_CTL1_CLB;
  while ((ADC_CTL1 & ADC_CTL1_CLB) != 0U) {
    if (--timeout == 0U) {
      return;
    }
  }
#endif
  adc_ok = 1U;

  ADC_CTL0 = ADC_CTL0_SM;          /* stock scan enable, no interrupt */
  ADC_CTL1 = (ADC_CTL1 & ~(ADC_CTL1_CTN | ADC_CTL1_DMA |
                           ADC_CTL1_ETSRC_MASK)) |
             ADC_CTL1_ETERC | ADC_CTL1_ETSRC_SOFTWARE;
  ADC_RSQ0 = 0U;                   /* RL = 0: one conversion */
  ADC_RSQ1 = 0U;
  ADC_RSQ2 = ADC_PIN_CHANNEL;      /* first (only) conversion = channel 4 */
  /* GD32F3x0 maps channels 0..9 to SAMPT1 (unlike STM32). */
  ADC_SAMPT1 = (ADC_SAMPT1 & ~(7UL << 12)) |
               (ADC_SAMPT_55_5 << 12); /* channel 4 dummy sample time */
}

static uint16_t adc_sample(uint32_t sample_code) {
  ADC_SAMPT1 = (ADC_SAMPT1 & ~(7UL << 12)) | (sample_code << 12);
  ADC_STAT = 0U;                   /* clear EOC and stale flags */
  ADC_CTL1 |= ADC_CTL1_SWRCST;

  uint32_t timeout = 200000U;
  while ((ADC_STAT & ADC_STAT_EOC) == 0U) {
    if (--timeout == 0U) {
      return 0U;
    }
  }
  /* GD32F3x0 errata: allow two ADC clocks after EOC before reading RDATA
   * when the ADC clock is much slower than PCLK (here 12 MHz vs 72 MHz). */
  delay_cycles_exact(12U);
  uint16_t value = (uint16_t)(ADC_RDATA & 0x0FFFU);
  ADC_STAT = 0U;
  return value;
}

static uint16_t median3(uint16_t a, uint16_t b, uint16_t c) {
  if (a > b) { uint16_t t = a; a = b; b = t; }
  if (b > c) { uint16_t t = b; b = c; c = t; }
  if (a > b) { uint16_t t = a; a = b; b = t; }
  return b;
}

/* Stock primes the ADC with a 55.5-cycle conversion before releasing PA2.
 * The result is discarded; it settles the sample-and-hold while the
 * integrator is still held in reset. */
static void adc_prime_dummy(void) {
  ADC_SAMPT1 = (ADC_SAMPT1 & ~(7UL << 12)) | (ADC_SAMPT_55_5 << 12);
  ADC_STAT = 0U;
  ADC_CTL1 |= ADC_CTL1_SWRCST;
}

static uint16_t adc_reset_read(void) {
  GPIOA_BOP = (1UL << 2);          /* PA2 high: reset */
  delay_us(100U);
  return median3(adc_sample(ADC_SAMPT_13_5), adc_sample(ADC_SAMPT_13_5),
                 adc_sample(ADC_SAMPT_13_5)); /* reset node settles to 0 */
}

/* Dummy conversion, integration window and three used conversions, scheduled
 * from t_end (PA3 fall of the burst). Leaves PA2 high and all muxes
 * deselected, and sets the earliest start of the next burst to gap_cyc after
 * PA2 high. */
#if SENSING_TELEMETRY || SENSING_ADC_STOCK || LOOP_MODES
/* ADC_STOCK variant: the registers the released image writes before every
 * conversion (RSQ0 twice, RSQ2). Values equal the init values, so on a GD32
 * this changes nothing; whether the HV901 ADC cares is the question. */
#define ADC_STOCK_LEAD_CYC 70U   /* start the writes this long before the conversion start */
static inline void adc_stock_regs(uint32_t on) {
  store_if((uint32_t)&ADC_RSQ0, on, 0U);
  store_if((uint32_t)&ADC_RSQ0, on, 0U);
  store_if((uint32_t)&ADC_RSQ2, on, ADC_PIN_CHANNEL);
}
#endif

/* Round 20: -Modes / -LoopMode builds set PA2 low per mode and move the dummy
 * conversion with it; plain builds use the constants (byte-identical). */
#if LOOP_MODES
#define SW_PA2_LOW_CYC  loop_pa2_low
#define SW_DUMMY_CYC    (loop_pa2_low - (PA2_LOW_AFTER_BURST_CYC - DUMMY_AFTER_BURST_CYC))
#else
#define SW_PA2_LOW_CYC  PA2_LOW_AFTER_BURST_CYC
#define SW_DUMMY_CYC    DUMMY_AFTER_BURST_CYC
#endif

static uint16_t sample_window(uint32_t t_end, uint32_t gap_cyc SW_PARAM) {
#if SENSING_TELEMETRY || SENSING_ADC_STOCK || LOOP_MODES
  const uint32_t stock = variant_mask(variant, SENSING_VARIANT_ADC_STOCK);
  wait_until(t_end + SW_DUMMY_CYC - ADC_STOCK_LEAD_CYC);
  adc_stock_regs(stock);
  wait_until(t_end + SW_DUMMY_CYC);
  store_if((uint32_t)&ADC_CTL0, stock, ADC_CTL0_SM);
#else
  wait_until(t_end + SW_DUMMY_CYC);
#endif
  adc_prime_dummy();
  const uint32_t t_lo = t_end + SW_PA2_LOW_CYC;
  wait_until(t_lo);
  GPIOA_BC = (1UL << 2);           /* PA2 low: integrate */
#if H420X_PROFILE == 4
  if (prep_pending) {
    prep_pending = 0U;
    if (prep_hook != 0 &&
        (int32_t)(t_lo + ADC1_AFTER_PA2_LOW_CYC - DWT_CYCCNT) >= (int32_t)PREP_GUARD_CYC) {
      __asm volatile ("cpsie i" ::: "memory");
      prep_hook();
      __asm volatile ("cpsid i" ::: "memory");
    }
  }
#endif
#if LOOP_MODES
  uint16_t s[3] = { 0U, 0U, 0U };
  const unsigned nconv = loop_nconv;
  uint32_t at = t_lo + loop_adc1;
  for (unsigned i = 0; i < nconv; i++) {
#else
  uint16_t s[3];
  uint32_t at = t_lo + ADC1_AFTER_PA2_LOW_CYC;
  for (unsigned i = 0; i < 3U; i++) {
#endif
#if SENSING_TELEMETRY || SENSING_ADC_STOCK || LOOP_MODES
    wait_until(at - ADC_STOCK_LEAD_CYC);
    adc_stock_regs(stock);
#endif
    wait_until(at);
#if LOOP_MODES
    s[i] = adc_sample(loop_sampt);
#else
    s[i] = adc_sample(ADC_SAMPT_13_5);
#endif
    at += ADC_STEP_CYC;
  }
  wait_until(at - ADC_STEP_CYC + PA2_HIGH_AFTER_LAST_CYC);
  GPIOA_BOP = (1UL << 2);          /* PA2 high: reset */
  GPIOB_OCTL |= 0x78U;             /* deselect all muxes */
#if LOOP_MODES
  loop_last_pa2_high = DWT_CYCCNT;
  loop_last_integ = loop_last_pa2_high - t_lo;
  next_burst_at = loop_last_pa2_high + gap_cyc;
#else
  next_burst_at = DWT_CYCCNT + gap_cyc;
#endif
  next_burst_valid = 1U;
#if SENSING_TELEMETRY
  last_samples[0] = s[0];
  last_samples[1] = s[1];
  last_samples[2] = s[2];
#endif
#if LOOP_MODES
  if (nconv == 1U) {
    return s[0];
  }
#endif
  return median3(s[0], s[1], s[2]);
}

/* Phases 1 and 3 use the normal sampling window without excitation. */
void sensing_adc_pa2_probe(uint16_t out[4]) {
  /* Call this only after at least one normal scan has primed the ADC. */
  GPIOB_OCTL |= 0x78U;             /* all muxes deselected */
  out[0] = adc_reset_read();

  {
    uint32_t mask = irq_save();
    out[1] = sample_window(DWT_CYCCNT, READ_GAP_CYC SW_ARG(SENSING_VARIANT_DEFAULT));
    irq_restore(mask);
  }

  GPIOB_OCTL |= 0x78U;
  mux_select_receive(&x_mux[9] SW_ARG(SENSING_VARIANT_DEFAULT));   /* X coil 10, no excitation */
  delay_us(20U);
  out[2] = adc_reset_read();

  {
    uint32_t mask = irq_save();
    out[3] = sample_window(DWT_CYCCNT, READ_GAP_CYC SW_ARG(SENSING_VARIANT_DEFAULT));
    irq_restore(mask);
  }
}

#if SENSING_TELEMETRY
/* Spectrum lab: hold the LED output constant (TIMER1 compare above the reload value) so
 * the 100 Hz PWM edges are gone; sensing_led_tracking() restores the normal duty. */
void sensing_led_hold(void) {
  TIMER1_CH1CV = 1000U;
}

/* Spectrum lab: one sampling window with no excitation on receive coil `coil` of
 * `axis` (the integrator floor plus whatever couples into the front end). */
uint16_t sensing_read_idle(uint8_t axis, uint8_t coil) {
  if (axis > AXIS_Y || coil == 0U || coil > mux_count(axis)) {
    return 0U;
  }
  uint32_t mask = irq_save();
  GPIOB_OCTL |= 0x78U;
  mux_select_receive(&mux_table(axis)[coil - 1U] SW_ARG(SENSING_VARIANT_DEFAULT));
  const uint16_t value = sample_window(DWT_CYCCNT, READ_GAP_CYC SW_ARG(SENSING_VARIANT_DEFAULT));
  irq_restore(mask);
  if (service_hook) {
    service_hook();
  }
  return value;
}
#endif

#if SENSING_TELEMETRY || SENSING_PA15_FAST || SENSING_PINS_STOCK
/* Telemetry lab (and the -Pa15Fast / -PinsStock builds): set the whole pin-drive
 * state of one read from `variant`.
 * Full-register writes of constants, branch-free (emulator ITSTATE rule), so
 * every variant costs the same cycles and no state leaks between reads.
 * Base values are the 0.5.1 register contents (see gpio_speed(): pins 8..15
 * land in OSPD1 bits 3, 5 and 15). */
static void apply_pin_variant(uint8_t variant) {
  const uint32_t fast = 0U - (uint32_t)((variant >> 1) & 1U);   /* PA15_FAST */
  const uint32_t slow = 0U - (uint32_t)((variant >> 2) & 1U);   /* CTL_SLOW  */
  /* GPIOA OSPD0: pins 1,2,3,5,6,7 at speed 2 (0xA8A8); CTL_SLOW zeroes pins
   * 2,3,5,6,7 (mask 0xFCF0); PA15_FAST sets pin 15 to 11. */
  GPIO_OSPD0(GPIOA_BASE) = (0x0000A8A8UL & ~(slow & 0x0000FCF0UL)) |
                           (fast & (3UL << 30));
  /* GPIOA OSPD1: bits 3, 5 (PA3, PA5) and 15 (PA15) in the 0.5.1 state. */
  GPIO_OSPD1(GPIOA_BASE) = 0x00008028UL & ~(slow & 0x00000028UL) &
                           ~(fast & 0x00008000UL);
  /* GPIOB OSPD0: PB3..PB7 at speed 2 (0xAA80); CTL_SLOW: PB3..PB6 = 00,
   * PB7 = 01. */
  GPIO_OSPD0(GPIOB_BASE) = (0x0000AA80UL & ~(slow & 0x0000FFC0UL)) |
                           (slow & 0x00004000UL);
}
#endif

/* 29-pulse burst on one coil. Returns the PA3 fall time (burst end). */
static uint32_t excite_coil(uint8_t tx_axis, uint8_t tx_coil, uint8_t bin,
                            uint8_t variant) {
  const uint32_t period = excite_period_cyc[bin];
  const uint32_t high = period / 2U;

#if LOOP_MODES
  /* Round 11 diagnostic: had the host already taken the vendor report (EP1 IN
   * transfer complete) when this read of the frame started? Branch-free. */
  loop_in_mask |= ((USBFS_DIEPINT1 & USBFS_DIEPINT_XFRC) & (uint32_t)frame_active)
                  << (loop_read_count & 15U);
  loop_read_count++;
#endif

#if LOOP_MODES
  if (loop_dwell && next_burst_valid) {
    /* DWELL: wait for the burst start with every mux deselected (the previous
     * read left them so) and select the TX mux only LOOP_DWELL_LEAD_CYC before
     * it, so every burst has the same short TX dwell. Without it a read that
     * waits, such as the first read of a rigid frame, keeps the TX mux
     * selected for the whole wait. */
    const uint32_t pre = next_burst_at - LOOP_DWELL_LEAD_CYC;
    const int32_t rem = (int32_t)(pre - DWT_CYCCNT);
    if (rem > 0 && rem <= (int32_t)MAX_GAP_CYC) {
      wait_until(pre);
    }
  }
#endif
  GPIOB_OCTL |= 0x78U;             /* deselect all (idle leaves PB3..PB6 low) */
  mux_select(&mux_table(tx_axis)[tx_coil - 1U] SW_ARG(variant));
  gpio_mode(GPIOA_BASE, 15U, GPIO_MODE_OUTPUT);
#if SENSING_TELEMETRY || SENSING_PA15_FAST || SENSING_PINS_STOCK
  apply_pin_variant(variant);       /* 0.6.3 lab: PA15_FAST / CTL_SLOW */
#else
  gpio_speed(GPIOA_BASE, 15U, 2U);  /* 0.6.1: 0.5.1 setting */
#endif
  GPIOA_BC = (1UL << 15);

  uint32_t t = DWT_CYCCNT + PA15_SETTLE_CYC;
#if LOOP_MODES
  {
    /* Lateness: how far the earliest possible start is past the requested
     * start (0 when this read waits for it). Branch-free. */
    const uint32_t d = t - next_burst_at;
    loop_last_late = d & ~(uint32_t)((int32_t)d >> 31) &
                     (0U - (uint32_t)next_burst_valid);
    /* Round 12: keep the first read's lateness (loop_read_count is 1 for the
     * first read after sensing_frame_begin()). Branch-free. */
    const uint32_t first = 0U - (uint32_t)(loop_read_count == 1U);
    loop_frame_first_late = (loop_frame_first_late & ~first) | (loop_last_late & first);
  }
#endif
  /* The remaining gap can never exceed MAX_GAP_CYC; the bound also keeps a
   * stale deadline (reads paused for > 2^31 cycles) from waiting ~30 s. */
  if (next_burst_valid && (int32_t)(next_burst_at - t) > 0 &&
      (int32_t)(next_burst_at - t) <= (int32_t)MAX_GAP_CYC) {
    t = next_burst_at;
  }
#if SENSING_TELEMETRY
  const uint32_t t_requested = t;
  uint32_t t_observed;
#endif
  wait_until(t);
  {
    /* ANCHOR: take every later deadline of this read from the observed
     * start, so the poll phase of every following wait is identical on every
     * read. Branch-free (emulator ITSTATE rule). */
    const uint32_t now = DWT_CYCCNT;
    const uint32_t amask = 0U - (uint32_t)(variant & 1U);
    t = (t & ~amask) | (now & amask);
#if SENSING_TELEMETRY
    t_observed = now;
#endif
  }
  GPIOA_BOP = (1UL << 3);          /* PA3 driver enable */
#if SENSING_TELEMETRY
  /* Overshoot bookkeeping runs after the PA3 rise so that telemetry builds
   * keep the plain build's PA3 lead. The asm pins it after the store. */
  __asm volatile ("" : "+r"(t_observed) :: "memory");
  {
    const uint32_t d = t_observed - t_requested;
    last_overshoot = (uint8_t)((d > 255U) ? 255U : d);
  }
#endif

  uint32_t edge = t + high + EXCITE_LEAD_EXTRA_CYC;
#if LOOP_MODES
  for (unsigned i = 0; i < loop_pulses; i++) {
#else
  for (unsigned i = 0; i < EXCITE_PULSES; i++) {
#endif
    wait_until(edge);
    GPIOA_BOP = (1UL << 15);
    wait_until(edge + high);
    GPIOA_BC = (1UL << 15);
    edge += period;
  }
  /* last fall = edge - period + high; PA3 falls T + 167 cycles later */
  const uint32_t t_end = edge + high + EXCITE_TRAIL_EXTRA_CYC;
  wait_until(t_end);
  GPIOA_BC = (1UL << 3);           /* PA3 driver disable */
  gpio_mode(GPIOA_BASE, 15U, GPIO_MODE_INPUT);
  gpio_pull(GPIOA_BASE, 15U, GPIO_PULL_NONE);
  return t_end;
}

static uint16_t sensing_read_raw(uint8_t rx_axis, uint8_t rx_coil,
                                 uint8_t tx_axis, uint8_t tx_coil, uint8_t bin,
                                 uint32_t gap_cyc, uint8_t variant) {
  uint32_t mask = irq_save();
  const uint32_t t_end = excite_coil(tx_axis, tx_coil, bin, variant);
  GPIOB_OCTL |= 0x78U;
  mux_select_receive(&mux_table(rx_axis)[rx_coil - 1U] SW_ARG(variant));
  const uint16_t value = sample_window(t_end, gap_cyc SW_ARG(variant));
  irq_restore(mask);
#if H420X_PROFILE == 4 || LOOP_MODES
  if (service_hook && !frame_active) {
#else
  if (service_hook) {
#endif
    service_hook();
  }
  return value;
}

static void pa1_pwm_init(void) {
  /* Stock parity: TIMER1 CH1 on PA1 AF2, 100 Hz, compare 899 (out of range). */
  RCU_APB1EN |= RCU_APB1EN_TIMER1;
  gpio_mode(GPIOA_BASE, 1U, GPIO_MODE_AF);
  gpio_speed(GPIOA_BASE, 1U, 2U);
  gpio_af(GPIOA_BASE, 1U, 2U);

  TIMER1_CTL0 = 0U;
  TIMER1_PSC = 719U;
  TIMER1_CAR = 999U;
#if defined(SENSING_LED_HOLD) && SENSING_LED_HOLD
  TIMER1_CH1CV = 1000U;
#else
  TIMER1_CH1CV = 899U;
#endif
  TIMER1_CHCTL0 = (TIMER1_CHCTL0 & ~(0xFFUL << 8)) |
                  TIMER1_CHCTL0_CH1_PWM0;
  TIMER1_CHCTL2 = (TIMER1_CHCTL2 & ~(0x3UL << 4)) |
                  TIMER1_CHCTL2_CH1EN;
  TIMER1_SWEVG = TIMER1_SWEVG_UPG;
  TIMER1_CTL0 = TIMER1_CTL0_ARSE | TIMER1_CTL0_CEN;
}

void sensing_led_tracking(uint8_t tracking) {
#if defined(SENSING_LED_HOLD) && SENSING_LED_HOLD
  /* Diagnostic (-LedHold): compare value above the reload value, so the output stays at one
   * level and the 100 Hz PWM edges never reach the board. */
  (void)tracking;
  TIMER1_CH1CV = 1000U;
#else
  /* Branch-free: 59 while tracking, 899 otherwise (stock values). */
  const uint32_t on = 0U - (uint32_t)(tracking != 0U);
  TIMER1_CH1CV = 899U - (840U & on);
#endif
}

void sensing_init(void) {
  RCU_AHBEN |= RCU_AHBEN_GPIOA | RCU_AHBEN_GPIOB;
  pa1_pwm_init();

  /* PA15 excitation, PA3 driver enable, PA2 integrator, PA5..7 mux address.
   * 0.6.1: 0.5.1 pin setup (the stock-speed setup of 0.6.0 read no pen). */
  for (unsigned pin = 2U; pin <= 3U; pin++) {
    gpio_mode(GPIOA_BASE, pin, GPIO_MODE_OUTPUT);
    gpio_speed(GPIOA_BASE, pin, 2U);
  }
  for (unsigned pin = 5U; pin <= 7U; pin++) {
    gpio_mode(GPIOA_BASE, pin, GPIO_MODE_OUTPUT);
    gpio_speed(GPIOA_BASE, pin, 2U);
  }
  gpio_mode(GPIOA_BASE, 15U, GPIO_MODE_OUTPUT);
  gpio_speed(GPIOA_BASE, 15U, 2U);

  /* Mux chip/data pins on GPIOB: PB3..PB7. */
  for (unsigned pin = 3U; pin <= 7U; pin++) {
    gpio_mode(GPIOB_BASE, pin, GPIO_MODE_OUTPUT);
    gpio_speed(GPIOB_BASE, pin, 2U);
  }

  GPIOA_BC = (1UL << 15) | (1UL << 3); /* excitation low, driver off */
  GPIOA_BOP = (1UL << 2);              /* integrator reset high */
  GPIOB_OCTL &= ~0x78U;                /* stock idle: PB3..PB6 low */

  dwt_init();
  adc_init();
}

void sensing_set_bin(uint8_t bin) {
  if (bin >= 1U && bin <= 12U) {
    current_bin = bin;
  }
}

uint8_t sensing_bin(void) {
  return current_bin;
}

uint16_t sensing_read_coil_gap(uint8_t axis, uint8_t receive_coil,
                               uint8_t excite_center, uint8_t bin,
                               uint8_t long_gap) {
  if (axis > AXIS_Y || receive_coil == 0U ||
      receive_coil > mux_count(axis) ||
      excite_center == 0U ||
      excite_center > mux_count((axis == AXIS_X) ? AXIS_Y : AXIS_X) ||
      bin < 1U || bin > 12U) {
    return 0U;
  }

  return sensing_read_raw(axis, receive_coil,
                          (axis == AXIS_X) ? AXIS_Y : AXIS_X,
                          excite_center, bin,
#if LOOP_MODES
                          long_gap ? LONG_GAP_CYC : (READ_GAP_CYC + loop_extra_gap),
                          (uint8_t)(SENSING_VARIANT_DEFAULT | loop_variant));
#else
                          long_gap ? LONG_GAP_CYC : READ_GAP_CYC,
                          SENSING_VARIANT_DEFAULT);
#endif
}

uint16_t sensing_read_coil(uint8_t axis, uint8_t receive_coil,
                           uint8_t excite_center, uint8_t bin) {
  return sensing_read_coil_gap(axis, receive_coil, excite_center, bin, 0U);
}

uint16_t sensing_read_coil_variant(uint8_t axis, uint8_t receive_coil,
                                   uint8_t excite_center, uint8_t bin,
                                   uint8_t variant) {
  if (axis > AXIS_Y || receive_coil == 0U ||
      receive_coil > mux_count(axis) ||
      excite_center == 0U ||
      excite_center > mux_count((axis == AXIS_X) ? AXIS_Y : AXIS_X) ||
      bin < 1U || bin > 12U) {
    return 0U;
  }
  return sensing_read_raw(axis, receive_coil,
                          (axis == AXIS_X) ? AXIS_Y : AXIS_X,
                          excite_center, bin, READ_GAP_CYC,
                          (uint8_t)((variant & SENSING_VARIANT_ALLOWED) |
                                    SENSING_VARIANT_DEFAULT));
}

#if SENSING_TELEMETRY
uint16_t sensing_read_coil_spaced(uint8_t axis, uint8_t receive_coil, uint8_t excite_center,
                                  uint8_t bin, uint8_t variant, uint32_t gap_cyc) {
  if (axis > AXIS_Y || receive_coil == 0U ||
      receive_coil > mux_count(axis) ||
      excite_center == 0U ||
      excite_center > mux_count((axis == AXIS_X) ? AXIS_Y : AXIS_X) ||
      bin < 1U || bin > 12U) {
    return 0U;
  }
  if (gap_cyc > 21000U) {
    gap_cyc = 21000U;
  }
  return sensing_read_raw(axis, receive_coil,
                          (axis == AXIS_X) ? AXIS_Y : AXIS_X,
                          excite_center, bin, gap_cyc,
                          (uint8_t)((variant & SENSING_VARIANT_ALLOWED) |
                                    SENSING_VARIANT_DEFAULT));
}
#endif

void sensing_hold_off_us(uint32_t us) {
  if (us > MAX_GAP_CYC / 72U) {
    us = MAX_GAP_CYC / 72U;
  }
  const uint32_t t = DWT_CYCCNT + us * 72U;
  if (!next_burst_valid || (int32_t)(t - next_burst_at) > 0) {
    next_burst_at = t;
    next_burst_valid = 1U;
  }
}

uint16_t sensing_read_self(uint8_t axis, uint8_t coil, uint8_t bin) {
  if (axis > AXIS_Y || coil == 0U || coil > mux_count(axis) ||
      bin < 1U || bin > 12U) {
    return 0U;
  }
  return sensing_read_raw(axis, coil, axis, coil, bin, READ_GAP_CYC,
                          SENSING_VARIANT_DEFAULT);
}

int sensing_read_window(uint8_t axis, uint8_t center, uint8_t excite_center,
                        uint8_t bin, uint16_t amps[4]) {
  if (axis > AXIS_Y || center < WINDOW_CENTER_MIN ||
      center > mux_count(axis)) {
    return -1;
  }

#if LOOP_MODES
  uint32_t late_max = 0U;
  /* Round 11/12: the Y window (7R) or both windows (6R) can skip the slot-0
   * preparation read. amps[0] = 0 then makes window_rise() measure the used
   * slots against 0. */
  const unsigned first_slot = (axis == AXIS_Y) ? (unsigned)loop_skip_prep_y
                                               : (unsigned)loop_skip_prep_x;
  amps[0] = 0U;
  loop_window_first_late[axis] = 0U;
  for (unsigned k = 0; k < loop_extra_prep; k++) {
    /* RIGID_PREP / TX_FIRST: one more discarded read of the slot-0 coil. */
    int32_t coil = (int32_t)center - 2;
    if (coil < 1) {
      coil = 1;
    }
    (void)sensing_read_coil(axis, (uint8_t)coil, excite_center, bin);
    if (k == 0U) {
      loop_window_first_late[axis] = loop_last_late;
    }
  }
#endif
#if LOOP_MODES
  for (unsigned slot = first_slot; slot < 4U; slot++) {
#else
  for (unsigned slot = 0; slot < 4U; slot++) {
#endif
    int32_t coil = (int32_t)center - 2 + (int32_t)slot;
#if TRACK_EDGE_EXT
    if (coil < 1) {
      /* Center 2: there is no coil 0. Slot 0 is the discarded preparation
       * read; it reads coil 1, which slot 1 then reads again. */
      coil = 1;
    }
#endif
    if (coil < 1 || coil > (int32_t)mux_count(axis)) {
      return -1;
    }
#if H420X_PROFILE == 4
    prep_pending = (slot == 0U) ? 1U : 0U;
#endif
    amps[slot] = sensing_read_coil(axis, (uint8_t)coil, excite_center, bin);
#if H420X_PROFILE == 4
    prep_pending = 0U;
#endif
    if (amps[slot] == 0U) {
      return -1;
    }
#if LOOP_MODES
    if (slot == 0U) {
      if (loop_extra_prep == 0U) {
        loop_window_first_late[axis] = loop_last_late;
      }
    } else if (loop_last_late > late_max) {
      late_max = loop_last_late;
    }
#endif
  }
#if LOOP_MODES
  loop_window_late[axis] = late_max;
#endif
  return 0;
}

#if LOOP_MODES
/* Round 13: exact-cadence frame reader. Every burst of the frame starts at a
 * DWT deadline: read k+1's PA3 rise = read k's PA3 rise + cadence_period(), so
 * the read timing does not depend on how fast the code between reads runs.
 * A read whose code reaches the deadline late starts as soon as it can and the
 * schedule continues from its actual start. */

/* PA3 rise -> PA3 rise of consecutive reads, cycles: the burst (PA3 rise ->
 * PA3 fall, see excite_coil()), PA2 low, the integration, the conversions, the
 * normal read gap and the run-time offset. */
static uint32_t cadence_period(uint32_t period, uint32_t high) {
  const uint32_t base = (uint32_t)loop_pulses * period + 2U * high +
                        EXCITE_LEAD_EXTRA_CYC + EXCITE_TRAIL_EXTRA_CYC +
                        loop_pa2_low + loop_adc1 +
                        (uint32_t)(loop_nconv - 1U) * CAD_CONV_STEP_CYC +
                        CAD_CONV_BUDGET_CYC + READ_GAP_CYC + loop_cad_pad;
  const uint32_t off = ((uint32_t)loop_cad_phase * period) / 60U;
  if (loop_cad_abs == 0U) {
    return base + off;
  }
  const uint32_t lo = base - CAD_ABS_SLACK_CYC;
  return lo + ((off + period - (lo % period)) % period);
}

/* One read with PA3 rising at *t (DWT), or as soon as the code gets there if
 * that is later; *t becomes the actual rise and *late how far it is past the
 * requested one. Pin sequence of excite_coil() with DWELL plus
 * sensing_read_raw(), without the per-read bookkeeping. Interrupts must be
 * masked. Returns the sample (0 = ADC timeout). */
static uint16_t cadence_read(const mux_entry_t *tx, const mux_entry_t *rx,
                             uint32_t *t, uint32_t period, uint32_t high,
                             uint8_t variant, uint32_t *late) {
  const uint32_t pre = *t - LOOP_DWELL_LEAD_CYC;
  const int32_t rem = (int32_t)(pre - DWT_CYCCNT);
  if (rem > 0 && rem <= (int32_t)MAX_GAP_CYC) {
    wait_until(pre);                 /* every mux deselected until just before the burst */
  }
  GPIOB_OCTL |= 0x78U;
  mux_select(tx SW_ARG(variant));
  gpio_mode(GPIOA_BASE, 15U, GPIO_MODE_OUTPUT);
#if SENSING_TELEMETRY || SENSING_PA15_FAST || SENSING_PINS_STOCK
  apply_pin_variant(variant);
#else
  gpio_speed(GPIOA_BASE, 15U, 2U);
#endif
  GPIOA_BC = (1UL << 15);

  /* Late when the earliest possible rise is past the deadline. Branch-free. */
  const uint32_t earliest = DWT_CYCCNT + PA15_SETTLE_CYC;
  const uint32_t d = earliest - *t;
  const uint32_t lm = 0U - (uint32_t)((int32_t)d > 0);
  const uint32_t start = (*t & ~lm) | (earliest & lm);
  *late = d & lm;
  *t = start;
  wait_until(start);
  GPIOA_BOP = (1UL << 3);          /* PA3 driver enable */

  uint32_t edge = start + high + EXCITE_LEAD_EXTRA_CYC;
  for (unsigned i = 0; i < loop_pulses; i++) {
    wait_until(edge);
    GPIOA_BOP = (1UL << 15);
    wait_until(edge + high);
    GPIOA_BC = (1UL << 15);
    edge += period;
  }
  const uint32_t t_end = edge + high + EXCITE_TRAIL_EXTRA_CYC;
  wait_until(t_end);
  GPIOA_BC = (1UL << 3);           /* PA3 driver disable */
  gpio_mode(GPIOA_BASE, 15U, GPIO_MODE_INPUT);
  gpio_pull(GPIOA_BASE, 15U, GPIO_PULL_NONE);
  GPIOB_OCTL |= 0x78U;
  mux_select_receive(rx SW_ARG(variant));
  return sample_window(t_end, READ_GAP_CYC SW_ARG(variant));
}

int sensing_read_frame(uint8_t x_center, uint8_t y_center, uint8_t bin,
                       uint16_t xa[4], uint16_t ya[4]) {
  if (x_center < WINDOW_CENTER_MIN || x_center > COIL_COUNT_X ||
      y_center < WINDOW_CENTER_MIN || y_center > COIL_COUNT_Y ||
      bin < 1U || bin > 12U) {
    return -1;
  }

  /* The X window (receive X coils, transmit Y center coil), then the Y
   * window (receive Y coils, transmit X center coil), with the slot and coil
   * rules of sensing_read_window(). */
  const mux_entry_t *txs[8];
  const mux_entry_t *rxs[8];
  uint16_t *dst[8];
  unsigned n = 0U;
  xa[0] = 0U;
  ya[0] = 0U;
  for (unsigned slot = loop_skip_prep_x; slot < 4U; slot++) {
    int32_t coil = (int32_t)x_center - 2 + (int32_t)slot;
#if TRACK_EDGE_EXT
    if (coil < 1) {
      coil = 1;
    }
#endif
    if (coil < 1 || coil > (int32_t)COIL_COUNT_X) {
      return -1;
    }
    txs[n] = &y_mux[y_center - 1U];
    rxs[n] = &x_mux[coil - 1];
    dst[n] = &xa[slot];
    n++;
  }
  for (unsigned slot = loop_skip_prep_y; slot < 4U; slot++) {
    int32_t coil = (int32_t)y_center - 2 + (int32_t)slot;
#if TRACK_EDGE_EXT
    if (coil < 1) {
      coil = 1;
    }
#endif
    if (coil < 1 || coil > (int32_t)COIL_COUNT_Y) {
      return -1;
    }
    txs[n] = &x_mux[x_center - 1U];
    rxs[n] = &y_mux[coil - 1];
    dst[n] = &ya[slot];
    n++;
  }

  const uint32_t period = (loop_cad_period != 0U) ? (uint32_t)loop_cad_period : excite_period_cyc[bin];
  const uint32_t high = period / 2U;
  const uint32_t r = cadence_period(period, high);
  const uint8_t variant = (uint8_t)(SENSING_VARIANT_DEFAULT | loop_variant);
  const uint32_t mask = irq_save();

  /* First read: on the cadence schedule if there is one and it is not
   * stale (more than MAX_GAP_CYC ahead); otherwise as soon as possible. A
   * schedule in the past makes the first read late, which cadence_read()
   * measures. */
  uint32_t t = DWT_CYCCNT + LOOP_DWELL_LEAD_CYC;
  if (loop_cad_next_valid &&
      (int32_t)(loop_cad_next - t) <= (int32_t)MAX_GAP_CYC) {
    t = loop_cad_next;
  }
  uint32_t late = 0U;
  uint16_t v = cadence_read(txs[0], rxs[0], &t, period, high, variant, &late);
  const uint32_t t_first = t;      /* actual PA3 rise of the frame's first read */
#if FRAME_AUX
  if (loop_prev_frame_valid) {
    loop_frame_period_cyc = t_first - loop_prev_frame_first;
  }
  loop_prev_frame_first = t_first;
  loop_prev_frame_valid = 1U;
#endif
  *dst[0] = v;
  loop_frame_first_late = late;
  uint32_t max_late = 0U;
  int result = (v == 0U) ? -1 : 0;
  for (unsigned k = 1U; k < n && result == 0; k++) {
    t += r;
    v = cadence_read(txs[k], rxs[k], &t, period, high, variant, &late);
    *dst[k] = v;
    max_late = (late > max_late) ? late : max_late;
    result = (v == 0U) ? -1 : 0;
  }
  loop_frame_max_late = max_late;
  uint32_t next = t + r + CAD_FRAME_EXTRA_CYC;
  if (loop_cad_frame != 0U && (int32_t)((t_first + loop_cad_frame) - next) > 0) {
    next = t_first + loop_cad_frame;   /* round 22: frame period floor */
  }
  loop_cad_next = next;
  loop_cad_next_valid = (result == 0) ? 1U : 0U;
  irq_restore(mask);
  return result;
}
#endif

uint8_t sensing_self_search(uint8_t axis, uint8_t bin, uint16_t *peak_amplitude,
                            uint16_t *floor_amplitude) {
  if (axis > AXIS_Y) {
    return 0U;
  }

  uint16_t vals[COIL_COUNT_X];
  const uint8_t n = mux_count(axis);
  uint8_t best_coil = 0U;
  uint16_t best = 0U;
  for (uint8_t coil = 1U; coil <= n; coil++) {
    uint16_t v = sensing_read_self(axis, coil, bin);
    vals[coil - 1U] = v;
    if (v > best) {
      best = v;
      best_coil = coil;
    }
  }
  if (peak_amplitude) {
    *peak_amplitude = best;
  }
  if (floor_amplitude) {
    /* Insertion sort; the median is element n/2 (13 -> 6, 19 -> 9). */
    for (uint8_t i = 1U; i < n; i++) {
      uint16_t key = vals[i];
      int j = (int)i - 1;
      while (j >= 0 && vals[j] > key) {
        vals[j + 1] = vals[j];
        j--;
      }
      vals[j + 1] = key;
    }
    *floor_amplitude = vals[n / 2U];
  }
  return best_coil;
}

uint8_t sensing_sweep_bin(uint8_t x_coil, uint8_t y_coil,
                          uint16_t *peak_amplitude) {
  uint8_t best_bin = current_bin;
  uint16_t best = 0U;
  for (uint8_t bin = 1U; bin <= 12U; bin++) {
    uint16_t v = sensing_read_coil(AXIS_Y, y_coil, x_coil, bin);
    if (v > best) {
      best = v;
      best_bin = bin;
    }
  }
  current_bin = best_bin;
  if (peak_amplitude) {
    *peak_amplitude = best;
  }
  return best_bin;
}

uint8_t sensing_maintain_bin(uint8_t x_center, uint8_t y_center, uint8_t bin) {
  uint16_t current = sensing_read_coil(AXIS_X, x_center, y_center, bin);
  uint8_t best_bin = bin;
  uint16_t best = current;

  if (bin > 1U) {
    uint16_t lower = sensing_read_coil(AXIS_X, x_center, y_center,
                                       (uint8_t)(bin - 1U));
    if (lower > best) {
      best = lower;
      best_bin = (uint8_t)(bin - 1U);
    }
  }
  if (bin < 12U) {
    uint16_t upper = sensing_read_coil(AXIS_X, x_center, y_center,
                                       (uint8_t)(bin + 1U));
    if (upper > best) {
      best = upper;
      best_bin = (uint8_t)(bin + 1U);
    }
  }

  current_bin = best_bin;
  return best_bin;
}

#if SENSING_TELEMETRY
void sensing_last_read_info(uint16_t samples[3], uint8_t *overshoot) {
  samples[0] = last_samples[0];
  samples[1] = last_samples[1];
  samples[2] = last_samples[2];
  *overshoot = last_overshoot;
}
#endif
