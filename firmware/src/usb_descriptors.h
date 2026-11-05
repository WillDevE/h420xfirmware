/*
 * USB descriptor and report layout shared by the application.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef H420X_USB_DESCRIPTORS_H
#define H420X_USB_DESCRIPTORS_H

#include <stdint.h>

#define H420X_VID               0x256CU
#define H420X_PID               0x0064U

#define ITF_VENDOR              0U
#define ITF_DIGITIZER           1U
#define ITF_KEYBOARD            2U

#define EPNUM_VENDOR            0x81U
#define EPNUM_DIGITIZER         0x82U
#define EPNUM_KEYBOARD          0x83U

#define REPORT_ID_VENDOR        8U
#define REPORT_ID_DIGITIZER     10U
#define REPORT_ID_KEYBOARD      3U

#define VENDOR_REPORT_LEN       11U   /* 88-bit payload, no report ID */
#define DIGITIZER_REPORT_LEN    9U    /* 72-bit payload, no report ID */
#define KEYBOARD_REPORT_LEN     7U    /* 56-bit payload, no report ID */

#define CONFIG_TOTAL_LEN        84U

extern const uint8_t h420x_device_descriptor[18];
extern const uint8_t h420x_config_descriptor[CONFIG_TOTAL_LEN];
extern const uint8_t h420x_report_vendor[18];
extern const uint8_t h420x_report_digitizer[93];
extern const uint8_t h420x_report_keyboard[40];

/* OTD detection strings (non-standard indices 200 and 201). */
extern const char h420x_string_200[];
extern const char h420x_string_201[];

#endif /* H420X_USB_DESCRIPTORS_H */
