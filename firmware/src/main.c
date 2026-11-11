/*
 * H420X clean-room firmware.
 *
 * Profile 0: M0 USB transport bring-up (synthetic circle, no analog access).
 * Profile 1: rhythm750 prototype (patch-line rhythm750 analog timing, send on completion).
 * Profile 2: rhythm900 prototype (patch-line rhythm900 analog timing, send on completion).
 * Profile 3: pen500 (rhythm750 analog timing, pressure/tip/buttons, 500 reports/s).
 * Profile 4: rhythm1000 (rhythm900 analog timing, USB-frame-locked frames, no USB work between reads).
 *
 * The sensing profiles are hardware-unvalidated clean-room prototypes. The
 * mux order, excitation timing and ADC path must be checked on the tablet
 * before trusting tracking data. The M0 profile remains the verified USB
 * transport image.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <stdint.h>

#include "tusb.h"
#include "device/dcd.h"

#include "board.h"
#include "gd32f350_regs.h"
#include "usb_descriptors.h"

#if OUTPUT_SMOOTH
#include "smooth.h"
#endif

#ifndef H420X_PROFILE
#define H420X_PROFILE 0
#endif

#ifndef NO_UART_LOG
#define NO_UART_LOG 0   /* build.ps1 -NoUartLog: remove the per-900-frame UART scan log */
#endif

#ifndef SENSING_TELEMETRY
#define SENSING_TELEMETRY 0
#endif

/* 0.6.4 build switches (all default off, plain builds are unchanged):
 *   ROBUST_LOCK  gated + confirmed acquisition, tolerant lock (build.ps1 -Robust)
 *   LAB_ENABLE   the 0.6.3 pin-drive lab in telemetry builds (build.ps1 -Lab) */
#ifndef ROBUST_LOCK
#define ROBUST_LOCK 0
#endif

#ifndef LAB_SPECTRUM
#define LAB_SPECTRUM 0   /* build.ps1 -Spectrum: the lab records raw back-to-back read series instead */
#endif
#ifndef LAB_ENABLE
#define LAB_ENABLE 0
#endif
#ifndef LOOP_MODES
#define LOOP_MODES 0     /* build.ps1 -Modes / -LoopMode <n>: round 10 loop-mode diagnostic (rhythm900) */
#endif
#ifndef LOOP_WARM
#define LOOP_WARM 0      /* build.ps1 -LoopWarm: HOOK for the first LOOP_BLOCK frames of every lock */
#endif
#ifndef LOOP_ARM
#define LOOP_ARM 0       /* build.ps1 -LoopArm: HOOK only for the unpublished arming frames of every lock */
#endif
#ifndef LOOP_DIAG
#define LOOP_DIAG 0      /* build.ps1 -Diag18: device clock, USB counters, heartbeat, stuck LED (round 18) */
#endif
#ifndef FRAME_AUX
#define FRAME_AUX 0      /* build.ps1 -FrameAux: aux low 11 bits = measured cadence frame period, us */
#endif
#ifndef WAVE_AUX
#define WAVE_AUX 0       /* build.ps1 -WaveAux: aux = one raw window read per frame (round 41 diagnostic) */
#endif
#ifndef LATE_CONNECT
#define LATE_CONNECT 0   /* build.ps1 -LateConnect: hold USB D+ down until sensing_init finishes */
#endif
#ifndef QUIET_HEARTBEAT
#define QUIET_HEARTBEAT 0 /* build.ps1 -QuietHeartbeat: 0xD0 only while the endpoint is stuck */
#endif
#ifndef SAMPT_MODES
#define SAMPT_MODES 0    /* build.ps1 -SamptModes: ADC sample-time rotation instead of the e table */
#endif
#ifndef CAD_RESET_FRAMES
#define CAD_RESET_FRAMES 0 /* build.ps1 -CadReset <n>: re-apply the mode every n published frames */
#endif
#ifndef FINE_MODES
#define FINE_MODES 0     /* build.ps1 -FineModes: hyst/e rotation at S41.5 (round 38) */
#endif
#ifndef RATE_MODES
#define RATE_MODES 0     /* build.ps1 -RateModes: 800 Hz rate/scanning rotation (round 39) */
#endif
#ifndef SCAN1K_MODES
#define SCAN1K_MODES 0   /* build.ps1 -Scan1kModes: 1 kHz long-window/frame-floor rotation (round 40) */
#endif
#ifndef PHASE_MODES
#define PHASE_MODES 0    /* build.ps1 -PhaseModes (needs -Scan1kModes): frame start phase alignment rotation (round 42) */
#endif
#ifndef ORDER_MODES
#define ORDER_MODES 0    /* build.ps1 -OrderModes (needs -Scan1kModes): read-order rotation (round 44) */
#endif
#ifndef SETTLE_MODES
#define SETTLE_MODES 0   /* build.ps1 -SettleModes (needs -Scan1kModes): settle-gap/floor rotation (round 44) */
#endif
#ifndef YPREP_MODES
#define YPREP_MODES 0    /* build.ps1 -YPrepModes (needs -Scan1kModes): Y-first + Y prep / no X prep rotation (round 45) */
#endif
#ifndef GAP_MODES
#define GAP_MODES 0      /* build.ps1 -GapModes (needs -Scan1kModes): read-gap sweep (round 49A) */
#endif
#ifndef CONV_MODES
#define CONV_MODES 0     /* build.ps1 -ConvModes (needs -Scan1kModes): sample-time / median-of-3 sweep (round 49B) */
#endif
#ifndef JITTER_MODES
#define JITTER_MODES 0   /* build.ps1 -JitterModes (needs -Scan1kModes): longer sample + gap combination (round 49C) */
#endif
#ifndef AXIS_GAP_MODES
#define AXIS_GAP_MODES 0 /* build.ps1 -AxisGapModes (needs -Scan1kModes): per-axis read-gap sweep (round 50) */
#endif
#ifndef FRAME_FLOOR_US
#define FRAME_FLOOR_US 0 /* build.ps1 -FrameFloorUs <us>: override the per-mode cadence frame floor */
#endif
#ifndef R55_MODES
#define R55_MODES 0      /* build.ps1 -R55Modes (needs -AxisGapModes -FastEp1): start alignment / exact landing rotation (round 55) */
#endif
#if R55_MODES && !(AXIS_GAP_MODES && defined(FAST_EP1) && FAST_EP1)
#error "-R55Modes needs -AxisGapModes and -FastEp1"
#endif
#ifndef R56_MODES
#define R56_MODES 0      /* build.ps1 -R56Modes (implies -R55Modes): no-Y-prep / longer-read rotation (round 56) */
#endif
#if R56_MODES && !R55_MODES
#error "-R56Modes needs the round 55 machinery (R55_MODES)"
#endif
#ifndef R57_MODES
#define R57_MODES 0      /* build.ps1 -R57Modes (implies -R56Modes): DWELL-start prep / grid-size rotation (round 57) */
#endif
#if R57_MODES && !R56_MODES
#error "-R57Modes needs the round 56 machinery (R56_MODES)"
#endif
#ifndef R58_MODES
#define R58_MODES 0      /* build.ps1 -R58Modes (implies -R57Modes): relative grids / same-hold ADC window / pulse-integration rotation (round 58) */
#endif
#if R58_MODES && !R57_MODES
#error "-R58Modes needs the round 57 machinery (R57_MODES)"
#endif
#ifndef R59_MODES
#define R59_MODES 0      /* build.ps1 -R59Modes (implies -R58Modes): pulse ladder 32..20 on the relative 139-cycle grid (round 59) */
#endif
#if R59_MODES && !R58_MODES
#error "-R59Modes needs the round 58 machinery (R58_MODES)"
#endif
#ifndef R55_AUX
#define R55_AUX 1        /* build.ps1 -NoR55Aux sets 0: no R55 aux, the report falls through to the release aux zero */
#endif
#ifndef LOOP_HEARTBEAT
#define LOOP_HEARTBEAT 0 /* build.ps1 -Release: heartbeat only, no diagnostic aux/UART */
#endif
#ifndef RELEASE_AUX_ZERO
#define RELEASE_AUX_ZERO 0 /* build.ps1 -Release: pen report aux bytes stay zero */
#endif
#if LOOP_DIAG && !LOOP_MODES
#error "-Diag18 needs -Modes or -LoopMode <n>"
#endif
#if LOOP_HEARTBEAT && !LOOP_MODES
#error "-Release needs -Modes or -LoopMode <n>"
#endif

#if H420X_PROFILE == 0
#define FW_VERSION "0.2.0-m0"
#elif H420X_PROFILE == 1
#define FW_VERSION "0.6.5-rhythm750"
#elif H420X_PROFILE == 2
#define FW_VERSION "0.6.5-rhythm900"
#elif H420X_PROFILE == 3
#if defined(PEN_PROBE_MASK) && (PEN_PROBE_MASK == 0)
#define FW_VERSION "0.6.5-pen500-fastp"
#else
#define FW_VERSION "0.6.5-pen500"
#endif
#elif H420X_PROFILE == 4
#define FW_STR2(x) #x
#define FW_STR(x) FW_STR2(x)
#if defined(EXCITE_PULSES)
#define FW_VERSION "0.6.5-rhythm1000-p" FW_STR(EXCITE_PULSES)
#else
#define FW_VERSION "0.6.5-rhythm1000"
#endif
#else
#error "Unknown H420X_PROFILE"
#endif

#if LOOP_MODES
/* Round 11 IN lock: count and DWT time of the vendor endpoint's IN transfer
 * completions (the host took a report), read before TinyUSB clears the flag.
 * Global (not static) so the emulator harness can find them. */
volatile uint32_t loop_in_count;
volatile uint32_t loop_in_t;
#if LOOP_DIAG
/* Round 18: SETUP stages seen by the USB interrupt (host control requests) and USB
 * bus events (mount, unmount, suspend, resume) since boot. */
volatile uint32_t loop_setup_count;
static uint32_t loop_usb_events;
#endif
#endif

void USBFS_IRQHandler(void) {
#if LOOP_MODES
  {
    const uint32_t done = USBFS_DIEPINT1 & USBFS_DIEPINT_XFRC;
    const uint32_t now = DWT_CYCCNT;
    loop_in_t ^= (loop_in_t ^ now) & (0U - done);   /* branch-free: loop_in_t = now when done */
    loop_in_count += done;
#if LOOP_DIAG
    loop_setup_count += (USBFS_DOEPINT0 & USBFS_DOEPINT_STUP) >> 3;   /* branch-free */
#endif
  }
#endif
  dcd_int_handler(0);
}

/* ------------------------------------------------------------------------- */
/* Common USB callbacks                                                      */
/* ------------------------------------------------------------------------- */
#if defined(FAST_EP1) && FAST_EP1
#include "report.h"
#endif

void tud_mount_cb(void) {
#if LOOP_MODES && LOOP_DIAG
  loop_usb_events++;
#endif
#if defined(FAST_EP1) && FAST_EP1
  report_fast_reset();   /* new configuration: EP1 was re-opened, mask its IN interrupt again */
#endif
  uart_puts("usb=mounted\r\n");
}

void tud_umount_cb(void) {
#if LOOP_MODES && LOOP_DIAG
  loop_usb_events++;
#endif
#if defined(FAST_EP1) && FAST_EP1
  report_fast_reset();
#endif
  uart_puts("usb=unmounted\r\n");
}

void tud_suspend_cb(bool remote_wakeup_en) {
#if LOOP_MODES && LOOP_DIAG
  loop_usb_events++;
#endif
  (void)remote_wakeup_en;
  uart_puts("usb=suspended\r\n");
}

void tud_resume_cb(void) {
#if LOOP_MODES && LOOP_DIAG
  loop_usb_events++;
#endif
  uart_puts("usb=resumed\r\n");
}

/* ------------------------------------------------------------------------- */
/* M0 transport bring-up                                                     */
/* ------------------------------------------------------------------------- */
#if H420X_PROFILE == 0

#include "report.h"

/* 256-entry full-circle sine, Q15. One revolution per 4096 frames (4.096 s). */
static const int16_t sine_q15[256] = {
    0, 804, 1608, 2410, 3212, 4011, 4808, 5602,
    6393, 7179, 7962, 8739, 9512, 10278, 11039, 11793,
    12539, 13279, 14010, 14732, 15446, 16151, 16846, 17530,
    18204, 18868, 19519, 20159, 20787, 21403, 22005, 22594,
    23170, 23731, 24279, 24811, 25329, 25832, 26319, 26790,
    27245, 27683, 28105, 28510, 28898, 29268, 29621, 29956,
    30273, 30571, 30852, 31113, 31356, 31580, 31785, 31971,
    32137, 32285, 32412, 32521, 32609, 32678, 32728, 32757,
    32767, 32757, 32728, 32678, 32609, 32521, 32412, 32285,
    32137, 31971, 31785, 31580, 31356, 31113, 30852, 30571,
    30273, 29956, 29621, 29268, 28898, 28510, 28105, 27683,
    27245, 26790, 26319, 25832, 25329, 24811, 24279, 23731,
    23170, 22594, 22005, 21403, 20787, 20159, 19519, 18868,
    18204, 17530, 16846, 16151, 15446, 14732, 14010, 13279,
    12539, 11793, 11039, 10278, 9512, 8739, 7962, 7179,
    6393, 5602, 4808, 4011, 3212, 2410, 1608, 804,
    0, -804, -1608, -2410, -3212, -4011, -4808, -5602,
    -6393, -7179, -7962, -8739, -9512, -10278, -11039, -11793,
    -12539, -13279, -14010, -14732, -15446, -16151, -16846, -17530,
    -18204, -18868, -19519, -20159, -20787, -21403, -22005, -22594,
    -23170, -23731, -24279, -24811, -25329, -25832, -26319, -26790,
    -27245, -27683, -28105, -28510, -28898, -29268, -29621, -29956,
    -30273, -30571, -30852, -31113, -31356, -31580, -31785, -31971,
    -32137, -32285, -32412, -32521, -32609, -32678, -32728, -32757,
    -32767, -32757, -32728, -32678, -32609, -32521, -32412, -32285,
    -32137, -31971, -31785, -31580, -31356, -31113, -30852, -30571,
    -30273, -29956, -29621, -29268, -28898, -28510, -28105, -27683,
    -27245, -26790, -26319, -25832, -25329, -24811, -24279, -23731,
    -23170, -22594, -22005, -21403, -20787, -20159, -19519, -18868,
    -18204, -17530, -16846, -16151, -15446, -14732, -14010, -13279,
    -12539, -11793, -11039, -10278, -9512, -8739, -7962, -7179,
    -6393, -5602, -4808, -4011, -3212, -2410, -1608, -804,
};

static volatile uint32_t tx_ok;
static volatile uint32_t tx_miss;
static uint32_t sequence;
static uint32_t frame_index;

/* M0 only: send a synthetic report whenever the vendor endpoint can accept
 * one. This is the same send-on-ready path used by the sensing profiles, so
 * it validates the production transport rather than the SOF callback. */
static void send_report(void) {
  if (!tud_ready() || !tud_hid_n_ready(ITF_VENDOR)) {
    return;
  }

  const unsigned idx = frame_index & 0xFFU;
  const int32_t c = sine_q15[idx];
  const int32_t s = sine_q15[(idx + 64U) & 0xFFU];

  int32_t x = 10600 + ((c * 8000) >> 15);
  int32_t y = 6600 + ((s * 5200) >> 15);

  uint8_t report[VENDOR_REPORT_LEN];
  report[0] = 0x80U;
  report[1] = (uint8_t)(x & 0xFF);
  report[2] = (uint8_t)((x >> 8) & 0xFF);
  report[3] = (uint8_t)(y & 0xFF);
  report[4] = (uint8_t)((y >> 8) & 0xFF);
  report[5] = 0;
  report[6] = 0;
  report[7] = (uint8_t)(sequence & 0xFF);
  report[8] = (uint8_t)((sequence >> 8) & 0xFF);
  report[9] = 0;
  report[10] = 0;

  if (tud_hid_n_report(ITF_VENDOR, REPORT_ID_VENDOR, report,
                       VENDOR_REPORT_LEN)) {
    sequence++;
    tx_ok++;
    frame_index++;
  } else {
    tx_miss++;
  }
}

static void log_status(void) {
  uart_puts("tx=");
  uart_u32(tx_ok);
  uart_puts(" miss=");
  uart_u32(tx_miss);
  uart_puts(" cfg=");
  uart_u32(tud_ready() ? 1U : 0U);
  uart_puts(" seq=");
  uart_u32(sequence);
  uart_puts("\r\n");
}

int main(void) {
  board_init();

  uart_puts("\r\nH420X cleanroom ");
  uart_puts(FW_VERSION);
  uart_puts("\r\n");

  const tusb_rhport_init_t rh_init = {
      .role = TUSB_ROLE_DEVICE,
      .speed = TUSB_SPEED_FULL,
  };
  if (!tusb_init(0, &rh_init)) {
    uart_puts("usb=init-failed\r\n");
    for (;;) {
      uart_poll();
    }
  }

  uart_puts("usb=init-ok\r\n");
  __asm volatile ("cpsie i");

  uint32_t last_log = 0;
  for (;;) {
    tud_task();
    uart_poll();
    send_report();

    if ((tx_ok - last_log) >= 1000U) {
      last_log = tx_ok;
      log_status();
    }
  }
}

/* ------------------------------------------------------------------------- */
/* Sensing prototypes                                                        */
/* ------------------------------------------------------------------------- */
#else

#include "report.h"
#include "sensing.h"
#include "tracker.h"
#if H420X_PROFILE == 3
#include "pen.h"
#endif
#if H420X_PROFILE == 4 && SENSING_TELEMETRY
#error "telemetry is not supported on the rhythm1000 profile"
#endif
#if LOOP_MODES && (H420X_PROFILE != 2 || SENSING_TELEMETRY)
#error "-Modes / -LoopMode need the rhythm900 profile without telemetry"
#endif
#if LOOP_WARM && !(LOOP_MODES && defined(LOOP_MODE_FIXED))
#error "-LoopWarm needs -LoopMode <n>"
#endif
#if LOOP_ARM && !(LOOP_MODES && defined(LOOP_MODE_FIXED))
#error "-LoopArm needs -LoopMode <n>"
#endif
#if LOOP_ARM && LOOP_WARM
#error "use -LoopArm or -LoopWarm, not both"
#endif

#define ACQUIRE_MIN_PEAK 150U
/* Presence gate on the 4-slot window rise. Hardware (0.5.1 telemetry): with
 * no pen the rise has median ~6 (p99 ~100); a weak pen (peak 320-450) has
 * median 86-150, a strong one 400+. A failed frame only withholds its report;
 * tracking continues as in 0.5.1. Reports resume after PRESENT_ARM_FRAMES
 * good frames in a row, and ABSENT_SEARCH_FRAMES failed frames in a row send
 * the firmware back to the full search. */
#if ROBUST_LOCK
/* 0.6.4. With the gate off (0.6.1-0.6.3) every search "found" a pen: a noise
 * peak was locked, and the bin sweep at that noise coil picked a random
 * excitation bin, after which a real pen at the right place read weakly and
 * could not be found again. Now: a pen must rise ACQUIRE_MIN_RISE above the
 * median coil, the same coil (+-1) must win two searches in a row, and after
 * ACQUIRE_LOOSE_PASSES searches without a pen the rise gate is lowered to
 * ACQUIRE_LOOSE_RISE (still with the two-search confirmation) in case the
 * gate is too strict for a hovering pen. 0.6.5: it was 0 in 0.6.4, which let
 * the noise of an empty tablet (rise <= ~5 counts) lock about twice a second
 * and flash the LED; a lifted pen at 40..110 counts still clears 25. The LED
 * now lights only once reports are flowing (PRESENT_ARM_FRAMES good frames). */
#define ACQUIRE_MIN_RISE 90U
#define ACQUIRE_LOOSE_RISE 25U
#define ACQUIRE_LOOSE_PASSES 40U
#define INVALID_DROP_FRAMES 30U  /* flat or weak windows in a row before the lock is dropped */
#else
#define ACQUIRE_MIN_RISE 0U    /* 0.6.1: off (no hardware search data) */
#endif
#define TRACK_MIN_RISE   60U   /* window max(slots 1..3) above min(slots 0..3) */
#define PRESENT_ARM_FRAMES   3U
#define ABSENT_SEARCH_FRAMES 50U

/* Rise of a 4-slot window: max of the used slots minus min of all slots. */
static uint16_t window_rise(const uint16_t a[4]) {
  uint16_t hi = a[1];
  if (a[2] > hi) hi = a[2];
  if (a[3] > hi) hi = a[3];
  uint16_t lo = a[0];
  if (a[1] < lo) lo = a[1];
  if (a[2] < lo) lo = a[2];
  if (a[3] < lo) lo = a[3];
  return (uint16_t)(hi - lo);
}

/* Search bin per attempt; 0 means "the current bin". */
#if ROBUST_LOCK
#define SEARCH_BIN_COUNT 8U
static const uint8_t search_bins[SEARCH_BIN_COUNT] = { 0U, 9U, 5U, 12U, 2U, 7U, 3U, 10U };
#else
#define SEARCH_BIN_COUNT 4U
static const uint8_t search_bins[SEARCH_BIN_COUNT] = { 0U, 9U, 5U, 12U };
#endif

#if ROBUST_LOCK
/* Coil numbers a and b are the same or neighbours. */
static uint8_t coil_near(uint8_t a, uint8_t b) {
  return (uint8_t)(((a > b) ? (a - b) : (b - a)) <= 1);
}
#endif

/* Keep USB alive during the full-coil acquisition searches.
 * This runs after each read, outside the sensing critical section. */
static void service_usb(void) {
  tud_task();
  report_task();
  uart_poll();
}

#if LOOP_MODES
/* Round 26 loop modes (build.ps1 -Modes; -LoopMode <n> keeps one mode).
 * Round 15 (hardware, 2026-11-05): with the same absolute phase in every bin,
 * bin-9 frames were still about twice as good as bin-8 frames (A15: 2.5 vs
 * 7.9), so the bin itself matters, and the images lost the pen about a second
 * after the first pen-down. Round 16: Bnn modes pin the bin their cadence
 * frames read at (acquisition and bin maintenance still track the pen's bin,
 * HOOK reads at it); Free modes read at the tracked bin. All cadence modes:
 * 8 reads, 28 pulses, one conversion, absolute phase Ann (read period mod
 * excitation period = nn/60 of the period).
 * Round 16 (hardware): bin 9 held = 2.9 / 2.8 at 997 Hz once running, but
 * every image still lost the pen about a second after the first pen-down.
 * Round 17: the reason and length of each lost lock go to the next lock's
 * HOOK report; RC modes also read re-centering passes as cadence frames.
 * Round 18: -Diag18 (device clock, USB counters, heartbeat); no image died.
 * Round 19: Tnnn modes = B9-A17 with cadence frames excited at period nnn
 * cycles (bin 9's is 139); re-centering passes and HOOK still use the bin's
 * period.
 * Round 19 (hardware): the resonance is sharp at 139 (T139 2.9 / 1.7, T137 and
 * T140 about twice as bad), so the period stays 139.
 * Round 20: every mode is T139; Dnnn = PA2 low (integration start) nnn cycles
 * after PA3 fall instead of 756 (the dummy conversion moves with it), Innnn =
 * conversion nnnn cycles after PA2 low instead of 2544, P30 = 30 pulses.
 * Round 20 (hardware): what counts is how late the conversion is, not where
 * integration starts (D468-I2832 = REF; I2832 best in X but over 1 ms).
 * Round 21: all modes PA2 low 468 cycles after PA3 fall; Cnnnn = conversion
 * nnnn cycles after PA3 fall (PA2 low + conversion delay); R7 = no Y
 * preparation read (7 reads per frame); PADnnn = nnn idle cycles added to the
 * read period; P32 = 32 pulses. Modes 5-7 share one read period (9213 cycles).
 * Round 21 (hardware): frames shorter than 1 ms let the host skip frames,
 * which shows as speed-dependent jitter (R7 at 847 us frames: X 4.9 slow,
 * 11.9 fast). Round 22: -F = frame period floor 1004 us (every frame reaches
 * the host once, a hole every ~250 ms); modes 4-7 sweep the conversion time
 * with 7 reads, mode 7 = 32 pulses at mode 4's conversion time.
 * Round 22 (hardware): floor works; fixed R7-C4581-F 2.0 / 1.5 at 974.7 Hz,
 * but bin maintenance every 128 frames still costs ~2 % host holes. Round 23:
 * mode 1 is that image exactly (floor 1004 us, maintenance on); modes 2-7 skip
 * bin maintenance (the read bin is pinned at 9 anyway) and use a 1002 us floor,
 * except mode 7 which has no floor (floor control). Modes 2/3 share read period
 * 9630 and 4-6 share 9769, so pulses vs conversion time is compared at one
 * frame period.
 * Round 23 (hardware): fixed P32-C4164 1.5 / 1.4 at 978.8 Hz is the best moving
 * jitter; the fixed builds stay work-limited at ~1022 us frames. The still-pen
 * trial (bench steady condition) exposed the real remaining gap: P32-C4164 holds
 * 0.073 / 0.043 mm RMS with discrete X states of about 17 and 51 units, while the
 * released rhythm900 holds 0.013 / 0.017 mm and has no such states. Round 24 keeps
 * the P32-C4164 read timing in every mode and changes only the tracker ownership
 * hysteresis (1/32, 1/16, 1/8, 1/4) and the online slot-gain learner (on/off), so
 * the steady trial separates them. The learner is the `e[]` correction that
 * compensates a center handoff; a still pen can toggle the center and make the
 * integer correction walk in ~17-unit steps.
 * Round 25 (hardware, 2026-11-05): the released rhythm900 at the same still-pen
 * position holds 0.012 / 0.027 mm while the cleanroom log fit holds ~0.10 mm X;
 * the stock decompile reconstructs position with a three-point linear
 * interpolation of the coil amplitudes, not the log-domain fit. Mode 1 switches
 * to that linear fit with the current 1/32 hysteresis and learner off; the other
 * modes stay as round 24 controls.
 * Round 25 (hardware): the linear-only fit is visually stair-stepped and its moving
 * jitter is worse (2.2 / 2.5 against 1.7 / 1.6 for the log fit), so it is dropped.
 * Round 26: the log fit is smooth but jumps at coil boundaries, and the online
 * learner walks. This round tests a fixed per-axis slot-gain correction e (the
 * calibration the learner tries to find) with the learner off: e_x = 0, +4, +8,
 * +16, -4, -8, -16, e_y = 0. All modes are P32-C4164, log fit, 1/32 hysteresis.
 * A lock starts in HOOK; after the lock's first published report the image
 * switches to the rotation mode (-Modes: modes 1..7, LOOP_BLOCK published
 * frames each; every new lock starts the next mode with a fresh block) or to
 * LOOP_MODE_FIXED (-LoopArm). Each mode keeps its own tracker.e[]
 * (loop_switch). The search and bin sweep always use the plain read. Every
 * mode except HOOK: interrupts masked and no service hook during the frame,
 * DWELL, 1/32 hysteresis, free-running, no extra between frames.
 *   0 HOOK            plain rhythm900 loop: 29 pulses, USB service after every read
 *   1 E0              P32-C4164, log fit, 1/32, learner off, e_x 0 / e_y 0 (reference)
 *   2 EP4             as 1, e_x +4
 *   3 EP8             as 1, e_x +8
 *   4 EP16            as 1, e_x +16
 *   5 EN4             as 1, e_x -4
 *   6 EN8             as 1, e_x -8
 *   7 EN16            as 1, e_x -16
 * A multi-pass frame re-reads the moved axis with sensing_read_window() at the
 * same bin as the frame's first pass.
 * Aux bits (wire bytes 10-11):
 *   0-5   cadence modes: lateness of the frame's first read in us (how far the
 *         work between frames ran past one read period), saturating at 63.
 *         HOOK: bits 0-2 why the previous lock ended (0 none yet, 1 invalid
 *         run, 2 lost (other), 3 absent, 4 center out of range, 5 cadence
 *         read failed, 6 window read failed, 7 three re-centering passes),
 *         bits 3-5 its published frames (0 <16, 1 <64, 2 <256, 3 <1024,
 *         4 <4096, 5 <16384, 6 more)
 *   6-9   excitation bin this frame was read at (1..12)
 *   10    more than one pass
 *   11    bin maintenance ran after the previous published frame
 *   12    cadence modes: a read after the frame's first started late; HOOK:
 *         more than one lock ended since the previous published report
 *   13-15 mode */
#define LOOP_MODE_COUNT       8U
#define LOOP_BLOCK            256U
#define LOOP_HYST_NORMAL      5U                              /* tracker default: 1/32 */
#define LOOP_SOF_PERIOD_CYC   (CPU_HZ / 1000UL)               /* one USB frame: 72000 */
#define LOOP_SOF_GUARD_CYC    (40UL * (CPU_HZ / 1000000UL))   /* no USB work in the last 40 us before the predicted edge */
#ifndef SOF_LEAD_US
#define SOF_LEAD_US 10UL   /* build.ps1 -SofLeadUs <us> (round 53) */
#endif
#define LOOP_SOF_LEAD_CYC     (SOF_LEAD_US * (CPU_HZ / 1000000UL))   /* first burst SOF_LEAD_US after the edge */
#define LOOP_SOF_TIMEOUT_CYC  (1100UL * (CPU_HZ / 1000000UL)) /* no edge: start the frame anyway */
#ifndef IN_EPS_US
#define IN_EPS_US 10UL   /* build.ps1 -InEpsUs <us> (round 54) */
#endif
#define LOOP_IN_EPS_CYC       (IN_EPS_US * (CPU_HZ / 1000000UL))   /* IN: first burst IN_EPS_US after the IN completion */
#define LOOP_IN_TIMEOUT_CYC   (1100UL * (CPU_HZ / 1000000UL)) /* IN: no completion: start the frame anyway */
#define LOOP_MAINT_FRAMES     128U                            /* bin maintenance interval, HOOK / FREE */
#define LOOP_MAINT_FRAMES_SOF 1024U                           /* SOF / IN: a maintenance costs one USB frame */
#define LOOP_LOCK_FREE        0U
#define LOOP_LOCK_SOF         1U
#define LOOP_LOCK_IN          2U

typedef struct {
  uint8_t pulses;       /* excitation pulses per burst */
  uint8_t lock;         /* LOOP_LOCK_FREE / LOOP_LOCK_SOF / LOOP_LOCK_IN */
  uint8_t nconv;        /* used conversions per read: 3 (median) or 1 */
  uint8_t skip_prep_y;  /* 1 = no Y slot-0 preparation read */
  uint8_t skip_prep_x;  /* 1 = no X slot-0 preparation read */
  uint8_t gap_us;       /* FREE: next frame's first burst this long after the last read; 0 = none */
  uint8_t cadence;      /* 1 = the first pass reads the frame with sensing_read_frame() */
  uint8_t cad_phase;    /* cadence read-period offset, sixtieths of the excitation period */
  uint8_t cad_abs;      /* 1 = absolute cadence phase (round 15) */
  uint8_t cad_bin;      /* 0 = read at the tracked bin, else cadence frames read at this bin (round 16) */
  uint8_t cad_rc;       /* 1 = re-centering passes also read with sensing_read_frame() (round 17) */
  uint8_t cad_period;   /* 0 = the bin's excitation period, else cadence frames use this period in cycles (round 19) */
  uint16_t adc1;        /* 0 = the profile's PA2 low -> conversion delay, else this many cycles (round 20) */
  uint16_t pa2_low;     /* 0 = PA3 fall -> PA2 low 756 cycles, else this many cycles (round 20) */
  uint16_t cad_pad;     /* idle cycles added to the cadence read period (round 21) */
  uint16_t frame_us;    /* 0 = no floor, else cadence frames start at least this many us apart (round 22) */
  uint8_t maint_off;    /* 1 = no bin maintenance while this mode is active (round 23) */
  uint8_t hyst;         /* ownership hysteresis shift, 1/2^hyst (2..8; 5 = 1/32) (round 24) */
  uint8_t learn;        /* 1 = online slot-gain learner on, 0 = off (round 24) */
  uint8_t fit;          /* 0 = log-domain fit, 1 = stock-style three-point linear fit (round 25) */
  int16_t e_x;          /* fixed X slot-gain correction (round 26) */
  int16_t e_y;          /* fixed Y slot-gain correction (round 26) */
  uint8_t sampt;        /* ADC sample-time code (round 35; 0 = 13.5 cycles) */
#if PHASE_MODES
  uint8_t align;        /* round 42: 0..11 = cadence frame start forced to DWT mod 12 == align, 255 = unaligned */
#endif
#if ORDER_MODES || SETTLE_MODES
  uint8_t order;        /* round 44: bit 0 = Y first, bit 1 = reverse X, bit 2 = reverse Y */
  uint16_t gap_cyc;     /* round 44: extra idle cycles before the first burst of every frame */
#endif
#if AXIS_GAP_MODES
  uint16_t gap_y;       /* round 50: extra Y read gap on top of gap_cyc */
#endif
#if R55_MODES
  uint16_t r55_align;   /* round 55: first read on a DWT grid of this many cycles (12 or 720; R57 24/72/144; R58 834/139 relative), 0 = off */
  uint8_t r55_exact;    /* round 55: 1 = cycle-exact burst start, PA2 low and conversion start */
#endif
#if R56_MODES
  uint16_t r56_lead;    /* round 56: first read this many cycles after ASAP (keeps the used reads in the control's slots), 0 = ASAP */
#endif
} loop_mode_def;

/* Candidate-only overrides for the fixed mode-1 row. Defaults reproduce the
 * current 1015 control byte for byte. build.ps1 -Pa2Cyc / -SofLock set them. */
#ifndef PA2_MODE1_CYC
#define PA2_MODE1_CYC 468U
#endif
#if defined(MODE1_SOF) && defined(MODE1_IN)
#error "-SofLock and -InLock cannot be combined"
#endif
#ifdef MODE1_SOF
#define MODE1_LOCK LOOP_LOCK_SOF
#elif defined(MODE1_IN)
#define MODE1_LOCK LOOP_LOCK_IN   /* round 54 -InLock */
#else
#define MODE1_LOCK LOOP_LOCK_FREE
#endif

#if R59_MODES
/* Round 59: the pulse ladder. Every mode is round 58 mode 3 (DW R139: IN lock,
 * cycle-exact landings, the Y prep read started on the DWELL path with
 * r56_lead 250, the relative 139-cycle first-read grid; S41.5, one conversion,
 * pa2 468, X387 / Y387 read gap, bin 9, learner off) with fewer excitation
 * pulses. Each 2 pulses removed (278 cycles) move adc1 278 cycles later, so the
 * conversion instant, the read period (9630 cycles) and the read slack do not
 * change, and the phase relative to the excitation is kept (278 = 2 x 139).
 * Round 58 at the same conversion instant gave Y 2.95 / 2.57 / 2.38 for
 * 36 / 32 / 28 pulses; this finds where that trend turns over.
 *   1 32p adc1 3500 (control)   2 30p adc1 3778   3 28p adc1 4056
 *   4 26p adc1 4334             5 24p adc1 4612   6 22p adc1 4890
 *   7 20p adc1 5168
 * Aux as round 55 (bits 0-5 IN wait us, 6-9 landing spread, 13-15 mode). */
static const loop_mode_def loop_defs[LOOP_MODE_COUNT] = {
  /* pulses lock          nconv skipY skipX gap  cad  phase abs  bin  rc  period adc1    pa2   pad    frame  maint hyst learn fit  ex   ey   sampt            order gap    gapy  align exact lead */
  { 29U, LOOP_LOCK_FREE, 3U, 0U, 0U,  0U, 0U,  0U, 0U,  0U, 0U,   0U,     0U,    0U,     0U,     0U,   0U,  5U,  1U,  0U,  0,   0,  ADC_SAMPT_13_5,   0U,    0U,    0U,    0U, 0U,    0U },   /* 0 HOOK            */
  { 32U, LOOP_LOCK_IN,   1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3500U,  468U,      0U,   900U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  1U,  337U,    0U,  139U, 1U,  250U },   /* 1 32p (ctrl)      */
  { 30U, LOOP_LOCK_IN,   1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3778U,  468U,      0U,   900U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  1U,  337U,    0U,  139U, 1U,  250U },   /* 2 30p             */
  { 28U, LOOP_LOCK_IN,   1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  4056U,  468U,      0U,   900U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  1U,  337U,    0U,  139U, 1U,  250U },   /* 3 28p             */
  { 26U, LOOP_LOCK_IN,   1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  4334U,  468U,      0U,   900U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  1U,  337U,    0U,  139U, 1U,  250U },   /* 4 26p             */
  { 24U, LOOP_LOCK_IN,   1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  4612U,  468U,      0U,   900U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  1U,  337U,    0U,  139U, 1U,  250U },   /* 5 24p             */
  { 22U, LOOP_LOCK_IN,   1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  4890U,  468U,      0U,   900U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  1U,  337U,    0U,  139U, 1U,  250U },   /* 6 22p             */
  { 20U, LOOP_LOCK_IN,   1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  5168U,  468U,      0U,   900U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  1U,  337U,    0U,  139U, 1U,  250U },   /* 7 20p             */
};
#elif R58_MODES
/* Round 58: every mode is round 57 mode 3 (DW A144: IN lock, cycle-exact
 * landings, the Y prep read started on the DWELL path with r56_lead 250, the
 * 144-cycle first-read grid; S41.5, 32 pulses, one conversion, adc3500 /
 * pa2 468, X387 / Y387 read gap, bin 9, learner off) except for what the row
 * changes. Every row keeps the read period at 9630 cycles and the conversion
 * end where mode 1 has it, so no read can run late.
 * Round 57 showed that a frame whose start crosses a grid step (host drift
 * ~9.35 cycles per frame, so drift / m of the frames) starts m cycles later,
 * which turns the pen's residual ring at the prep by (m mod 139) / 139 of a
 * period and moves Y. Relative grids make that step a multiple of 139:
 *   1 DW A144 (control = release candidate 3)
 *   2 DW R834          steps of 6 x 139: ring phase and ADC clock phase kept
 *   3 DW R139          steps of 139: ring phase kept, ADC clock phase +1 cycle per step
 *   4 S71.5 same-hold  sample time 71.5, the window starts 180 cycles earlier
 *                      (adc1 3320, pad 180): same hold instant and conversion end
 *   5 S239.5 same-hold sample time 239.5, starts 1188 cycles earlier (adc1 2312, pad 1188)
 *   6 28p late-conv    28 pulses, adc1 4056: same conversion instant, 556 cycles more integration
 *   7 36p early-conv   36 pulses, adc1 2944: same conversion instant, 556 cycles less integration
 * Aux as round 55 (bits 0-5 IN wait us, 6-9 landing spread, 13-15 mode). */
static const loop_mode_def loop_defs[LOOP_MODE_COUNT] = {
  /* pulses lock          nconv skipY skipX gap  cad  phase abs  bin  rc  period adc1    pa2   pad    frame  maint hyst learn fit  ex   ey   sampt            order gap    gapy  align exact lead */
  { 29U, LOOP_LOCK_FREE, 3U, 0U, 0U,  0U, 0U,  0U, 0U,  0U, 0U,   0U,     0U,    0U,     0U,     0U,   0U,  5U,  1U,  0U,  0,   0,  ADC_SAMPT_13_5,   0U,    0U,    0U,    0U, 0U,    0U },   /* 0 HOOK            */
  { 32U, LOOP_LOCK_IN,   1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3500U,  468U,      0U,   900U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  1U,  337U,    0U,  144U, 1U,  250U },   /* 1 DW A144 (ctrl)  */
  { 32U, LOOP_LOCK_IN,   1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3500U,  468U,      0U,   900U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  1U,  337U,    0U,  834U, 1U,  250U },   /* 2 DW R834         */
  { 32U, LOOP_LOCK_IN,   1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3500U,  468U,      0U,   900U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  1U,  337U,    0U,  139U, 1U,  250U },   /* 3 DW R139         */
  { 32U, LOOP_LOCK_IN,   1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3320U,  468U,    180U,   900U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_71_5,  1U,  337U,    0U,  144U, 1U,  250U },   /* 4 S71.5 same-hold */
  { 32U, LOOP_LOCK_IN,   1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  2312U,  468U,   1188U,   900U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_239_5, 1U,  337U,    0U,  144U, 1U,  250U },   /* 5 S239.5 same-hld */
  { 28U, LOOP_LOCK_IN,   1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  4056U,  468U,      0U,   900U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  1U,  337U,    0U,  144U, 1U,  250U },   /* 6 28p late-conv   */
  { 36U, LOOP_LOCK_IN,   1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  2944U,  468U,      0U,   900U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  1U,  337U,    0U,  144U, 1U,  250U },   /* 7 36p early-conv  */
};
#elif R57_MODES
/* Round 57: every mode is round 55 mode 5 (IN lock, cycle-exact landings, the
 * Y prep read kept; S41.5, 32 pulses, one conversion, adc3500 / pa2 468,
 * X387 / Y387 read gap, bin 9, learner off) except for what the row changes.
 * Round 56 showed that mode 5's prep read starts on the late path: its TX mux
 * is selected 168 + (A720 delay, 0..719) cycles before the burst, and that
 * variable settle moves Y1 (about 1.5 units of middle-row Y RMS). A non-zero
 * r56_lead starts the frame on the DWELL path instead: the grid alignment is
 * applied first, then the TX mux is selected a constant LOOP_DWELL_LEAD_CYC
 * before the burst. The lead must exceed the ~100 cycles of code between the
 * DWT read in sensing_read_frame() and the pre-wait check in cadence_read(),
 * or the pre-wait is skipped and the settle varies again: lead 250 (3.5 us).
 *   1 control (round 55 mode 5, late-path prep, A720)
 *   2 DW A720       DWELL-start prep, 720-cycle grid
 *   3 DW A144       144-cycle grid (A720 delay 0..2 us instead of 0..10)
 *   4 DW A72        72-cycle grid
 *   5 DW A24        24-cycle grid
 *   6 DW A72 L700   lead 700: the prep 450 cycles (6.25 us) later than mode 4
 *   7 NP            round 56 mode 2: no prep, same slots (still-pen reference)
 * Aux as round 55 (bits 0-5 IN wait us, 6-9 landing spread, 13-15 mode). */
static const loop_mode_def loop_defs[LOOP_MODE_COUNT] = {
  /* pulses lock          nconv skipY skipX gap  cad  phase abs  bin  rc  period adc1    pa2   pad    frame  maint hyst learn fit  ex   ey   sampt            order gap    gapy  align exact lead */
  { 29U, LOOP_LOCK_FREE, 3U, 0U, 0U,  0U, 0U,  0U, 0U,  0U, 0U,   0U,     0U,    0U,     0U,     0U,   0U,  5U,  1U,  0U,  0,   0,  ADC_SAMPT_13_5,   0U,    0U,    0U,    0U, 0U,    0U },   /* 0 HOOK            */
  { 32U, LOOP_LOCK_IN,   1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3500U,  468U,     0U,   900U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,   1U,  337U,    0U,  720U, 1U,    0U },   /* 1 control (R55 5) */
  { 32U, LOOP_LOCK_IN,   1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3500U,  468U,     0U,   900U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,   1U,  337U,    0U,  720U, 1U,  250U },   /* 2 DW A720         */
  { 32U, LOOP_LOCK_IN,   1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3500U,  468U,     0U,   900U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,   1U,  337U,    0U,  144U, 1U,  250U },   /* 3 DW A144         */
  { 32U, LOOP_LOCK_IN,   1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3500U,  468U,     0U,   900U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,   1U,  337U,    0U,   72U, 1U,  250U },   /* 4 DW A72          */
  { 32U, LOOP_LOCK_IN,   1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3500U,  468U,     0U,   900U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,   1U,  337U,    0U,   24U, 1U,  250U },   /* 5 DW A24          */
  { 32U, LOOP_LOCK_IN,   1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3500U,  468U,     0U,   900U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,   1U,  337U,    0U,   72U, 1U,  700U },   /* 6 DW A72 L700     */
  { 32U, LOOP_LOCK_IN,   1U, 1U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3500U,  468U,     0U,   900U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,   1U,  337U,    0U,  720U, 1U, 9330U },   /* 7 NP (R56 2)      */
};
#elif R56_MODES
/* Round 56: every mode is round 55 mode 5 (IN lock, first read on the 720-cycle
 * grid, cycle-exact landings; S41.5, 32 pulses, one conversion, adc3500 /
 * pa2 468, X387 / Y387 read gap, bin 9, learner off) except for what the row
 * changes. Modes 2-7 drop the discarded Y prep read and spend its time:
 *   1 control (round 55 mode 5, with the Y prep read)
 *   2 NP            no Y prep, same read slots as 1 (lead = one read)
 *   3 NP adc4250    PA2 low -> conversion 4250 cycles (59.0 us, 1 = 48.6 us)
 *   4 NP adc5000    5000 cycles (69.4 us)
 *   5 NP 40p        40 excitation pulses
 *   6 NP S239.5     ADC sample time 239.5 (pad 1200 keeps the read gap)
 *   7 NP 36p adc4000
 * r56_lead puts each mode's last conversion where mode 1 has it (minus the
 * 168 + code cycles of mode 1's late-path start), so every mode reports from
 * the end of the frame and keeps mode 1's IN-wait margin. Longer reads only
 * make the earliest reads older (mean sample age Y +39..+78 us, X +10..+19 us; development/cleanroom_rounds/r56_model.py).
 * Aux as round 55 (bits 0-5 IN wait us, 6-9 landing spread, 13-15 mode). */
static const loop_mode_def loop_defs[LOOP_MODE_COUNT] = {
  /* pulses lock          nconv skipY skipX gap  cad  phase abs  bin  rc  period adc1    pa2   pad    frame  maint hyst learn fit  ex   ey   sampt            order gap    gapy  align exact lead */
  { 29U, LOOP_LOCK_FREE, 3U, 0U, 0U,  0U, 0U,  0U, 0U,  0U, 0U,   0U,     0U,    0U,     0U,     0U,   0U,  5U,  1U,  0U,  0,   0,  ADC_SAMPT_13_5,   0U,    0U,    0U,    0U, 0U,    0U },   /* 0 HOOK            */
  { 32U, LOOP_LOCK_IN,   1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3500U,  468U,     0U,   900U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,   1U,  337U,    0U,  720U, 1U,    0U },   /* 1 control (R55 5) */
  { 32U, LOOP_LOCK_IN,   1U, 1U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3500U,  468U,     0U,   900U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,   1U,  337U,    0U,  720U, 1U, 9330U },   /* 2 NP              */
  { 32U, LOOP_LOCK_IN,   1U, 1U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  4250U,  468U,     0U,   900U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,   1U,  337U,    0U,  720U, 1U, 5105U },   /* 3 NP adc4250      */
  { 32U, LOOP_LOCK_IN,   1U, 1U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  5000U,  468U,     0U,   900U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,   1U,  337U,    0U,  720U, 1U,  880U },   /* 4 NP adc5000      */
  { 40U, LOOP_LOCK_IN,   1U, 1U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3500U,  468U,     0U,   900U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,   1U,  337U,    0U,  720U, 1U, 2658U },   /* 5 NP 40p          */
  { 32U, LOOP_LOCK_IN,   1U, 1U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3500U,  468U,  1200U,   900U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_239_5,  1U,  337U,    0U,  720U, 1U, 2582U },   /* 6 NP S239.5       */
  { 36U, LOOP_LOCK_IN,   1U, 1U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  4000U,  468U,     0U,   900U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,   1U,  337U,    0U,  720U, 1U, 3409U },   /* 7 NP 36p adc4000  */
};
#elif R55_MODES
/* Round 55: every mode is the round 54 G read (S41.5, 32 pulses, one
 * conversion, Y-first with the Y prep read, adc3500 / pa2 468, X387 / Y387
 * read gap, bin 9, learner off; build with -ReadGap 50 -FastEp1). Modes 1-5
 * start each frame on the host's IN poll (G); modes 6-7 are free-running with
 * the 990 us floor (C). The two mechanisms (see sensing.c):
 *   A12 / A720: the frame's first read on a 12 / 720-cycle DWT grid
 *   exact:      cycle-exact burst start, PA2 low and conversion start
 *   1 G control      IN               5 IN A720 exact
 *   2 IN A12         IN               6 C control (free 990)
 *   3 IN A720        IN               7 C A720 exact (free 990)
 *   4 IN exact       IN
 * Aux (wire bytes 10-11): bits 0-5 IN wait in us (IN modes) or the first
 * read's lateness in us (free), saturating 63; bits 6-9 landing spread in
 * cycles (0 = exact), saturating 15; bit 10 more than one pass; bit 11 the
 * frame did not start on an IN completion; bit 12 a later read started late;
 * bits 13-15 mode. */
static const loop_mode_def loop_defs[LOOP_MODE_COUNT] = {
  /* pulses lock          nconv skipY skipX gap  cad  phase abs  bin  rc  period adc1    pa2   pad  frame  maint hyst learn fit  ex   ey   sampt            order gap    gapy  align exact */
  { 29U, LOOP_LOCK_FREE, 3U, 0U, 0U,  0U, 0U,  0U, 0U,  0U, 0U,   0U,     0U,    0U,    0U,     0U,   0U,  5U,  1U,  0U,  0,   0,  ADC_SAMPT_13_5,  0U,    0U,    0U,    0U, 0U },   /* 0 HOOK */
  { 32U, LOOP_LOCK_IN,   1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3500U,  468U,    0U,   900U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  1U,  337U,    0U,    0U, 0U },   /* 1 G control       */
  { 32U, LOOP_LOCK_IN,   1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3500U,  468U,    0U,   900U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  1U,  337U,    0U,   12U, 0U },   /* 2 IN A12          */
  { 32U, LOOP_LOCK_IN,   1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3500U,  468U,    0U,   900U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  1U,  337U,    0U,  720U, 0U },   /* 3 IN A720         */
  { 32U, LOOP_LOCK_IN,   1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3500U,  468U,    0U,   900U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  1U,  337U,    0U,    0U, 1U },   /* 4 IN exact        */
  { 32U, LOOP_LOCK_IN,   1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3500U,  468U,    0U,   900U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  1U,  337U,    0U,  720U, 1U },   /* 5 IN A720 exact   */
  { 32U, LOOP_LOCK_FREE, 1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3500U,  468U,    0U,   990U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  1U,  337U,    0U,    0U, 0U },   /* 6 C control       */
  { 32U, LOOP_LOCK_FREE, 1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3500U,  468U,    0U,   990U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  1U,  337U,    0U,  720U, 1U },   /* 7 C A720 exact    */
};
#elif YPREP_MODES
/* Round 48: Y-prep 7R with the same 32 pulses but shorter ADC/conversion
 * delays. All modes are Y-first, Y prep kept, X prep dropped, no gap, 1000 us
 * floor. Modes 2-4 shorten adc1 (PA2 low -> conversion start); modes 5-7 also
 * shorten pa2_low (PA3 fall -> PA2 low). Goal: save ~27 us/frame without
 * cutting a read. */
static const loop_mode_def loop_defs[LOOP_MODE_COUNT] = {
  /* pulses lock          nconv skipY skipX gap  cad  phase abs  bin  rc  period adc1    pa2   pad  frame  maint hyst learn fit  ex   ey   sampt            order gap */
  { 29U, LOOP_LOCK_FREE, 3U, 0U, 0U,  0U, 0U,  0U, 0U,  0U, 0U,   0U,     0U,    0U,    0U,     0U,   0U,  5U,  1U,  0U,  0,   0,  ADC_SAMPT_13_5,  0U,    0U },   /* 0 HOOK          */
  { 32U, LOOP_LOCK_FREE, 1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1000U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  1U,    0U },   /* 1 adc3696 pa2 468 control */
  { 32U, LOOP_LOCK_FREE, 1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3500U,  468U,    0U,  1000U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  1U,    0U },   /* 2 adc3500 pa2 468 */
  { 32U, LOOP_LOCK_FREE, 1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3300U,  468U,    0U,  1000U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  1U,    0U },   /* 3 adc3300 pa2 468 */
  { 32U, LOOP_LOCK_FREE, 1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3100U,  468U,    0U,  1000U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  1U,    0U },   /* 4 adc3100 pa2 468 */
  { 32U, LOOP_LOCK_FREE, 1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  400U,    0U,  1000U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  1U,    0U },   /* 5 adc3696 pa2 400 */
  { 32U, LOOP_LOCK_FREE, 1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3500U,  400U,    0U,  1000U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  1U,    0U },   /* 6 adc3500 pa2 400 */
  { 32U, LOOP_LOCK_FREE, 1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3300U,  400U,    0U,  1000U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  1U,    0U },   /* 7 adc3300 pa2 400 */
};
#elif AXIS_GAP_MODES
/* Round 50: per-axis read-gap sweep on the round-48 winner. Build with
 * -ReadGap 50, so gap_cyc is the extra X gap and gap_y the extra Y gap on
 * top of it. The frame's cadence schedule uses a separate period for X and Y
 * reads; this tests whether the short-global-gap Y state needs a longer Y
 * settle while X stays fast. Modes: true 387/387 control, then X50 with Y
 * gap 50/150/250/387, then the same at S71.5. All 32 pulses, adc3500/pa2 468,
 * Y-prep 7R, 1000 us floor. */
static const loop_mode_def loop_defs[LOOP_MODE_COUNT] = {
  /* pulses lock          nconv skipY skipX gap  cad  phase abs  bin  rc  period adc1    pa2   pad  frame  maint hyst learn fit  ex   ey   sampt            order gap */
  { 29U, LOOP_LOCK_FREE, 3U, 0U, 0U,  0U, 0U,  0U, 0U,  0U, 0U,   0U,     0U,    0U,    0U,     0U,   0U,  5U,  1U,  0U,  0,   0,  ADC_SAMPT_13_5,  0U,    0U,    0U },   /* 0 HOOK */
  { 32U, MODE1_LOCK, 1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3500U,  PA2_MODE1_CYC,    0U,  1000U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  1U,  337U,    0U },   /* 1 S41.5 X387 Y387 control */
  { 32U, LOOP_LOCK_FREE, 1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3500U,  468U,    0U,  1000U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  1U,    0U,    0U },   /* 2 S41.5 X50 Y50          */
  { 32U, LOOP_LOCK_FREE, 1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3500U,  468U,    0U,  1000U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  1U,    0U,  100U },   /* 3 S41.5 X50 Y150         */
  { 32U, LOOP_LOCK_FREE, 1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3500U,  468U,    0U,  1000U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  1U,    0U,  200U },   /* 4 S41.5 X50 Y250         */
  { 32U, LOOP_LOCK_FREE, 1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3500U,  468U,    0U,  1000U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  1U,    0U,  337U },   /* 5 S41.5 X50 Y387         */
  { 32U, LOOP_LOCK_FREE, 1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3500U,  468U,    0U,  1000U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_71_5,  1U,    0U,  100U },   /* 6 S71.5 X50 Y150         */
  { 32U, LOOP_LOCK_FREE, 1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3500U,  468U,    0U,  1000U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_71_5,  1U,    0U,  200U },   /* 7 S71.5 X50 Y250         */
};
#elif GAP_MODES
/* Round 49A: read-to-read gap sweep on the round-48 winner. Build with
 * -ReadGap 50, so mode 1 (extra 337) is the 387-cycle control and mode 7
 * (extra 0) is the 50-cycle target. All modes: adc3500 / pa2 468, S41.5,
 * 32 pulses, Y-prep 7R, 1000 us floor. Expected rate 968.5 -> ~1000 Hz. */
static const loop_mode_def loop_defs[LOOP_MODE_COUNT] = {
  /* pulses lock          nconv skipY skipX gap  cad  phase abs  bin  rc  period adc1    pa2   pad  frame  maint hyst learn fit  ex   ey   sampt            order gap */
  { 29U, LOOP_LOCK_FREE, 3U, 0U, 0U,  0U, 0U,  0U, 0U,  0U, 0U,   0U,     0U,    0U,    0U,     0U,   0U,  5U,  1U,  0U,  0,   0,  ADC_SAMPT_13_5,  0U,    0U },   /* 0 HOOK          */
  { 32U, LOOP_LOCK_FREE, 1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3500U,  468U,    0U,  1000U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  1U,  337U },   /* 1 gap 387 control */
  { 32U, LOOP_LOCK_FREE, 1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3500U,  468U,    0U,  1000U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  1U,  287U },   /* 2 gap 337      */
  { 32U, LOOP_LOCK_FREE, 1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3500U,  468U,    0U,  1000U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  1U,  237U },   /* 3 gap 287      */
  { 32U, LOOP_LOCK_FREE, 1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3500U,  468U,    0U,  1000U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  1U,  187U },   /* 4 gap 237      */
  { 32U, LOOP_LOCK_FREE, 1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3500U,  468U,    0U,  1000U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  1U,  137U },   /* 5 gap 187      */
  { 32U, LOOP_LOCK_FREE, 1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3500U,  468U,    0U,  1000U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  1U,   50U },   /* 6 gap 100      */
  { 32U, LOOP_LOCK_FREE, 1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3500U,  468U,    0U,  1000U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  1U,    0U },   /* 7 gap 50       */
};
#elif CONV_MODES
/* Round 49B: conversion-budget / sample-window / median-of-3 rotation on the
 * round-48 winner. Build with -ConvBudget 350 (the measured worst case was
 * 335 cycles). Modes 1-5 sweep the ADC sample window with one conversion;
 * modes 6-7 use the same-frame median of three at S41.5/S71.5. All modes:
 * adc3500 / pa2 468, 32 pulses, Y-prep 7R, 387-cycle read gap, 1000 us floor. */
static const loop_mode_def loop_defs[LOOP_MODE_COUNT] = {
  /* pulses lock          nconv skipY skipX gap  cad  phase abs  bin  rc  period adc1    pa2   pad  frame  maint hyst learn fit  ex   ey   sampt            order gap */
  { 29U, LOOP_LOCK_FREE, 3U, 0U, 0U,  0U, 0U,  0U, 0U,  0U, 0U,   0U,     0U,    0U,    0U,     0U,   0U,  5U,  1U,  0U,  0,   0,  ADC_SAMPT_13_5,  0U,    0U },   /* 0 HOOK          */
  { 32U, LOOP_LOCK_FREE, 1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3500U,  468U,    0U,  1000U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  1U,    0U },   /* 1 S41.5 1conv   */
  { 32U, LOOP_LOCK_FREE, 1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3500U,  468U,    0U,  1000U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_28_5,  1U,    0U },   /* 2 S28.5 1conv   */
  { 32U, LOOP_LOCK_FREE, 1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3500U,  468U,    0U,  1000U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_71_5,  1U,    0U },   /* 3 S71.5 1conv   */
  { 32U, LOOP_LOCK_FREE, 1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3500U,  468U,    0U,  1000U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_239_5, 1U,    0U },   /* 4 S239.5 1conv  */
  { 32U, LOOP_LOCK_FREE, 1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3500U,  468U,    0U,  1000U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_13_5,  1U,    0U },   /* 5 S13.5 1conv   */
  { 32U, LOOP_LOCK_FREE, 3U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3500U,  468U,    0U,  1000U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  1U,    0U },   /* 6 S41.5 median3 */
  { 32U, LOOP_LOCK_FREE, 3U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3500U,  468U,    0U,  1000U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_71_5,  1U,    0U },   /* 7 S71.5 median3 */
};
#elif JITTER_MODES
/* Round 49C: longer sample window + shorter read gap. Build with -ReadGap 50
 * and -ConvBudget 350. Mode 1 (extra 337) is the S41.5 / 387-cycle control;
 * modes 2-5 keep S71.5 while the gap falls 337 -> 187; mode 6 tests S239.5 at
 * a 100-cycle gap; mode 7 is S71.5 at the 50-cycle target. All modes: adc3500
 * / pa2 468, 32 pulses, Y-prep 7R, 1000 us floor. */
static const loop_mode_def loop_defs[LOOP_MODE_COUNT] = {
  /* pulses lock          nconv skipY skipX gap  cad  phase abs  bin  rc  period adc1    pa2   pad  frame  maint hyst learn fit  ex   ey   sampt            order gap */
  { 29U, LOOP_LOCK_FREE, 3U, 0U, 0U,  0U, 0U,  0U, 0U,  0U, 0U,   0U,     0U,    0U,    0U,     0U,   0U,  5U,  1U,  0U,  0,   0,  ADC_SAMPT_13_5,  0U,    0U },   /* 0 HOOK          */
  { 32U, LOOP_LOCK_FREE, 1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3500U,  468U,    0U,  1000U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  1U,  337U },   /* 1 S41.5 gap 387 control */
  { 32U, LOOP_LOCK_FREE, 1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3500U,  468U,    0U,  1000U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_71_5,  1U,  287U },   /* 2 S71.5 gap 337 */
  { 32U, LOOP_LOCK_FREE, 1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3500U,  468U,    0U,  1000U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_71_5,  1U,  237U },   /* 3 S71.5 gap 287 */
  { 32U, LOOP_LOCK_FREE, 1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3500U,  468U,    0U,  1000U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_71_5,  1U,  187U },   /* 4 S71.5 gap 237 */
  { 32U, LOOP_LOCK_FREE, 1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3500U,  468U,    0U,  1000U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_71_5,  1U,  137U },   /* 5 S71.5 gap 187 */
  { 32U, LOOP_LOCK_FREE, 1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3500U,  468U,    0U,  1000U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_239_5, 1U,   50U },   /* 6 S239.5 gap 100 */
  { 32U, LOOP_LOCK_FREE, 1U, 0U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3500U,  468U,    0U,  1000U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_71_5,  1U,    0U },   /* 7 S71.5 gap 50  */
};
#elif ORDER_MODES
/* Round 44a: read-order rotation. All modes are the 00d11f9e configuration
 * (S41.5, 32 pulses, one conversion, bin 9, P32-C4164, 1002 us floor, 1/32
 * hysteresis, learner off). Modes 1-7 change only the order of the 7 or 8
 * reads: X-first/Y-first, 7R (no Y preparation read) or 8R, and forward or
 * reversed slots within each window. Mode 7 adds the 1004 us floor. */
static const loop_mode_def loop_defs[LOOP_MODE_COUNT] = {
  /* pulses lock          nconv skipY skipX gap  cad  phase abs  bin  rc  period adc1    pa2   pad  frame  maint hyst learn fit  ex   ey   sampt            order gap */
  { 29U, LOOP_LOCK_FREE, 3U, 0U, 0U,  0U, 0U,  0U, 0U,  0U, 0U,   0U,     0U,    0U,    0U,     0U,   0U,  5U,  1U,  0U,  0,   0,  ADC_SAMPT_13_5,  0U,    0U },   /* 0 HOOK          */
  { 32U, LOOP_LOCK_FREE, 1U, 1U, 0U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1002U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  0U,    0U },   /* 1 X-first 7R    */
  { 32U, LOOP_LOCK_FREE, 1U, 0U, 0U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1002U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  0U,    0U },   /* 2 X-first 8R    */
  { 32U, LOOP_LOCK_FREE, 1U, 0U, 0U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1002U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  1U,    0U },   /* 3 Y-first 8R    */
  { 32U, LOOP_LOCK_FREE, 1U, 1U, 0U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1002U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  1U,    0U },   /* 4 Y-first 7R    */
  { 32U, LOOP_LOCK_FREE, 1U, 1U, 0U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1002U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  4U,    0U },   /* 5 X-rev 7R      */
  { 32U, LOOP_LOCK_FREE, 1U, 1U, 0U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1002U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  5U,    0U },   /* 6 Y-first X-rev */
  { 32U, LOOP_LOCK_FREE, 1U, 1U, 0U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1004U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  1U,    0U },   /* 7 Y-first 1004  */
};
#elif SETTLE_MODES
/* Round 44b: settle-gap / frame-floor rotation. Mode 1 is the control and mode
 * 2 is the known-safe 1004 us floor, so one bad gap mode cannot hide the floor
 * result. Modes 3-5 add 4/8/16 us before the first burst, mode 6 is Y-first +
 * 8 us, mode 7 combines Y-first + 8 us + 1004 us. */
static const loop_mode_def loop_defs[LOOP_MODE_COUNT] = {
  /* pulses lock          nconv skipY skipX gap  cad  phase abs  bin  rc  period adc1    pa2   pad  frame  maint hyst learn fit  ex   ey   sampt            order gap */
  { 29U, LOOP_LOCK_FREE, 3U, 0U, 0U,  0U, 0U,  0U, 0U,  0U, 0U,   0U,     0U,    0U,    0U,     0U,   0U,  5U,  1U,  0U,  0,   0,  ADC_SAMPT_13_5,  0U,    0U },   /* 0 HOOK          */
  { 32U, LOOP_LOCK_FREE, 1U, 1U, 0U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1002U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  0U,    0U },   /* 1 base          */
  { 32U, LOOP_LOCK_FREE, 1U, 1U, 0U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1004U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  0U,    0U },   /* 2 floor 1004    */
  { 32U, LOOP_LOCK_FREE, 1U, 1U, 0U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1002U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  0U,  288U },   /* 3 gap 4 us      */
  { 32U, LOOP_LOCK_FREE, 1U, 1U, 0U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1002U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  0U,  576U },   /* 4 gap 8 us      */
  { 32U, LOOP_LOCK_FREE, 1U, 1U, 0U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1002U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  0U, 1152U },   /* 5 gap 16 us     */
  { 32U, LOOP_LOCK_FREE, 1U, 1U, 0U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1002U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  1U,  576U },   /* 6 Y-first 8 us  */
  { 32U, LOOP_LOCK_FREE, 1U, 1U, 0U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1004U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  1U,  576U },   /* 7 Y-first+1004+8*/
};
#elif PHASE_MODES
/* Round 42: frame start phase alignment. All modes are the 00d11f9e configuration
 * (S41.5, 32 pulses, one conversion, bin 9, P32-C4164, 1002 us floor, 1/32 hysteresis,
 * learner off); only the frame start phase differs: mode 1 is unaligned (the control,
 * the phase is whatever the schedule reset happens to give), modes 2-7 force DWT mod 12
 * to 0, 2, 4, 6, 8, 10. */
static const loop_mode_def loop_defs[LOOP_MODE_COUNT] = {
  /* pulses lock          nconv skipY skipX gap  cad  phase abs  bin  rc  period adc1    pa2   pad  frame  maint hyst learn fit  ex   ey   sampt            align */
  { 29U, LOOP_LOCK_FREE, 3U, 0U, 0U,  0U, 0U,  0U, 0U,  0U, 0U,   0U,     0U,    0U,    0U,     0U,   0U,  5U,  1U,  0U,  0,   0,  ADC_SAMPT_13_5,  255U },   /* 0 HOOK    */
  { 32U, LOOP_LOCK_FREE, 1U, 1U, 0U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1002U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,  255U },   /* 1 free    */
  { 32U, LOOP_LOCK_FREE, 1U, 1U, 0U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1002U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,    0U },   /* 2 A0      */
  { 32U, LOOP_LOCK_FREE, 1U, 1U, 0U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1002U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,    2U },   /* 3 A2      */
  { 32U, LOOP_LOCK_FREE, 1U, 1U, 0U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1002U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,    4U },   /* 4 A4      */
  { 32U, LOOP_LOCK_FREE, 1U, 1U, 0U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1002U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,    6U },   /* 5 A6      */
  { 32U, LOOP_LOCK_FREE, 1U, 1U, 0U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1002U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,    8U },   /* 6 A8      */
  { 32U, LOOP_LOCK_FREE, 1U, 1U, 0U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1002U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5,   10U },   /* 7 A10     */
};
#elif SCAN1K_MODES
/* Round 40: 1 kHz long-window / frame-floor rotation. All modes keep one
 * conversion (median-of-3 was bad at the short windows in round 39), bin 9,
 * 1/32 hysteresis and the learner off. Modes 1-3 vary the frame floor around
 * the 1 ms host poll; modes 4-7 trade pulses and the X preparation read to fit
 * a longer ADC window inside ~1002 us. */
static const loop_mode_def loop_defs[LOOP_MODE_COUNT] = {
  /* pulses lock          nconv skipY skipX gap  cad  phase abs  bin  rc  period adc1    pa2   pad  frame  maint hyst learn fit  ex   ey   sampt */
  { 29U, LOOP_LOCK_FREE, 3U, 0U, 0U,  0U, 0U,  0U, 0U,  0U, 0U,   0U,     0U,    0U,    0U,     0U,   0U,  5U,  1U,  0U,  0,   0,  ADC_SAMPT_13_5  },   /* 0 HOOK        */
  { 32U, LOOP_LOCK_FREE, 1U, 1U, 0U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1002U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5  },   /* 1 S41/1002    */
  { 32U, LOOP_LOCK_FREE, 1U, 1U, 0U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1000U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5  },   /* 2 S41/1000    */
  { 32U, LOOP_LOCK_FREE, 1U, 1U, 0U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1004U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5  },   /* 3 S41/1004    */
  { 28U, LOOP_LOCK_FREE, 1U, 1U, 0U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1002U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_71_5  },   /* 4 S71/28p     */
  { 24U, LOOP_LOCK_FREE, 1U, 1U, 0U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1002U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_71_5  },   /* 5 S71/24p     */
  { 24U, LOOP_LOCK_FREE, 1U, 1U, 0U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1002U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_239_5 },   /* 6 S239/24p    */
  { 26U, LOOP_LOCK_FREE, 1U, 1U, 1U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1002U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_239_5 },   /* 7 S239/26p 6R */
};
#elif RATE_MODES
/* Round 39: 800 Hz (1250 us frame floor) rate/scanning rotation. All modes keep
 * the release read timing (P32-C4164, bin 9, 1/32 hysteresis, learner off) and
 * spend the extra ~250 us per frame on a longer ADC window and/or same-frame
 * median-of-3, and in modes 6-7 on 40 excitation pulses. */
static const loop_mode_def loop_defs[LOOP_MODE_COUNT] = {
  /* pulses lock          nconv skipY skipX gap  cad  phase abs  bin  rc  period adc1    pa2   pad  frame  maint hyst learn fit  ex   ey   sampt */
  { 29U, LOOP_LOCK_FREE, 3U, 0U, 0U,  0U, 0U,  0U, 0U,  0U, 0U,   0U,     0U,    0U,    0U,     0U,   0U,  5U,  1U,  0U,  0,   0,  ADC_SAMPT_13_5  },   /* 0 HOOK       */
  { 32U, LOOP_LOCK_FREE, 1U, 1U, 0U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1250U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5  },   /* 1 S41/1      */
  { 32U, LOOP_LOCK_FREE, 3U, 1U, 0U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1250U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5  },   /* 2 S41/3      */
  { 32U, LOOP_LOCK_FREE, 3U, 1U, 0U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1250U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_71_5  },   /* 3 S71/3      */
  { 32U, LOOP_LOCK_FREE, 1U, 1U, 0U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1250U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_239_5 },   /* 4 S239/1     */
  { 32U, LOOP_LOCK_FREE, 3U, 1U, 0U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1250U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_239_5 },   /* 5 S239/3     */
  { 40U, LOOP_LOCK_FREE, 3U, 1U, 0U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1250U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5  },   /* 6 S41/40p/3  */
  { 40U, LOOP_LOCK_FREE, 3U, 1U, 0U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1250U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_71_5  },   /* 7 S71/40p/3  */
};
#elif FINE_MODES
/* Round 38: S41.5 fixed, residual-X rotation. Hysteresis 1/16, 1/32, 1/64,
 * 1/128 and fixed X slot-gain e +8/-8/+16 at the same read timing. */
static const loop_mode_def loop_defs[LOOP_MODE_COUNT] = {
  /* pulses lock          nconv skipY skipX gap  cad  phase abs  bin  rc  period adc1    pa2   pad  frame  maint hyst learn fit  ex   ey   sampt */
  { 29U, LOOP_LOCK_FREE, 3U, 0U, 0U,  0U, 0U,  0U, 0U,  0U, 0U,   0U,     0U,    0U,    0U,     0U,   0U,  5U,  1U,  0U,  0,   0,  ADC_SAMPT_13_5  },   /* 0 HOOK      */
  { 32U, LOOP_LOCK_FREE, 1U, 1U, 0U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1002U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5  },   /* 1 H5       */
  { 32U, LOOP_LOCK_FREE, 1U, 1U, 0U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1002U,   1U,  4U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5  },   /* 2 H4       */
  { 32U, LOOP_LOCK_FREE, 1U, 1U, 0U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1002U,   1U,  6U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5  },   /* 3 H6       */
  { 32U, LOOP_LOCK_FREE, 1U, 1U, 0U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1002U,   1U,  7U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5  },   /* 4 H7       */
  { 32U, LOOP_LOCK_FREE, 1U, 1U, 0U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1002U,   1U,  5U,  0U,  0U,  8,   0,  ADC_SAMPT_41_5  },   /* 5 EP8      */
  { 32U, LOOP_LOCK_FREE, 1U, 1U, 0U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1002U,   1U,  5U,  0U,  0U, -8,   0,  ADC_SAMPT_41_5  },   /* 6 EN8      */
  { 32U, LOOP_LOCK_FREE, 1U, 1U, 0U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1002U,   1U,  5U,  0U,  0U, 16,   0,  ADC_SAMPT_41_5  },   /* 7 EP16     */
};
#elif SAMPT_MODES
/* Round 35: ADC sample-time rotation. Same read timing, one conversion per
 * read; only the ADC sample window changes. The stock/Gaomon use a much
 * longer window and a median-of-3, so 13.5 cycles is the outlier. */
static const loop_mode_def loop_defs[LOOP_MODE_COUNT] = {
  /* pulses lock          nconv skipY skipX gap  cad  phase abs  bin  rc  period adc1    pa2   pad  frame  maint hyst learn fit  ex   ey   sampt */
  { 29U, LOOP_LOCK_FREE, 3U, 0U, 0U,  0U, 0U,  0U, 0U,  0U, 0U,   0U,     0U,    0U,    0U,     0U,   0U,  5U,  1U,  0U,  0,   0,  ADC_SAMPT_13_5  },   /* 0 HOOK      */
  { 32U, LOOP_LOCK_FREE, 1U, 1U, 0U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1002U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_13_5  },   /* 1 S13.5     */
  { 32U, LOOP_LOCK_FREE, 1U, 1U, 0U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1002U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_28_5  },   /* 2 S28.5     */
  { 32U, LOOP_LOCK_FREE, 1U, 1U, 0U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1002U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_41_5  },   /* 3 S41.5     */
  { 32U, LOOP_LOCK_FREE, 1U, 1U, 0U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1002U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_55_5  },   /* 4 S55.5     */
  { 32U, LOOP_LOCK_FREE, 1U, 1U, 0U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1002U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_71_5  },   /* 5 S71.5     */
  { 32U, LOOP_LOCK_FREE, 1U, 1U, 0U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1002U,   1U,  5U,  0U,  0U,  0,   0,  ADC_SAMPT_239_5 },   /* 6 S239.5    */
  { 32U, LOOP_LOCK_FREE, 1U, 1U, 0U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1002U,   1U,  5U,  0U,  0U,  0,   0,  1U              },   /* 7 S7.5      */
};
#else
static const loop_mode_def loop_defs[LOOP_MODE_COUNT] = {
  /* pulses lock          nconv skipY skipX gap  cad  phase abs  bin  rc  period adc1    pa2   pad  frame  maint hyst learn fit  ex   ey   sampt */
  { 29U, LOOP_LOCK_FREE, 3U, 0U, 0U,  0U, 0U,  0U, 0U,  0U, 0U,   0U,     0U,    0U,    0U,     0U,   0U,  5U,  1U,  0U,  0,   0,  0U },   /* 0 HOOK    */
  { 32U, LOOP_LOCK_FREE, 1U, 1U, 0U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1002U,   1U,  5U,  0U,  0U,  0,   0,  0U },   /* 1 E0      */
  { 32U, LOOP_LOCK_FREE, 1U, 1U, 0U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1002U,   1U,  5U,  0U,  0U,  4,   0,  0U },   /* 2 EP4     */
  { 32U, LOOP_LOCK_FREE, 1U, 1U, 0U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1002U,   1U,  5U,  0U,  0U,  8,   0,  0U },   /* 3 EP8     */
  { 32U, LOOP_LOCK_FREE, 1U, 1U, 0U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1002U,   1U,  5U,  0U,  0U, 16,   0,  0U },   /* 4 EP16    */
  { 32U, LOOP_LOCK_FREE, 1U, 1U, 0U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1002U,   1U,  5U,  0U,  0U, -4,   0,  0U },   /* 5 EN4     */
  { 32U, LOOP_LOCK_FREE, 1U, 1U, 0U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1002U,   1U,  5U,  0U,  0U, -8,   0,  0U },   /* 6 EN8     */
  { 32U, LOOP_LOCK_FREE, 1U, 1U, 0U,  0U, 1U, 17U, 1U,  9U, 0U,   0U,  3696U,  468U,    0U,  1002U,   1U,  5U,  0U,  0U,-16,   0,  0U },   /* 7 EN16    */
};
#endif

static uint8_t loop_mode;
static uint8_t loop_maint;
static uint32_t loop_gap;          /* FREE frame gap in cycles; 0 = none (HOOK, SOF, IN) */
static uint8_t loop_sof;           /* the current mode is SOF-locked */
static uint8_t loop_in;            /* the current mode starts frames on the host's poll */
static uint8_t loop_cadence;       /* the current mode reads its first pass with sensing_read_frame() */
static uint8_t loop_cad_bin;       /* nonzero: the current mode's frames read at this bin (round 16) */
static uint8_t loop_cad_rc;        /* the current mode reads re-centering passes as cadence frames (round 17) */
static uint8_t loop_maint_off;     /* the current mode skips bin maintenance (round 23) */
#if !defined(LOOP_MODE_FIXED)
static uint32_t loop_frames;       /* published rotation-mode frames, counted across locks */
static uint8_t loop_rot = 1U;      /* the current rotation mode, 1..LOOP_MODE_COUNT-1 */
#endif
#if LOOP_WARM
static uint16_t loop_warm;         /* HOOK frames left in this lock before LOOP_MODE_FIXED */
#endif
static uint8_t loop_sof_have;      /* loop_sof_fn / loop_sof_t belong to the previous frame start */
static uint16_t loop_sof_fn;       /* USB frame number when that frame started */
static uint32_t loop_sof_t;        /* DWT when its SOF edge was seen */
static uint16_t loop_sof_slack;    /* us, saturating 1023 */
static uint8_t loop_sof_over;      /* the previous frame ran past its USB frame */
static uint8_t loop_published;     /* the previous frame published a report */
static uint16_t loop_in_wait_us;   /* IN: start of the wait to the IN completion, us, saturating 1023 */
static uint8_t loop_in_flag;       /* IN: this frame did not start on an IN completion */

static void loop_apply(uint8_t mode) {
  if (mode >= LOOP_MODE_COUNT) {
    mode = 0U;
  }
  const loop_mode_def *d = &loop_defs[mode];
  loop_mode = mode;
  loop_sof = (d->lock == LOOP_LOCK_SOF) ? 1U : 0U;
  loop_in = (d->lock == LOOP_LOCK_IN) ? 1U : 0U;
  loop_gap = (mode == 0U || d->lock != LOOP_LOCK_FREE) ? 0U : (uint32_t)d->gap_us * (CPU_HZ / 1000000UL);
  loop_sof_have = 0U;
  loop_sof_over = 0U;
  loop_sof_slack = 0U;
  loop_in_wait_us = 0U;
  loop_in_flag = 0U;
#if AXIS_GAP_MODES
  /* Round 50: gap_cyc is the extra X gap, gap_y the extra Y gap. */
  sensing_set_loop((uint32_t)d->gap_cyc, 0U,
                   (mode == 0U) ? 0U : SENSING_LOOP_DWELL);
  sensing_set_loop_gap_y(d->gap_y);
#elif GAP_MODES || CONV_MODES || JITTER_MODES
  /* Round 49 tables reuse gap_cyc as the extra read-to-read idle gap. */
  sensing_set_loop((uint32_t)d->gap_cyc, 0U,
                   (mode == 0U) ? 0U : SENSING_LOOP_DWELL);
#else
  sensing_set_loop(0U, 0U, (mode == 0U) ? 0U : SENSING_LOOP_DWELL);
#endif
  sensing_set_loop_analog(d->pulses, d->adc1);
  sensing_set_loop_pa2(d->pa2_low);
  sensing_set_loop_read(d->nconv, d->skip_prep_y, d->skip_prep_x);
#if ORDER_MODES || SETTLE_MODES
  sensing_set_read_order(d->order);
#if !(GAP_MODES || CONV_MODES || JITTER_MODES || AXIS_GAP_MODES)
  sensing_set_frame_gap(d->gap_cyc);
#endif
#endif
  loop_cadence = (mode != 0U && d->cadence != 0U) ? 1U : 0U;
  loop_cad_bin = (loop_cadence != 0U && d->cad_bin >= 1U && d->cad_bin <= 12U) ? d->cad_bin : 0U;
  loop_cad_rc = (loop_cadence != 0U && d->cad_rc != 0U) ? 1U : 0U;
  loop_maint_off = (loop_cadence != 0U && d->maint_off != 0U) ? 1U : 0U;
  sensing_set_cadence(d->cad_phase, d->cad_abs);
  sensing_set_cadence_period((loop_cadence != 0U) ? d->cad_period : 0U);
  sensing_set_cadence_pad((loop_cadence != 0U) ? d->cad_pad : 0U);
#if FRAME_FLOOR_US
  const uint16_t floor_us = (uint16_t)FRAME_FLOOR_US;
#else
  const uint16_t floor_us = d->frame_us;
#endif
  sensing_set_cadence_frame((loop_cadence != 0U) ? (uint32_t)floor_us * (CPU_HZ / 1000000UL) : 0U);
#if PHASE_MODES
  sensing_set_start_align((loop_cadence != 0U) ? d->align : 255U);
#endif
#if R55_MODES
  sensing_set_r55((loop_cadence != 0U) ? d->r55_align : 0U, (loop_cadence != 0U) ? d->r55_exact : 0U);
#endif
#if R56_MODES
  sensing_set_r56_lead((loop_cadence != 0U) ? (uint32_t)d->r56_lead : 0U);
#endif
  tracker_set_hyst_shift(d->hyst);
  tracker_set_slot_learn(d->learn);
  tracker_set_fit_mode(d->fit);
#if SAMPT_MODES
  sensing_set_sample_time(d->sampt);
#endif
}

/* Per-mode tracker slot-gain correction: modes whose reads differ need
 * different corrections, so each mode keeps its own copy of tracker.e[]. */
static int32_t loop_e[LOOP_MODE_COUNT][2];

static void loop_switch(tracker_state *t, uint8_t mode) {
  if (mode >= LOOP_MODE_COUNT) {
    mode = 0U;
  }
  loop_e[loop_mode][0] = t->e[0];
  loop_e[loop_mode][1] = t->e[1];
  loop_apply(mode);
  if (loop_defs[mode].learn == 0U) {
    t->e[0] = loop_defs[mode].e_x;
    t->e[1] = loop_defs[mode].e_y;
  } else {
    t->e[0] = loop_e[mode][0];
    t->e[1] = loop_e[mode][1];
  }
  loop_e[mode][0] = t->e[0];
  loop_e[mode][1] = t->e[1];
}

/* Round 17 lock-loss diagnostics: why the previous lock ended and how many
 * reports it published, sent in the next lock's HOOK report (aux bits 0-5). */
static uint8_t loop_lost_why;      /* this frame's pass loop: 5 cadence read failed, else 0 */
static uint32_t loop_lock_frames;  /* published reports in the current lock */
static uint8_t loop_last_why;      /* see the aux description above */
static uint8_t loop_last_len;      /* see the aux description above */
static uint8_t loop_drops_since_pub;  /* locks ended since the latest published report, saturating */

static __attribute__((unused)) void loop_note_drop(uint8_t reason) {
  loop_last_why = (reason == 2U && loop_lost_why != 0U) ? loop_lost_why : reason;
  const uint32_t n = loop_lock_frames;
  loop_last_len = (n < 16U) ? 0U : (n < 64U) ? 1U : (n < 256U) ? 2U :
                  (n < 1024U) ? 3U : (n < 4096U) ? 4U : (n < 16384U) ? 5U : 6U;
  loop_lock_frames = 0U;
  if (loop_drops_since_pub < 255U) {
    loop_drops_since_pub++;
  }
}

#if LOOP_DIAG || LOOP_HEARTBEAT
/* Round 18 diagnostics (-Diag18); -Release reuses only the clock and heartbeat.
 * Pen reports: aux bits 0-5 = (device ms >> 2) & 63 (a 4 ms clock that wraps every
 * 256 ms; it replaces the lateness and the round 17 HOOK why/len); bit 12 = a SETUP
 * stage or a USB bus event happened since the previous pen report; bits 6-11 and 13-15
 * as before (bin, multipass, maintenance, mode).
 * Heartbeat: when no frame has been published for DIAG_HB_MS, a status 0xD0 frame.
 * Round 20: OpenTabletDriver reads frames with status bit 6 as aux reports and
 * takes the aux buttons from wire bytes 4-6, so wire bytes 2-7 are zero:
 *   wire 8-9 sequence, 10 previous lock's why | len << 3 (round 17 codes) |
 *   bit 6 tracking | bit 7 stuck (endpoint busy or USB not ready >= 100 ms),
 *   11 locks since boot (u8).
 * LED: stuck 1 blinks fast (64 ms on / 64 ms off), stuck 2 blinks slowly (512 ms). */
#define DIAG_MS_CYC   (CPU_HZ / 1000UL)
#define DIAG_HB_MS    10U
#define DIAG_STUCK_MS 100U
#if QUIET_HEARTBEAT
#define DIAG_PRIME_FRAMES 1024U   /* startup heartbeat burst before quiet mode (round 17 death) */
#endif
static uint32_t diag_ms;            /* device milliseconds since dwt_init() */
static uint32_t diag_ms_cyc;        /* DWT of the last whole millisecond */
static uint32_t diag_last_pub;      /* diag_ms of the latest published frame */
#if LOOP_DIAG
static uint32_t diag_searches;      /* search iterations since boot */
#endif
static uint32_t diag_locks;         /* locks acquired since boot */
#if LOOP_DIAG
static uint8_t diag_peak;           /* latest search: max(xp, yp) / 8, saturating */
#endif
static uint8_t diag_stuck;          /* 0 ok, 1 endpoint busy, 2 USB not ready (>= DIAG_STUCK_MS) */
static uint8_t diag_wait_on;        /* a report is waiting */
static uint32_t diag_wait_since;    /* diag_ms when it started waiting */
#if QUIET_HEARTBEAT
static uint16_t diag_prime;         /* startup heartbeat frames sent since boot */
#endif
#if LOOP_DIAG
static uint32_t diag_setup_at_pub;  /* loop_setup_count at the latest pen report */
static uint32_t diag_events_at_pub; /* loop_usb_events at the latest pen report */
#endif

static void diag_clock(void) {
  while ((uint32_t)(DWT_CYCCNT - diag_ms_cyc) >= DIAG_MS_CYC) {
    diag_ms_cyc += DIAG_MS_CYC;
    diag_ms++;
  }
}

static void diag_usb_watch(void) {
  if (report_pending() && !REPORT_EP_READY()) {
    if (!diag_wait_on) {
      diag_wait_on = 1U;
      diag_wait_since = diag_ms;
    }
    if ((uint32_t)(diag_ms - diag_wait_since) >= DIAG_STUCK_MS) {
      diag_stuck = tud_ready() ? 1U : 2U;
    } else {
      diag_stuck = 0U;
    }
  } else {
    diag_wait_on = 0U;
    diag_stuck = 0U;
  }
}

static void diag_heartbeat(uint8_t tracking) {
  static const uint8_t body[6] = { 0U, 0U, 0U, 0U, 0U, 0U };
  const uint16_t aux = (uint16_t)((loop_last_why & 7U) | ((loop_last_len & 7U) << 3) |
                                  ((tracking ? 1U : 0U) << 6) |
                                  ((diag_stuck != 0U) ? 0x80U : 0U) |
                                  ((diag_locks & 0xFFU) << 8));
  report_publish_frame(0xD0U, body, aux);
}
#endif

static uint16_t loop_usb_frame(void) {
  return (uint16_t)USBFS_DSTS_FNSOF(USBFS_DSTS);
}

/* SOF modes: wait for the next USB frame edge, then schedule the frame's first
 * burst LOOP_SOF_LEAD_CYC after it. USB is serviced once (that sends the report
 * just published) and then only while the predicted edge is more than
 * LOOP_SOF_GUARD_CYC away, so a tight poll sees the edge. Without a previous
 * edge (the first frame of a SOF block) the poll services USB as it waits.
 * After LOOP_SOF_TIMEOUT_CYC without an edge (USB not running) the frame
 * starts at once. */
static void loop_sof_wait(void) {
  const uint32_t w0 = DWT_CYCCNT;
  const uint16_t fn0 = loop_usb_frame();
  const uint16_t elapsed = (uint16_t)(((uint32_t)fn0 - (uint32_t)loop_sof_fn) & 0x3FFFU);
  loop_sof_over = (loop_sof_have != 0U && elapsed != 0U) ? 1U : 0U;
  service_usb();
  const uint8_t precise = (loop_sof_have != 0U && elapsed < 8U) ? 1U : 0U;
  if (precise) {
    const uint32_t edge = loop_sof_t + ((uint32_t)elapsed + 1U) * LOOP_SOF_PERIOD_CYC;
    while ((int32_t)(edge - LOOP_SOF_GUARD_CYC - DWT_CYCCNT) > 0 &&
           loop_usb_frame() == fn0) {
      service_usb();
    }
  }
  uint8_t timed_out = 0U;
  while (loop_usb_frame() == fn0) {
    if ((uint32_t)(DWT_CYCCNT - w0) > LOOP_SOF_TIMEOUT_CYC) {
      timed_out = 1U;
      break;
    }
    if (!precise) {
      service_usb();
    }
  }
  const uint32_t t_edge = DWT_CYCCNT;
  const uint32_t slack_us = (t_edge - w0) / (CPU_HZ / 1000000UL);
  loop_sof_slack = (uint16_t)((slack_us > 1023U) ? 1023U : slack_us);
  loop_sof_fn = loop_usb_frame();
  loop_sof_t = t_edge;
  loop_sof_have = timed_out ? 0U : 1U;
  sensing_frame_start_at(t_edge + LOOP_SOF_LEAD_CYC);
}

/* IN modes: wait until the host has taken the report the previous frame
 * published (vendor EP1 IN transfer complete, captured by USBFS_IRQHandler),
 * then schedule the frame's first burst LOOP_IN_EPS_CYC after it. USB is
 * serviced once (that queues the report). If the previous frame published no
 * report, the SOF wait is used instead. After LOOP_IN_TIMEOUT_CYC without a
 * completion the frame starts at once. */
static void loop_in_wait(uint8_t published) {
  if (!published) {
    loop_sof_wait();
    loop_in_wait_us = loop_sof_slack;
    loop_in_flag = 1U;
    return;
  }
  const uint32_t w0 = DWT_CYCCNT;
#if defined(FAST_EP1) && FAST_EP1 && (defined(MODE1_IN) || R55_MODES)
  /* Round 54: -FastEp1 masks the EP1 IN interrupt, so loop_in_count never
   * moves. Poll the transfer-complete flag directly instead. report_task()
   * right after report_publish() normally armed this frame's report already
   * (fast_send() cleared XFRC), so XFRC here means the host took that report.
   * service_usb() sends it if the endpoint was still busy then. */
  service_usb();
  uint8_t timed_out = 0U;
  uint32_t t_in;
  for (;;) {
    t_in = DWT_CYCCNT;
    if ((USBFS_DIEPINT1 & USBFS_DIEPINT_XFRC) != 0U) {
      break;
    }
    if ((uint32_t)(t_in - w0) > LOOP_IN_TIMEOUT_CYC) {
      timed_out = 1U;
      break;
    }
  }
  USBFS_DIEPINT1 = USBFS_DIEPINT_XFRC;   /* write 1 to clear: the next XFRC is the next report's */
  report_task();                         /* a report still pending (endpoint was busy) goes now */
#else
  const uint32_t c0 = loop_in_count;
  service_usb();
  uint8_t timed_out = 0U;
  while (loop_in_count == c0) {
    if ((uint32_t)(DWT_CYCCNT - w0) > LOOP_IN_TIMEOUT_CYC) {
      timed_out = 1U;
      break;
    }
  }
  const uint32_t t_in = timed_out ? DWT_CYCCNT : loop_in_t;
#endif
  const uint32_t wait_us = (t_in - w0) / (CPU_HZ / 1000000UL);
  loop_in_wait_us = (uint16_t)((wait_us > 1023U) ? 1023U : wait_us);
  loop_in_flag = timed_out;
  loop_sof_have = 0U;   /* a later SOF wait must not predict from an old edge */
  sensing_frame_start_at(t_in + LOOP_IN_EPS_CYC);
}

/* Aux in-index from sensing_in_mask(): first read with the IN transfer
 * complete (0..13), 14 = only at frame end, 15 = never during the frame. */
static __attribute__((unused)) uint32_t loop_in_index(uint32_t mask) {
  if ((mask & 0x7FFFU) != 0U) {
    const uint32_t i = (uint32_t)__builtin_ctz(mask & 0x7FFFU);
    return (i > 13U) ? 13U : i;
  }
  return ((mask & 0x8000U) != 0U) ? 14U : 15U;
}
#endif

#if H420X_PROFILE == 3
#define PEN_PACE_FRAMES      2U                          /* report every 2nd USB frame */
#define PEN_PACE_TIMEOUT_CYC (3UL * (CPU_HZ / 1000UL))   /* 3 ms */
#define PEN_PACE_MAX_MISSES  8U
#define PEN_RELEASE_GAP_CYC  (4UL * (CPU_HZ / 1000UL))   /* hover -> out of range */

static uint16_t pace_fn0;
static uint32_t pace_t0;
static uint8_t pace_misses;
static uint8_t pace_enabled = 1U;
static uint8_t pen_in_range;
static int32_t last_x;
static int32_t last_y;

static uint16_t usb_frame_number(void) {
  return (uint16_t)USBFS_DSTS_FNSOF(USBFS_DSTS);
}

static void pace_frame_start(void) {
  pace_fn0 = usb_frame_number();
  pace_t0 = DWT_CYCCNT;
}

/* Wait until PEN_PACE_FRAMES USB frames have started since pace_frame_start()
 * so reports leave exactly 2 ms apart. If the frame number does not advance,
 * give up after 3 ms; after 8 consecutive misses, stop pacing. */
static void pace_wait(void) {
  if (!pace_enabled) {
    return;
  }
  while ((uint16_t)((usb_frame_number() - pace_fn0) & 0x3FFFU) < PEN_PACE_FRAMES) {
    service_usb();
    if ((uint32_t)(DWT_CYCCNT - pace_t0) > PEN_PACE_TIMEOUT_CYC) {
      if (++pace_misses >= PEN_PACE_MAX_MISSES) {
        pace_enabled = 0U;
      }
      return;
    }
  }
  pace_misses = 0U;
}

/* Pen lost: one in-range report with pressure 0, then an out-of-range report
 * 4 ms later, both at the last position. */
static void pen_release(void) {
  pen_reset_probe();
  if (!pen_in_range) {
    return;
  }
  pen_in_range = 0U;
  report_publish(last_x, last_y, 0U, PEN_STATUS_IN_RANGE);
  const uint32_t t0 = DWT_CYCCNT;
  while ((uint32_t)(DWT_CYCCNT - t0) < PEN_RELEASE_GAP_CYC) {
    service_usb();
  }
  report_publish(last_x, last_y, 0U, 0x00U);
  report_task();
}
#endif

#if H420X_PROFILE == 4
/* rhythm1000 frame lock. Each tracked frame starts on a USB frame edge:
 *  - no previous tracked frame, or the previous frame ended inside the USB
 *    frame it started in: start at the next frame-number change;
 *  - the previous frame ran past its USB frame because it is too long to fit
 *    (one pass, no maintenance, >= 980 us): start at once (free-run);
 *  - otherwise (re-centering pass or bin maintenance): start at the next
 *    frame-number change, skipping one USB frame, so the phase stays locked.
 * If the frame number does not change within 1.1 ms, count a miss; after 8
 * consecutive misses, locking is off for 1024 frames, then retried. */
#define LOCK_LONG_CYC     (980UL * (CPU_HZ / 1000000UL))
#define LOCK_TIMEOUT_CYC  (1100UL * (CPU_HZ / 1000000UL))
#define LOCK_MAX_MISSES   8U
#define LOCK_RETRY_FRAMES 1024U

/* Wire bytes 10-11: bits 0-10 frame time in us (frame start to the end of
 * the pass loop, saturating), bits 11-12 passes, then these flags. */
#define AUX_PASSES_SHIFT  11U
#define AUX_LOCKED        0x2000U  /* this frame started on a USB frame edge */
#define AUX_PREV_OVERRAN  0x4000U  /* the previous frame ran past its USB frame */
#define AUX_MAINT         0x8000U  /* bin maintenance ran before this frame */

static uint8_t lock_enabled = 1U;
static uint8_t lock_misses;
static uint16_t lock_retry;
static uint8_t lock_have_prev;   /* the previous iteration published a tracked frame */
static uint8_t lock_prev_passes;
static uint8_t lock_maint;
static uint16_t lock_fn;         /* USB frame number at the current frame start */
static uint32_t lock_t0;         /* DWT at the current frame start */
static uint16_t lock_flags;      /* AUX_* flags of the current frame */

static uint16_t lock_frame_number(void) {
  return (uint16_t)USBFS_DSTS_FNSOF(USBFS_DSTS);
}

/* Wait until the frame number differs from `from`; 1 = edge seen, 0 = miss. */
static int lock_wait_edge(uint16_t from) {
  const uint32_t t0 = DWT_CYCCNT;
  while (lock_frame_number() == from) {
    if (report_pending()) {
      tud_task();
      report_task();
    }
    if ((uint32_t)(DWT_CYCCNT - t0) > LOCK_TIMEOUT_CYC) {
      if (++lock_misses >= LOCK_MAX_MISSES) {
        lock_enabled = 0U;
        lock_retry = 0U;
      }
      return 0;
    }
  }
  lock_misses = 0U;
  return 1;
}

static __attribute__((noinline)) void frame_lock_wait(void) {
  uint16_t flags = lock_maint ? AUX_MAINT : 0U;
  lock_maint = 0U;
  if (!lock_enabled && ++lock_retry >= LOCK_RETRY_FRAMES) {
    lock_enabled = 1U;
    lock_misses = 0U;
  }
  if (lock_enabled) {
    const uint16_t fn = lock_frame_number();
    int wait = 1;
    if (lock_have_prev && fn != lock_fn) {
      flags |= AUX_PREV_OVERRAN;
      if (lock_prev_passes == 1U && (flags & AUX_MAINT) == 0U &&
          (uint32_t)(DWT_CYCCNT - lock_t0) >= LOCK_LONG_CYC) {
        wait = 0;
      }
    }
    if (wait && lock_wait_edge(fn)) {
      flags |= AUX_LOCKED;
    }
  }
  lock_flags = flags;
  lock_fn = lock_frame_number();
  lock_t0 = DWT_CYCCNT;
}
#endif

#if SENSING_TELEMETRY
static uint32_t telemetry_frames;
static uint16_t selftest_values[6];
static uint8_t selftest_ready;
static uint8_t selftest_index;

/* Diagnostic vendor frames, sent over the same MI_00 endpoint:
 *   0xD1: status  flags, bin, xc, yc, xpeak, ypeak, DWT low 16
 *   0xD2: window  axis, center, four raw amplitudes
 *   0xD3 selftest, 0xD5 PA2 probe, 0xD6/0xD7 lab, 0xD8 search
 * The normal 0x80 tracking report is still emitted when valid. */
static void telemetry_status(uint8_t flags, uint8_t bin, uint8_t xc,
                             uint8_t yc, uint16_t xp, uint16_t yp) {
  if (report_pending() || !tud_hid_n_ready(ITF_VENDOR)) {
    return;
  }
  uint8_t p[VENDOR_REPORT_LEN];
  p[0] = 0xD1U;
  p[1] = flags;
  p[2] = bin;
  p[3] = xc;
  p[4] = yc;
  p[5] = (uint8_t)(xp & 0xFFU);
  p[6] = (uint8_t)(xp >> 8);
  p[7] = (uint8_t)(yp & 0xFFU);
  p[8] = (uint8_t)(yp >> 8);
  p[9] = (uint8_t)(DWT_CYCCNT & 0xFFU);
  p[10] = (uint8_t)((DWT_CYCCNT >> 8) & 0xFFU);
  if (tud_hid_n_report(ITF_VENDOR, REPORT_ID_VENDOR, p, VENDOR_REPORT_LEN)) {
    telemetry_frames++;
  }
}

static void telemetry_window(uint8_t axis, uint8_t center,
                             const uint16_t a[4]) {
  if (report_pending() || !tud_hid_n_ready(ITF_VENDOR)) {
    return;
  }
  uint8_t p[VENDOR_REPORT_LEN];
  p[0] = 0xD2U;
  p[1] = axis;
  p[2] = center;
  p[3] = (uint8_t)(a[0] & 0xFFU);
  p[4] = (uint8_t)(a[0] >> 8);
  p[5] = (uint8_t)(a[1] & 0xFFU);
  p[6] = (uint8_t)(a[1] >> 8);
  p[7] = (uint8_t)(a[2] & 0xFFU);
  p[8] = (uint8_t)(a[2] >> 8);
  p[9] = (uint8_t)(a[3] & 0xFFU);
  p[10] = (uint8_t)(a[3] >> 8);
  if (tud_hid_n_report(ITF_VENDOR, REPORT_ID_VENDOR, p, VENDOR_REPORT_LEN)) {
    telemetry_frames++;
  }
}

static bool telemetry_probe(const uint16_t pr[4]) {
  if (report_pending() || !tud_hid_n_ready(ITF_VENDOR)) {
    return false;
  }
  uint8_t p[VENDOR_REPORT_LEN] = { 0xD5U, 0U,
      (uint8_t)(pr[0] & 0xFFU), (uint8_t)(pr[0] >> 8),
      (uint8_t)(pr[1] & 0xFFU), (uint8_t)(pr[1] >> 8),
      (uint8_t)(pr[2] & 0xFFU), (uint8_t)(pr[2] >> 8),
      (uint8_t)(pr[3] & 0xFFU), (uint8_t)(pr[3] >> 8),
      0U };
  if (tud_hid_n_report(ITF_VENDOR, REPORT_ID_VENDOR, p,
                       VENDOR_REPORT_LEN)) {
    telemetry_frames++;
    return true;
  }
  return false;
}

static void telemetry_selftest_one(uint8_t idx, uint16_t value) {
  if (report_pending() || !tud_hid_n_ready(ITF_VENDOR)) {
    return;
  }
  uint8_t p[VENDOR_REPORT_LEN] = { 0xD3U, idx,
      (uint8_t)(value & 0xFFU), (uint8_t)(value >> 8),
      0, 0, 0, 0, 0, 0, 0 };
  if (tud_hid_n_report(ITF_VENDOR, REPORT_ID_VENDOR, p,
                       VENDOR_REPORT_LEN)) {
    telemetry_frames++;
  }
}

/* Send one diagnostic frame, waiting (with USB serviced) up to 4 ms for the
 * pending pen report to leave and the endpoint to be free. */
static bool telemetry_send_blocking(const uint8_t p[VENDOR_REPORT_LEN]) {
  if (!tud_ready()) {
    return false;
  }
  const uint32_t t0 = DWT_CYCCNT;
  while (report_pending() || !tud_hid_n_ready(ITF_VENDOR)) {
    service_usb();
    if ((uint32_t)(DWT_CYCCNT - t0) > (4UL * (CPU_HZ / 1000UL))) {
      return false;
    }
  }
  if (tud_hid_n_report(ITF_VENDOR, REPORT_ID_VENDOR, p, VENDOR_REPORT_LEN)) {
    telemetry_frames++;
    return true;
  }
  return false;
}

static void put16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)(v & 0xFFU);
  p[1] = (uint8_t)(v >> 8);
}

/* 0xD8 search: xc, yc, X peak, X floor (median), Y peak, Y floor. */
static void telemetry_search(uint8_t xc, uint8_t yc, uint16_t xp, uint16_t xf,
                             uint16_t yp, uint16_t yf) {
  uint8_t p[VENDOR_REPORT_LEN];
  p[0] = 0xD8U;
  p[1] = xc;
  p[2] = yc;
  put16(&p[3], xp);
  put16(&p[5], xf);
  put16(&p[7], yp);
  put16(&p[9], yf);
  (void)telemetry_send_blocking(p);
}

/* Read A/B lab (0.6.3, 8 variants since 0.6.5): LAB_VARIANTS * LAB_ROUNDS
 * reads of X coil rx_coil excited from Y coil tx_coil. Read i uses lab
 * variant j = i % LAB_VARIANTS:
 *   j = 0: baseline (0.5.1 pins)         j = 1: PA15_FAST (stock PA15 speed)
 *   j = 2: ADC_STOCK (stock ADC register rewrites before every conversion)
 *   j = 3: ADDR_CLEAR (stock clears the mux address bits before setting them)
 *   j = 4: ADC_STOCK + ADDR_CLEAR        j = 5: CTL_SLOW (stock control-pin speeds)
 *   j = 6: PA15_FAST + CTL_SLOW (stock pins)
 *   j = 7: ADC_STOCK + ADDR_CLEAR + PA15_FAST
 * Frames (11 bytes, all little endian):
 *   0xD6: seq, rx_coil | 0x80 when tracking, reads 0..3
 *   0xD7: seq, (bin << 4) | tx_coil, reads 4..7
 *   0xD9: seq, overshoot(read 3), read 3 conversions s0..s2, overshoot(read 0), overshoot(read 1)
 *   0xDA (LAB_VARIANTS per lab, one per j): seq, j | 0x80 when tracking, mean u16,
 *         min u16, max u16, mean absolute deviation * 16 u16 (over LAB_ROUNDS reads)
 */
#if LAB_ENABLE
#if LAB_SPECTRUM
#define LAB_EVERY 512U
#else
#define LAB_EVERY 64U
#endif
#define LAB_ROUNDS 8U
#define LAB_VARIANTS 8U
#define LAB_IDLE_SKIP 32U     /* lab calls to skip after the endpoint stayed busy 3 ms */
static uint8_t lab_seq;

#if !LAB_SPECTRUM
static const uint8_t lab_variant[LAB_VARIANTS] = {
  0x00U,
  SENSING_VARIANT_PA15_FAST,
  SENSING_VARIANT_ADC_STOCK,
  SENSING_VARIANT_ADDR_CLEAR,
  SENSING_VARIANT_ADC_STOCK | SENSING_VARIANT_ADDR_CLEAR,
  SENSING_VARIANT_CTL_SLOW,
  SENSING_VARIANT_PA15_FAST | SENSING_VARIANT_CTL_SLOW,
  SENSING_VARIANT_ADC_STOCK | SENSING_VARIANT_ADDR_CLEAR | SENSING_VARIANT_PA15_FAST,
};
#endif

/* Send one lab frame; give up on the whole lab when the host stops taking
 * frames (reader closed mid-run) instead of stalling 4 ms per remaining frame. */
#define LAB_SEND(pp) do { if (!telemetry_send_blocking(pp)) { lab_seq++; return; } } while (0)

/* Is a reader draining the vendor endpoint? (see the comment inside) */
static bool lab_gate(void) {
  /* Nobody is draining the vendor endpoint (no telemetry reader running): skip
   * the whole lab. The first lab image (0.6.5 entry 4) sent 11 blocking frames
   * of 4 ms each per call with no reader and made the pen unusable.
   * The pen report (same interface) or a window frame was queued just before
   * this call, so the endpoint is normally busy for up to a millisecond: an
   * immediate "ready" test (round 3, 0.6.5c) never passed and no lab frame was
   * ever sent. Wait up to 3 ms for it to free up; if it stays busy nobody is
   * reading, so skip the next LAB_IDLE_SKIP calls without waiting again. */
  static uint8_t lab_backoff;
  if (lab_backoff != 0U) {
    lab_backoff--;
    return false;
  }
  if (!tud_ready()) {
    return false;
  }
  {
    const uint32_t t0 = DWT_CYCCNT;
    while (report_pending() || !tud_hid_n_ready(ITF_VENDOR)) {
      service_usb();
      if ((uint32_t)(DWT_CYCCNT - t0) > (3UL * (CPU_HZ / 1000UL))) {
        lab_backoff = LAB_IDLE_SKIP;
        return false;
      }
    }
  }
  return true;
}

#if LAB_SPECTRUM
/* Spectrum lab (build.ps1 -Spectrum), round 7: SPEC_SERIES series of SPEC_N reads of the
 * same coil (service hook off), kept raw so the host can look at the pattern from read to
 * read. Round 6 found a period-4 pattern in back-to-back reads (std 2.7 %) that vanished
 * when the service hook spaced the reads (std 0.15 %); the series now vary the gap:
 *   0 gap 387 cycles (normal)      1 gap 387, ANCHOR variant     2 gap +72 (+1 us)
 *   3 gap +360 (+5 us)             4 gap +1440 (+20 us)          5 gap +5760 (+80 us)
 *   6 gap 387 + (i & 7) cycles (fine structure)
 * Frames (11 bytes, little endian):
 *   0xDC header: seq, series, duration us u16 (whole series), rx coil | 0x80 when tracking,
 *                (bin << 4) | tx coil, N u16
 *   0xDB data:   seq, (series << 5) | k, four u16 reads k*4 .. k*4+3; each u16 is the ADC
 *                value (12 bits) | min(burst start overshoot in cycles, 15) << 12 */
#define SPEC_N 128U
#define SPEC_SERIES 7U
static uint16_t spec_buf[SPEC_N];

static void telemetry_spectrum(uint8_t rx_coil, uint8_t tx_coil, uint8_t bin,
                               uint8_t tracking_now) {
  static const uint16_t gap_extra[SPEC_SERIES] = { 0U, 0U, 72U, 360U, 1440U, 5760U, 0U };
  if (!lab_gate()) {
    return;
  }
  sensing_set_service_hook(0);
  for (unsigned s = 0; s < SPEC_SERIES; s++) {
    const uint8_t variant = (s == 1U) ? SENSING_VARIANT_ANCHOR : 0U;
    const uint32_t t0 = DWT_CYCCNT;
    for (unsigned i = 0; i < SPEC_N; i++) {
      const uint32_t gap = 387U + gap_extra[s] + ((s == 6U) ? (i & 7U) : 0U);
      uint16_t v = sensing_read_coil_spaced(AXIS_X, rx_coil, tx_coil, bin, variant, gap);
      uint16_t samples[3];
      uint8_t ov;
      sensing_last_read_info(samples, &ov);
      spec_buf[i] = (uint16_t)((v & 0x0FFFU) | ((ov > 15U ? 15U : ov) << 12));
    }
    uint32_t dur = (DWT_CYCCNT - t0) / (CPU_HZ / 1000000UL);
    if (dur > 0xFFFFUL) {
      dur = 0xFFFFUL;
    }
    sensing_set_service_hook(service_usb);
    uint8_t p[VENDOR_REPORT_LEN];
    p[0] = 0xDCU;
    p[1] = lab_seq;
    p[2] = (uint8_t)s;
    put16(&p[3], (uint16_t)dur);
    p[5] = (uint8_t)((rx_coil & 0x1FU) | (tracking_now ? 0x80U : 0U));
    p[6] = (uint8_t)(((bin & 0x0FU) << 4) | (tx_coil & 0x0FU));
    put16(&p[7], (uint16_t)SPEC_N);
    p[9] = 0U;
    p[10] = 0U;
    LAB_SEND(p);
    for (unsigned k = 0; k < SPEC_N / 4U; k++) {
      p[0] = 0xDBU;
      p[1] = lab_seq;
      p[2] = (uint8_t)((s << 5) | k);
      for (unsigned j = 0; j < 4U; j++) {
        put16(&p[3U + 2U * j], spec_buf[k * 4U + j]);
      }
      LAB_SEND(p);
    }
    sensing_set_service_hook(0);
  }
  sensing_set_service_hook(service_usb);
  lab_seq++;
}
#define LAB_RUN telemetry_spectrum
#else
#define LAB_RUN telemetry_lab

static void telemetry_lab(uint8_t rx_coil, uint8_t tx_coil, uint8_t bin,
                          uint8_t tracking_now) {
  uint16_t v[LAB_VARIANTS * LAB_ROUNDS];
  uint16_t raw3[3] = { 0U, 0U, 0U };
  uint8_t over[8];
  if (!lab_gate()) {
    return;
  }
  for (unsigned i = 0; i < LAB_VARIANTS * LAB_ROUNDS; i++) {
    v[i] = sensing_read_coil_variant(AXIS_X, rx_coil, tx_coil, bin,
                                     lab_variant[i % LAB_VARIANTS]);
    if (i < 8U) {
      uint16_t samples[3];
      sensing_last_read_info(samples, &over[i]);
      if (i == 3U) {
        raw3[0] = samples[0];
        raw3[1] = samples[1];
        raw3[2] = samples[2];
      }
    }
  }
  uint8_t p[VENDOR_REPORT_LEN];
  p[0] = 0xD6U;
  p[1] = lab_seq;
  p[2] = (uint8_t)((rx_coil & 0x1FU) | (tracking_now ? 0x80U : 0U));
  for (unsigned i = 0; i < 4U; i++) {
    put16(&p[3U + 2U * i], v[i]);
  }
  LAB_SEND(p);
  p[0] = 0xD7U;
  p[1] = lab_seq;
  p[2] = (uint8_t)(((bin & 0x0FU) << 4) | (tx_coil & 0x0FU));
  for (unsigned i = 0; i < 4U; i++) {
    put16(&p[3U + 2U * i], v[4U + i]);
  }
  LAB_SEND(p);
  /* 0xD9: seq, overshoot of read 3, read 3's three conversions,
   * overshoot of reads 0 and 1. */
  p[0] = 0xD9U;
  p[1] = lab_seq;
  p[2] = over[3];
  put16(&p[3], raw3[0]);
  put16(&p[5], raw3[1]);
  put16(&p[7], raw3[2]);
  p[9] = over[0];
  p[10] = over[1];
  LAB_SEND(p);
  for (unsigned j = 0; j < LAB_VARIANTS; j++) {
    uint32_t sum = 0U;
    uint16_t lo = 0xFFFFU;
    uint16_t hi = 0U;
    for (unsigned r = 0; r < LAB_ROUNDS; r++) {
      const uint16_t x = v[r * LAB_VARIANTS + j];
      sum += x;
      if (x < lo) lo = x;
      if (x > hi) hi = x;
    }
    const int32_t mean = (int32_t)(sum / LAB_ROUNDS);
    uint32_t dev = 0U;
    for (unsigned r = 0; r < LAB_ROUNDS; r++) {
      const int32_t d = (int32_t)v[r * LAB_VARIANTS + j] - mean;
      dev += (uint32_t)((d < 0) ? -d : d);
    }
    p[0] = 0xDAU;
    p[1] = lab_seq;
    p[2] = (uint8_t)(j | (tracking_now ? 0x80U : 0U));
    put16(&p[3], (uint16_t)mean);
    put16(&p[5], lo);
    put16(&p[7], hi);
    put16(&p[9], (uint16_t)((dev * 16U) / LAB_ROUNDS));
    LAB_SEND(p);
  }
  lab_seq++;
}
#endif /* LAB_SPECTRUM */
#endif /* LAB_ENABLE */

#endif

#if !NO_UART_LOG
static void log_scan(uint32_t frames, uint32_t valid, uint8_t bin,
                     uint8_t xc, uint8_t yc) {
  uart_puts("frames=");
  uart_u32(frames);
  uart_puts(" valid=");
  uart_u32(valid);
  uart_puts(" bin=");
  uart_u32(bin);
  uart_puts(" xc=");
  uart_u32(xc);
  uart_puts(" yc=");
  uart_u32(yc);
  uart_puts("\r\n");
}
#endif

int main(void) {
  board_init();

  uart_puts("\r\nH420X cleanroom ");
  uart_puts(FW_VERSION);
  uart_puts("\r\n");

  const tusb_rhport_init_t rh_init = {
      .role = TUSB_ROLE_DEVICE,
      .speed = TUSB_SPEED_FULL,
  };
  if (!tusb_init(0, &rh_init)) {
    uart_puts("usb=init-failed\r\n");
    for (;;) {
      uart_poll();
    }
  }
  uart_puts("usb=init-ok\r\n");
#if LATE_CONNECT
  /* Keep the host from adding the HID interfaces until the ADC/GPIO init is
   * done. OTD scans immediately on the device-add event and reads string 201;
   * a scan during sensing_init() fails and OTD gives up until a manual Detect. */
  tud_disconnect();
#endif

  /* USB service must run from the first moment the D+ pull-up is enabled.
   * Do not run the ADC calibration or the startup bin sweep with interrupts
   * masked; a slow or absent ADC clock must not prevent enumeration. */
  report_init();
  sensing_set_service_hook(service_usb);
#if H420X_PROFILE == 4
  sensing_set_prep_hook(service_usb);
#endif
  __asm volatile ("cpsie i");

  dwt_init();
#if LOOP_MODES && (LOOP_DIAG || LOOP_HEARTBEAT)
  diag_ms_cyc = DWT_CYCCNT;
#endif
#if LATE_CONNECT
  /* Initialize the analog side before the host can enumerate, then connect.
   * The device-add event OTD sees is now on a fully initialized device. */
  sensing_init();
#if H420X_PROFILE == 3
  pen_init();
#endif
  tud_connect();
#endif
  uint32_t usb_start = DWT_CYCCNT;
  while (!tud_ready() &&
         (uint32_t)(DWT_CYCCNT - usb_start) < (2UL * CPU_HZ)) {
    service_usb();
  }
  uart_puts(tud_ready() ? "usb=ready\r\n" : "usb=not-ready\r\n");

#if SENSING_TELEMETRY
  /* Prove the telemetry path before touching the ADC. If these frames stop,
   * sensing_init() is the blocking step. */
  for (uint32_t n = 0; n < 40000U && telemetry_frames < 5000U; n++) {
    telemetry_status(0x02U, 0U, 0U, 0U, 0U, 0U);
    service_usb();
  }
#endif

#if !LATE_CONNECT
  sensing_init();
#if H420X_PROFILE == 3
  pen_init();
#endif
#endif
  uart_puts("adc=");
  uart_u32(sensing_adc_ok());
  uart_puts("\r\n");
#if SENSING_TELEMETRY
  telemetry_status((uint8_t)(0x02U | 0x08U |
                             (sensing_adc_ok() ? 0x01U : 0U)),
                   0U, 0U, 0U, 0U, 0U);
#endif
  uint8_t bin = SENSING_DEFAULT_BIN;
  sensing_set_bin(bin);
#if LOOP_MODES
  loop_apply(0U);
#endif
  if (!sensing_adc_ok()) {
    uart_puts("sensing=adc-timeout\r\n");
  }
#if SENSING_TELEMETRY
  telemetry_status((uint8_t)(0x02U | 0x08U | 0x10U |
                             (sensing_adc_ok() ? 0x01U : 0U)),
                   bin, 0U, 0U, 0U, 0U);
#endif
  uart_puts("sensing=ready bin=");
  uart_u32(bin);
  uart_puts("\r\n");

  tracker_state tracker;
  tracker_init(&tracker);

  uint16_t xa[4];
  uint16_t ya[4];
  uint32_t frames = 0;
  uint32_t valid = 0;
#if !NO_UART_LOG
  uint32_t last_log = 0;
#endif
  uint8_t tracking = 0;
  uint8_t present_run = 0U;   /* good frames in a row, saturates at PRESENT_ARM_FRAMES */
  uint8_t absent_run = 0U;    /* failed presence frames in a row */
  uint8_t search_index = 0U;
#if ROBUST_LOCK
  uint8_t cand_x = 0U;        /* coils that won the previous search, 0 = none */
  uint8_t cand_y = 0U;
  uint8_t search_fails = 0U;  /* searches in a row that found no pen */
  uint8_t invalid_run = 0U;   /* flat or weak windows in a row while tracking */
#endif
#if SENSING_TELEMETRY
  /* Lock-loss diagnostics, sent in the spare xp/yp fields of the 0xD1 status
   * frame while tracking: xp = lock drops since boot, yp = (last drop reason
   * << 12) | (invalid windows since boot & 0xFFF). Reasons: 1 invalid-run
   * (flat/weak window), 2 lost (read failure or 3 re-centerings), 3 absent
   * (no pen signal for 50 frames), 4 center out of range. */
  uint16_t drop_count = 0U;
  uint16_t invalid_total = 0U;
  uint8_t drop_reason = 0U;
#define NOTE_DROP(r) do { drop_count++; drop_reason = (uint8_t)(r); } while (0)
#define DIAG_LOSS ((uint16_t)(((uint16_t)drop_reason << 12) | (invalid_total & 0x0FFFU)))
#elif LOOP_MODES
#define NOTE_DROP(r) loop_note_drop((uint8_t)(r))
#else
#define NOTE_DROP(r) do { } while (0)
#endif
  for (;;) {
#if H420X_PROFILE == 4
    if (!tracking) {
      service_usb();
    }
#else
    service_usb();
#endif

#if LOOP_MODES && (LOOP_DIAG || LOOP_HEARTBEAT)
    diag_clock();
    diag_usb_watch();
#if QUIET_HEARTBEAT
    const uint8_t priming = (diag_prime < DIAG_PRIME_FRAMES) ? 1U : 0U;
    const uint32_t hb_gap = priming ? 1U : DIAG_HB_MS;
    if ((diag_stuck != 0U || priming != 0U) &&
        (uint32_t)(diag_ms - diag_last_pub) >= hb_gap) {
#else
    if ((uint32_t)(diag_ms - diag_last_pub) >= DIAG_HB_MS) {
#endif
      diag_heartbeat(tracking);
      diag_last_pub = diag_ms;
#if QUIET_HEARTBEAT
      if (diag_prime < DIAG_PRIME_FRAMES) {
        diag_prime++;
      }
#endif
      report_task();
    }
#endif
#if ROBUST_LOCK
    /* Lit only while reports are flowing, so a lock that never produces a
     * valid frame does not flash the LED. */
#if LOOP_MODES && LOOP_DIAG
    {
      const uint8_t lit = (tracking && present_run >= PRESENT_ARM_FRAMES) ? 1U : 0U;
      const uint8_t blink = (diag_stuck == 1U) ? (uint8_t)((diag_ms >> 6) & 1U)
                                               : (uint8_t)((diag_ms >> 9) & 1U);
      sensing_led_tracking((diag_stuck != 0U) ? blink : lit);
    }
#else
    sensing_led_tracking((tracking && present_run >= PRESENT_ARM_FRAMES) ? 1U : 0U);
#endif
#else
    sensing_led_tracking(tracking);
#endif

#if SENSING_TELEMETRY
    if (selftest_ready) {
      telemetry_selftest_one(selftest_index,
                             selftest_values[selftest_index]);
      selftest_index = (uint8_t)((selftest_index + 1U) % 6U);
    }
#endif

#if SENSING_TELEMETRY
    telemetry_status((uint8_t)(0x02U | 0x08U |
                               (sensing_adc_ok() ? 0x01U : 0U) |
                               (tracking ? 0x04U : 0U)),
                     bin, tracker.x_center, tracker.y_center,
                     drop_count, DIAG_LOSS);
#endif

    if (!sensing_adc_ok()) {
      /* Keep the USB transport healthy even if the analog front end did not
       * calibrate; the diagnostic log identifies this case. */
      continue;
    }

    if (!tracking) {
#if LOOP_MODES && LOOP_DIAG
      diag_searches++;
#endif
#if LOOP_MODES
      if (loop_mode != 0U) {
#if !defined(LOOP_MODE_FIXED)
        /* Round 16: the next lock starts the next rotation mode with a fresh
         * block, so a mode that loses the pen cannot hold the rotation. */
        loop_rot = (uint8_t)((loop_rot % (LOOP_MODE_COUNT - 1U)) + 1U);
        loop_frames = 0U;
#endif
        loop_switch(&tracker, 0U);   /* the search and the bin sweep always use the plain read */
      }
#endif
      const uint8_t sbin = search_bins[search_index] ? search_bins[search_index] : bin;
      uint16_t xp = 0;
      uint16_t yp = 0;
      uint16_t xf = 0;
      uint16_t yf = 0;
      uint8_t yc = sensing_self_search(AXIS_Y, sbin, &yp, &yf);
      uint8_t xc = sensing_self_search(AXIS_X, sbin, &xp, &xf);
#if LOOP_MODES && LOOP_DIAG
      {
        const uint32_t pk = ((xp > yp) ? xp : yp) >> 3;
        diag_peak = (uint8_t)((pk > 255U) ? 255U : pk);
      }
#endif
#if ROBUST_LOCK
      const uint32_t need = (search_fails >= ACQUIRE_LOOSE_PASSES) ? ACQUIRE_LOOSE_RISE : ACQUIRE_MIN_RISE;
      uint8_t found = (xc != 0U && yc != 0U &&
                       xp > ACQUIRE_MIN_PEAK && yp > ACQUIRE_MIN_PEAK &&
                       (uint32_t)xp > (uint32_t)xf + need &&
                       (uint32_t)yp > (uint32_t)yf + need) ? 1U : 0U;
      /* Two searches in a row must pick the same coils (+-1) before a lock. */
      uint8_t candidate = 0U;
      if (found) {
        const uint8_t same = (cand_x != 0U && coil_near(xc, cand_x) &&
                              coil_near(yc, cand_y)) ? 1U : 0U;
        cand_x = xc;
        cand_y = yc;
        if (!same) {
          found = 0U;
          candidate = 1U;
        }
      } else {
        cand_x = 0U;
        cand_y = 0U;
        if (search_fails < 255U) {
          search_fails++;
        }
      }
      if (found) {
#else
      if (xc != 0U && yc != 0U &&
          xp > ACQUIRE_MIN_PEAK && yp > ACQUIRE_MIN_PEAK &&
          (uint32_t)xp > (uint32_t)xf + ACQUIRE_MIN_RISE &&
          (uint32_t)yp > (uint32_t)yf + ACQUIRE_MIN_RISE) {
#endif
#if H420X_PROFILE == 3
        pen_set_bin(sbin);
        bin = pen_acquire(xc, yc);
        sensing_set_bin(bin);
#else
        bin = sensing_sweep_bin(xc, yc, 0);
#endif
        if (xc < X_CENTER_MIN) xc = X_CENTER_MIN;
        if (xc > X_CENTER_MAX) xc = X_CENTER_MAX;
        if (yc < Y_CENTER_MIN) yc = Y_CENTER_MIN;
        if (yc > Y_CENTER_MAX) yc = Y_CENTER_MAX;
        tracker_set_centers(&tracker, xc, yc);
        tracking = 1;
#if LOOP_MODES && LOOP_DIAG
        diag_locks++;
#endif
#if LOOP_MODES
#if defined(LOOP_MODE_FIXED) && !LOOP_WARM && !LOOP_ARM
        loop_switch(&tracker, (uint8_t)(LOOP_MODE_FIXED));   /* cold: the fixed mode from the first frame */
#else
        loop_switch(&tracker, 0U);   /* HOOK until the lock's first report is out */
#endif
#if LOOP_WARM
        loop_warm = LOOP_BLOCK;
#endif
#endif
        present_run = 0U;
        absent_run = 0U;
        search_index = 0U;
#if ROBUST_LOCK
        search_fails = 0U;
        cand_x = 0U;
        cand_y = 0U;
        invalid_run = 0U;
#endif
        uart_puts("acquired xc=");
        uart_u32(xc);
        uart_puts(" yc=");
        uart_u32(yc);
        uart_puts(" bin=");
        uart_u32(bin);
        uart_puts("\r\n");
      } else {
        tracker_forget(&tracker);
#if ROBUST_LOCK
        if (!candidate) {
          search_index = (uint8_t)((search_index + 1U) % SEARCH_BIN_COUNT);
        }
#else
        search_index = (uint8_t)((search_index + 1U) & 3U);
#endif
      }
#if SENSING_TELEMETRY
      {
        uint16_t pr[4] = { 0, 0, 0, 0 };
        sensing_adc_pa2_probe(pr);
        for (uint32_t n = 0; n < 4000U; n++) {
          if (telemetry_probe(pr)) {
            break;
          }
          service_usb();
        }
      }
      telemetry_status((uint8_t)(0x02U | 0x08U | 0x20U |
                                 (sensing_adc_ok() ? 0x01U : 0U)),
                       bin, xc, yc, xp, yp);
      telemetry_search(xc, yc, xp, xf, yp, yf);
      {
        uint16_t tw[4];
        if (sensing_read_window(AXIS_X, xc, yc, bin, tw) == 0) {
          telemetry_window(AXIS_X, xc, tw);
        }
        if (sensing_read_window(AXIS_Y, yc, xc, bin, tw) == 0) {
          telemetry_window(AXIS_Y, yc, tw);
        }
      }
#if LAB_ENABLE
      {
        /* Search iterations come every ~10 ms: one lab (64 reads + 11 frames)
         * per 32 of them gives the pen-away noise floor without slowing the
         * acquisition. */
        static uint8_t lab_search_div;
        if ((++lab_search_div & 31U) == 0U) {
          LAB_RUN(10U, 7U, bin, tracking);
        }
      }
#endif
#endif
      tud_task();
      report_task();
      continue;
    }

#if H420X_PROFILE == 3
    pace_frame_start();
#endif
#if H420X_PROFILE == 4
    frame_lock_wait();
    sensing_frame_begin();
#endif
#if LOOP_MODES
    const uint8_t loop_quiet = (loop_mode != 0U) ? 1U : 0U;
    {
      const uint8_t published = loop_published;
      loop_published = 0U;
      if (loop_sof) {
        loop_sof_wait();
      } else if (loop_in) {
        loop_in_wait(published);
      }
    }
    if (loop_quiet) {
      sensing_frame_begin();
    }
#endif
    /* Up to three passes. A pass after the first re-reads only the axes the
     * tracker re-centered (pen far past a coil boundary within one frame);
     * the other axis keeps this frame's window. */
    tracker_output out;
    uint8_t scan_x = 1U;
    uint8_t scan_y = 1U;
    uint8_t fit_xc = tracker.x_center;
    uint8_t fit_yc = tracker.y_center;
    unsigned passes = 0U;
    int lost = 0;
#if LOOP_MODES
    const uint8_t bin_tracked = bin;
    if (loop_cad_bin != 0U) {
      bin = loop_cad_bin;   /* round 16: pinned-bin mode, every read of this frame at that bin */
    }
    loop_lost_why = 0U;
#endif
    for (;;) {
#if LOOP_MODES
      if (loop_cadence && (passes == 0U || loop_cad_rc != 0U)) {
        if (sensing_read_frame(tracker.x_center, tracker.y_center, bin, xa, ya) != 0) {
          loop_lost_why = 5U;
          lost = 1;
          break;
        }
      } else
#endif
      {
        if (scan_x && sensing_read_window(AXIS_X, tracker.x_center,
                                          tracker.y_center, bin, xa) != 0) {
          lost = 1;
          break;
        }
        if (scan_y && sensing_read_window(AXIS_Y, tracker.y_center,
                                          tracker.x_center, bin, ya) != 0) {
          lost = 1;
          break;
        }
      }
      passes++;
      fit_xc = tracker.x_center;
      fit_yc = tracker.y_center;
      tracker_update(&tracker, &xa[1], &ya[1], &out);
      if ((out.flags & (TRACK_RECENTER_X | TRACK_RECENTER_Y)) == 0U) {
        break;
      }
      if (passes >= 3U) {
        lost = 1;
        break;
      }
      scan_x = (out.flags & TRACK_RECENTER_X) ? 1U : 0U;
      scan_y = (out.flags & TRACK_RECENTER_Y) ? 1U : 0U;
    }
#if LOOP_MODES
#if !RELEASE_AUX_ZERO && !FRAME_AUX
    const uint8_t bin_read = bin;
#endif
    bin = bin_tracked;
    if (lost && loop_lost_why == 0U) {
      loop_lost_why = (passes >= 3U) ? 7U : 6U;   /* three re-centerings, or a window read failed */
    }
#endif
#if H420X_PROFILE == 4
    const uint32_t frame_cyc = DWT_CYCCNT - lock_t0;
    sensing_frame_end();
#endif
#if LOOP_MODES
    if (loop_quiet) {
      sensing_frame_end();
    }
    if (loop_gap != 0U) {
      sensing_frame_gap(loop_gap);
    }
#endif
    if (lost) {
      NOTE_DROP(2);
      tracking = 0;
      tracker_forget(&tracker);
#if H420X_PROFILE == 3
      pen_release();
#elif H420X_PROFILE == 4
      lock_have_prev = 0U;
#endif
      continue;
    }
    (void)fit_xc;  /* used only by telemetry and profile 3 */
    (void)fit_yc;

    const uint8_t present = (window_rise(xa) > TRACK_MIN_RISE &&
                             window_rise(ya) > TRACK_MIN_RISE) ? 1U : 0U;

#if SENSING_TELEMETRY
    telemetry_window(AXIS_X, fit_xc, xa);
    telemetry_window(AXIS_Y, fit_yc, ya);
#endif

#if SENSING_TELEMETRY
    telemetry_status((uint8_t)(0x02U | 0x08U | 0x04U | 0x20U |
                               (sensing_adc_ok() ? 0x01U : 0U) |
                               (((out.flags & TRACK_VALID) && present) ? 0x40U : 0U) |
                               (present ? 0U : 0x80U)),
                     bin, tracker.x_center, tracker.y_center,
                     drop_count, DIAG_LOSS);
#endif
    if ((out.flags & TRACK_VALID) == 0U) {
#if SENSING_TELEMETRY
      invalid_total++;
#endif
#if ROBUST_LOCK
      /* A flat or weak window withholds this frame's report only; the lock
       * is dropped after INVALID_DROP_FRAMES of them in a row. */
      if (++invalid_run < INVALID_DROP_FRAMES) {
        continue;
      }
#endif
      NOTE_DROP(1);
      tracking = 0;
      tracker_forget(&tracker);
#if H420X_PROFILE == 3
      pen_release();
#elif H420X_PROFILE == 4
      lock_have_prev = 0U;
#endif
      continue;
    }
#if ROBUST_LOCK
    invalid_run = 0U;
#endif
    if (!present) {
      present_run = 0U;
      absent_run++;
      if (absent_run >= ABSENT_SEARCH_FRAMES) {
        NOTE_DROP(3);
        tracking = 0;
        tracker_forget(&tracker);
#if H420X_PROFILE == 3
        pen_release();
#elif H420X_PROFILE == 4
        lock_have_prev = 0U;
#endif
      }
      continue;
    }
    absent_run = 0U;
    if (present_run < PRESENT_ARM_FRAMES) {
      present_run++;
      if (present_run < PRESENT_ARM_FRAMES) {
        continue;
      }
    }

    if (tracker.x_center < X_CENTER_MIN ||
        tracker.x_center > X_CENTER_MAX ||
        tracker.y_center < Y_CENTER_MIN ||
        tracker.y_center > Y_CENTER_MAX) {
      NOTE_DROP(4);
      tracking = 0;
      tracker_forget(&tracker);
#if H420X_PROFILE == 3
      pen_release();
#elif H420X_PROFILE == 4
      lock_have_prev = 0U;
#endif
      continue;
    }

#if H420X_PROFILE == 3
    /* A frame that needed a re-centering pass defers the probe (and its
     * quiet time) to the next frame; the probe uses this frame's centers. */
    if (passes == 1U) {
      (void)pen_maintain(fit_xc, fit_yc);
    }
    bin = pen_bin();
    {
      uint8_t status;
      uint16_t pressure;
      pen_fields(pen_state(), &status, &pressure);
#if OUTPUT_SMOOTH
      smooth_apply(&out.x, &out.y);
#endif
      last_x = out.x;
      last_y = out.y;
      pen_in_range = 1U;
      pace_wait();
      report_publish(out.x, out.y, pressure, status);
    }
#elif H420X_PROFILE == 4
    {
      uint32_t frame_us = frame_cyc / (CPU_HZ / 1000000UL);
      if (frame_us > 2047U) {
        frame_us = 2047U;
      }
      report_set_aux((uint16_t)(frame_us | ((uint32_t)passes << AUX_PASSES_SHIFT) |
                                lock_flags));
      report_publish(out.x, out.y, 0U, 0x80U);
      if (!tud_hid_n_ready(ITF_VENDOR)) {
        tud_task();
      }
      lock_have_prev = 1U;
      lock_prev_passes = (uint8_t)passes;
    }
#else
#if OUTPUT_SMOOTH
    smooth_apply(&out.x, &out.y);
#endif
#if LOOP_MODES
#if R55_MODES && R55_AUX
    {
      /* Round 55 aux, see the loop_defs table. */
      uint32_t low = loop_in ? (uint32_t)loop_in_wait_us
                             : sensing_frame_first_late_cyc() / (CPU_HZ / 1000000UL);
      if (low > 63U) {
        low = 63U;
      }
      uint32_t spread = sensing_r55_land_spread();
      if (spread > 15U) {
        spread = 15U;
      }
      const uint32_t aux = low | (spread << 6) |
                           ((passes > 1U) ? 0x0400U : 0U) |
                           (loop_in_flag ? 0x0800U : 0U) |
                           ((loop_cadence && sensing_frame_max_late_cyc() != 0U) ? 0x1000U : 0U) |
                           ((uint32_t)loop_mode << 13);
      report_set_aux((uint16_t)aux);
      loop_maint = 0U;
    }
#elif WAVE_AUX
    {
      /* Round 41 diagnostic (build.ps1 -WaveAux): the aux bytes carry one raw window read of this
       * frame, rotating through the seven reads the fit uses: channel 0..3 = X slots 0..3,
       * channel 4..6 = Y slots 1..3. Bits 0-11 value (clamped to 4095), bits 12-14 channel,
       * bit 15 = bit 4 of the frame counter (frame phase, to compare with the sent-report phase). */
      const uint32_t wc = frames % 7U;
      uint32_t wv = (wc < 4U) ? (uint32_t)xa[wc] : (uint32_t)ya[wc - 3U];
      if (wv > 4095U) {
        wv = 4095U;
      }
      report_set_aux((uint16_t)(wv | (wc << 12) | (((frames >> 4) & 1U) << 15)));
      loop_maint = 0U;
    }
#elif RELEASE_AUX_ZERO && !FRAME_AUX
    report_set_aux(0U);
    loop_maint = 0U;
#else
    {
#if FRAME_AUX
#if defined(MODE1_SOF)
      /* Round 53 SOF-locked frameaux: aux bits 0-9 = SOF-wait slack in us (time from
       * the end of the frame's work to the next USB frame edge, i.e. the margin),
       * bit 10 = the previous frame ran past its USB frame (missed a SOF). */
      /* Bits 11-14 = read index (0..13) at whose start the host had taken the
       * previous report, 14 = only by frame end, 15 = not during this frame. */
      const uint32_t low = (uint32_t)loop_sof_slack | (loop_sof_over ? 0x400U : 0U);
      const uint32_t aux = (low & 0x7FFU) | (loop_in_index(sensing_in_mask()) << 11);
#elif defined(MODE1_IN)
      /* Round 54 IN-locked frameaux: bits 0-9 = IN wait in us (from the end of
       * the frame's work to the host taking the report, i.e. the margin;
       * saturating 1023), bit 10 = this frame did not start on an IN completion
       * (timeout, or the SOF fallback after a frame without a report). Bits
       * 11-14 = read index (0..13) at whose start the USB frame number had
       * changed (where the SOF fell; sensing.c samples DSTS instead of XFRC in
       * this build), 14 = only by frame end, 15 = no SOF during the frame. */
      const uint32_t low = (uint32_t)loop_in_wait_us | (loop_in_flag ? 0x400U : 0U);
      const uint32_t aux = (low & 0x7FFU) | (loop_in_index(sensing_in_mask()) << 11);
#else
      uint32_t low = sensing_frame_period_us();
      if (low > 2047U) {
        low = 2047U;
      }
      const uint32_t aux = low | ((uint32_t)loop_mode << 13);
#endif
#else
      uint32_t low;
      uint32_t flag = 0U;
      if (loop_sof) {
        low = (uint32_t)loop_sof_slack / 4U;
        flag = loop_sof_over;
      } else if (loop_in) {
        low = (uint32_t)loop_in_wait_us / 4U;
        flag = loop_in_flag;
      } else {
        low = sensing_frame_first_late_cyc() / (CPU_HZ / 1000000UL);
      }
      if (low > 63U) {
        low = 63U;
      }
      if (loop_mode == 0U) {
        low = (uint32_t)loop_last_why | ((uint32_t)loop_last_len << 3);   /* round 17 */
      }
      const uint32_t idx = (uint32_t)bin_read & 0x0FU;   /* round 16: bin this frame was read at */
      if (loop_cadence && sensing_frame_max_late_cyc() != 0U) {
        flag = 1U;
      }
      if (loop_mode == 0U && loop_drops_since_pub > 1U) {
        flag = 1U;
      }
#if LOOP_DIAG
      low = (diag_ms >> 2) & 63U;
      flag = (loop_setup_count != diag_setup_at_pub || loop_usb_events != diag_events_at_pub) ? 1U : 0U;
#endif
      const uint32_t aux = low | (idx << 6) |
                           ((passes > 1U) ? 0x0400U : 0U) |
                           (loop_maint ? 0x0800U : 0U) |
                           ((flag != 0U) ? 0x1000U : 0U) |
                           ((uint32_t)loop_mode << 13);
#endif
      report_set_aux((uint16_t)aux);
    }
    loop_maint = 0U;
#endif
#endif
    report_publish(out.x, out.y, 0U, 0x80U);
#if LOOP_MODES
    loop_published = 1U;
    loop_lock_frames++;
    loop_drops_since_pub = 0U;
#if LOOP_DIAG || LOOP_HEARTBEAT
    diag_last_pub = diag_ms;
#if LOOP_DIAG
    diag_setup_at_pub = loop_setup_count;
    diag_events_at_pub = loop_usb_events;
#endif
#endif
#endif
#endif
    frames++;
    valid++;
    report_task();
#if LOOP_MODES && CAD_RESET_FRAMES
    if ((frames % (uint32_t)CAD_RESET_FRAMES) == 0U) {
      loop_apply(loop_mode);   /* reset the cadence schedule without changing mode */
    }
#endif
#if LOOP_MODES && !defined(LOOP_MODE_FIXED)
    if (loop_mode == 0U) {
      loop_switch(&tracker, loop_rot);   /* the lock's first report is out: resume the rotation */
    } else {
      loop_frames++;
      if ((loop_frames % LOOP_BLOCK) == 0U) {
        loop_rot = (uint8_t)((loop_rot % (LOOP_MODE_COUNT - 1U)) + 1U);   /* 1..7 */
        loop_switch(&tracker, loop_rot);
      }
    }
#elif LOOP_WARM
    if (loop_warm != 0U) {
      loop_warm--;
      if (loop_warm == 0U) {
        loop_switch(&tracker, (uint8_t)(LOOP_MODE_FIXED));
      }
    }
#elif LOOP_ARM
    if (loop_mode == 0U) {
      loop_switch(&tracker, (uint8_t)(LOOP_MODE_FIXED));   /* the lock's first report is out: leave HOOK */
    }
#endif
#if SENSING_TELEMETRY && LAB_ENABLE
    if ((frames % LAB_EVERY) == 0U) {
      LAB_RUN(tracker.x_center, tracker.y_center, bin, 1U);
    }
#endif

#if SENSING_DIAG
    if ((frames % 100U) == 0U) {
      uart_puts("a x=");
      uart_u32(xa[0]); uart_puts(",");
      uart_u32(xa[1]); uart_puts(",");
      uart_u32(xa[2]); uart_puts(",");
      uart_u32(xa[3]);
      uart_puts(" y=");
      uart_u32(ya[0]); uart_puts(",");
      uart_u32(ya[1]); uart_puts(",");
      uart_u32(ya[2]); uart_puts(",");
      uart_u32(ya[3]);
      uart_puts(" pos=");
      uart_u32((uint32_t)out.x); uart_puts(",");
      uart_u32((uint32_t)out.y);
      uart_puts("\r\n");
    }
#endif

#if H420X_PROFILE != 3
#if LOOP_MODES
    if (!loop_maint_off &&
        (frames % ((loop_sof || loop_in) ? LOOP_MAINT_FRAMES_SOF : LOOP_MAINT_FRAMES)) == 0U) {
#else
    if ((frames % 128U) == 0U) {
#endif
      bin = sensing_maintain_bin(tracker.x_center, tracker.y_center, bin);
#if H420X_PROFILE == 4
      lock_maint = 1U;
#endif
#if LOOP_MODES
      loop_maint = 1U;
#endif
    }
#endif

#if !NO_UART_LOG
    if ((frames - last_log) >= 900U) {
      last_log = frames;
      log_scan(frames, valid, bin, tracker.x_center, tracker.y_center);
    }

    uart_poll();
#endif
  }
}

#endif
