/*
 * TinyUSB configuration for the H420X clean-room application.
 *
 * Three HID interfaces, matching the stock composite:
 *   MI_00 vendor-defined report ID 8  (EP 0x81, 64 B, 1 ms)
 *   MI_01 digitizer report ID 10      (EP 0x82, 16 B, 1 ms)
 *   MI_02 keyboard report ID 3        (EP 0x83, 64 B, 8 ms)
 *
 * DWC2 is used in slave mode: no DMA, no cache maintenance, smallest and
 * most deterministic path for a 1 kHz report loop.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef H420X_TUSB_CONFIG_H
#define H420X_TUSB_CONFIG_H

#define CFG_TUSB_MCU              OPT_MCU_GD32F350
#define CFG_TUSB_OS               OPT_OS_NONE
#define CFG_TUSB_DEBUG            0

#define CFG_TUD_ENABLED           1
#define CFG_TUD_MAX_SPEED         OPT_MODE_FULL_SPEED
#define CFG_TUD_ENDPOINT0_SIZE    8

#define CFG_TUD_HID               3
#define CFG_TUD_HID_EP_BUFSIZE    64

#define CFG_TUD_DWC2_SLAVE_ENABLE 1
#define CFG_TUD_DWC2_DMA_ENABLE   0

#define CFG_TUSB_MEM_ALIGN        __attribute__((aligned(4)))

#endif /* H420X_TUSB_CONFIG_H */
