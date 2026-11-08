/*
 * Clean-room USB descriptors for the H420X application.
 *
 * The report descriptors are functional protocol data reconstructed from the
 * HID 1.11 specification and the published USB HID usage tables. They encode
 * the report contract that OpenTabletDriver expects for 256C:0064:
 *
 *   MI_00 vendor page 0xFF00, report ID 8, 88-bit input
 *   MI_01 digitizer page 0x0D, report ID 10
 *   MI_02 keyboard page 0x01, report ID 3
 *
 * The device and configuration descriptor field values (EP0 size, bcdDevice,
 * bcdHID, power attributes) are pinned to the values used by the stock
 * H420X/T223 firmware so that host-side enumeration is indistinguishable.
 *
 * String descriptor 201 is an OTD detection contract: the upstream Huion
 * H420X configuration matches it against HUION_(T210|T223)_[0-9]{6}. String
 * 200 is read once during OTD initialization. Both are ordinary USB string
 * descriptors and contain no vendor code.
 *
 * No Huion source code or binary blob is included. The byte sequences below
 * are written item by item with their HID meanings.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "tusb.h"
#include "usb_descriptors.h"

#ifndef STOCK_IDENTITY
#define STOCK_IDENTITY 0   /* build.ps1 -StockIdentity: stock H420X strings/serial */
#endif
#ifndef BCD_DEVICE
#define BCD_DEVICE 0x0112  /* build.ps1 -BcdDevice <n>: cache-bust release number */
#endif
#ifndef SERIAL_NUMBER
/* Stable stock H420X serial. Keep this constant for every test and release image:
 * Windows/OTD bind the device by serial, so a per-candidate serial makes each
 * flash look like a new tablet and breaks automatic pickup. build.ps1
 * -SerialNumber <n> can override it for a deliberate identity test. */
#define SERIAL_NUMBER 20210233
#endif
#define H420X_STR2(x) #x
#define H420X_STR(x) H420X_STR2(x)

_Static_assert(CONFIG_TOTAL_LEN == 84, "stock configuration descriptor is 84 bytes");
_Static_assert(sizeof(h420x_device_descriptor) == 18, "device descriptor is 18 bytes");
_Static_assert(sizeof(h420x_report_vendor) == 18, "vendor report descriptor is 18 bytes");
_Static_assert(sizeof(h420x_report_digitizer) == 93, "digitizer report descriptor is 93 bytes");
_Static_assert(sizeof(h420x_report_keyboard) == 40, "keyboard report descriptor is 40 bytes");

/* ------------------------------------------------------------------------- */
/* Device descriptor                                                         */
/* ------------------------------------------------------------------------- */
const uint8_t h420x_device_descriptor[18] = {
    18, 0x01,             /* bLength, DEVICE                      */
    0x00, 0x02,           /* bcdUSB 2.00                          */
    0x00,                 /* bDeviceClass: per-interface          */
    0x00,                 /* bDeviceSubClass                      */
    0x00,                 /* bDeviceProtocol                      */
    8,                    /* bMaxPacketSize0 (stock value)        */
    0x6C, 0x25,           /* idVendor  0x256C                     */
    0x64, 0x00,           /* idProduct 0x0064                     */
#if BCD_DEVICE == 0x0112
    0x12, 0x01,           /* bcdDevice 1.12 (stock value)         */
#else
    (uint8_t)(BCD_DEVICE & 0xFF), (uint8_t)(BCD_DEVICE >> 8),
#endif
    1, 2, 3,              /* manufacturer, product, serial        */
    1,                    /* bNumConfigurations                   */
};

/* ------------------------------------------------------------------------- */
/* Configuration descriptor: 3 interfaces, 84 bytes                          */
/* ------------------------------------------------------------------------- */
const uint8_t h420x_config_descriptor[CONFIG_TOTAL_LEN] = {
    /* Configuration */
    9, 0x02,              /* bLength, CONFIGURATION               */
    CONFIG_TOTAL_LEN, 0,  /* wTotalLength                         */
    3,                    /* bNumInterfaces                       */
    1,                    /* bConfigurationValue                  */
    0,                    /* iConfiguration                       */
    0xA0,                 /* bmAttributes: bus powered, wakeup    */
    0xF0,                 /* bMaxPower: 480 mA (stock value)      */

    /* Interface 0: vendor-defined pen reports */
    9, 0x04,              /* bLength, INTERFACE                   */
    ITF_VENDOR, 0,        /* bInterfaceNumber, bAlternateSetting  */
    1,                    /* bNumEndpoints                        */
    0x03,                 /* bInterfaceClass: HID                 */
    0x01,                 /* bInterfaceSubClass: boot            */
    0x02,                 /* bInterfaceProtocol: mouse           */
    0,                    /* iInterface                           */
    9, 0x21,              /* bLength, HID                         */
    0x10, 0x01,           /* bcdHID 1.10 (stock value)            */
    0,                    /* bCountryCode                         */
    1,                    /* bNumDescriptors                      */
    0x22,                 /* descriptor type: REPORT              */
    sizeof(h420x_report_vendor), 0,
    7, 0x05,              /* bLength, ENDPOINT                    */
    EPNUM_VENDOR,         /* bEndpointAddress: IN 1               */
    0x03,                 /* bmAttributes: interrupt              */
    64, 0,                /* wMaxPacketSize                       */
    1,                    /* bInterval: 1 ms                      */

    /* Interface 1: digitizer reports (enumerated, driven from M1) */
    9, 0x04,
    ITF_DIGITIZER, 0,
    1,
    0x03, 0x01, 0x02,
    0,
    9, 0x21,
    0x10, 0x01,
    0,
    1,
    0x22,
    sizeof(h420x_report_digitizer), 0,
    7, 0x05,
    EPNUM_DIGITIZER,
    0x03,
    16, 0,
    1,

    /* Interface 2: keyboard reports */
    9, 0x04,
    ITF_KEYBOARD, 0,
    1,
    0x03, 0x00, 0x00,
    0,
    9, 0x21,
    0x10, 0x01,
    0,
    1,
    0x22,
    sizeof(h420x_report_keyboard), 0,
    7, 0x05,
    EPNUM_KEYBOARD,
    0x03,
    64, 0,
    8,
};

/* ------------------------------------------------------------------------- */
/* Report descriptors                                                        */
/* ------------------------------------------------------------------------- */

/* Vendor-defined 11-byte payload (88 bits), report ID 8.
 * Byte layout on the wire:
 *   [0] report ID 8
 *   [1] status (bit 7 = in range)
 *   [2..3] X little-endian
 *   [4..5] Y little-endian
 *   [6..7] pressure little-endian
 *   [8..11] auxiliary
 */
const uint8_t h420x_report_vendor[18] = {
    0x06, 0x00, 0xFF,     /* Usage Page (Vendor-Defined 0xFF00)   */
    0x09, 0x01,           /* Usage (0x01)                         */
    0xA1, 0x01,           /* Collection (Application)             */
    0x85, REPORT_ID_VENDOR, /*   Report ID (8)                    */
    0x75, 0x58,           /*   Report Size (88)                   */
    0x95, 0x01,           /*   Report Count (1)                   */
    0x09, 0x01,           /*   Usage (0x01)                       */
    0x81, 0x02,           /*   Input (Data,Var,Abs)               */
    0xC0,                 /* End Collection                       */
};

/* Digitizer, report ID 10: stylus buttons, in-range, X/Y (0..32767),
 * pressure (0..8191) and two signed 8-bit fields. */
const uint8_t h420x_report_digitizer[93] = {
    0x05, 0x0D,           /* Usage Page (Digitizer)               */
    0x09, 0x02,           /* Usage (Pen)                          */
    0xA1, 0x01,           /* Collection (Application)             */
    0x85, REPORT_ID_DIGITIZER, /* Report ID (10)                  */
    0x09, 0x20,           /*   Usage (Stylus)                     */
    0xA1, 0x01,           /*   Collection (Physical)              */
    0x09, 0x42,           /*     Usage (Tip Switch)               */
    0x09, 0x44,           /*     Usage (Barrel Switch)            */
    0x09, 0x45,           /*     Usage (Eraser)                   */
    0x09, 0x3C,           /*     Usage (Invert)                   */
    0x15, 0x00,           /*     Logical Minimum (0)              */
    0x25, 0x01,           /*     Logical Maximum (1)              */
    0x75, 0x01,           /*     Report Size (1)                  */
    0x95, 0x06,           /*     Report Count (6)                 */
    0x81, 0x02,           /*     Input (Data,Var,Abs)             */
    0x09, 0x32,           /*     Usage (In Range)                 */
    0x75, 0x01,           /*     Report Size (1)                  */
    0x95, 0x01,           /*     Report Count (1)                 */
    0x81, 0x02,           /*     Input (Data,Var,Abs)             */
    0x81, 0x03,           /*     Input (Const,Var,Abs)            */
    0x05, 0x01,           /*     Usage Page (Generic Desktop)     */
    0x09, 0x30,           /*     Usage (X)                        */
    0x09, 0x31,           /*     Usage (Y)                        */
    0x55, 0x0D,           /*     Unit Exponent (-3)               */
    0x65, 0x33,           /*     Unit (English, inch)             */
    0x26, 0xFF, 0x7F,     /*     Logical Maximum (32767)          */
    0x35, 0x00,           /*     Physical Minimum (0)             */
    0x46, 0x00, 0x08,     /*     Physical Maximum (2048)          */
    0x75, 0x10,           /*     Report Size (16)                 */
    0x95, 0x02,           /*     Report Count (2)                 */
    0x81, 0x02,           /*     Input (Data,Var,Abs)             */
    0x05, 0x0D,           /*     Usage Page (Digitizer)           */
    0x09, 0x30,           /*     Usage (Tip Pressure)             */
    0x26, 0xFF, 0x1F,     /*     Logical Maximum (8191)           */
    0x75, 0x10,           /*     Report Size (16)                 */
    0x95, 0x01,           /*     Report Count (1)                 */
    0x81, 0x02,           /*     Input (Data,Var,Abs)             */
    0x09, 0x3D,           /*     Usage (X Tilt)                   */
    0x09, 0x3E,           /*     Usage (Y Tilt)                   */
    0x15, 0x81,           /*     Logical Minimum (-127)           */
    0x25, 0x7F,           /*     Logical Maximum (127)            */
    0x75, 0x08,           /*     Report Size (8)                  */
    0x95, 0x02,           /*     Report Count (2)                 */
    0x81, 0x02,           /*     Input (Data,Var,Abs)             */
    0xC0,                 /*   End Collection                     */
    0xC0,                 /* End Collection                       */
};

/* Boot-style keyboard, report ID 3: 8 modifier bits + 6 key codes. */
const uint8_t h420x_report_keyboard[40] = {
    0x05, 0x01,           /* Usage Page (Generic Desktop)         */
    0x09, 0x06,           /* Usage (Keyboard)                     */
    0xA1, 0x01,           /* Collection (Application)             */
    0x85, REPORT_ID_KEYBOARD, /* Report ID (3)                    */
    0x05, 0x07,           /*   Usage Page (Keyboard)              */
    0x19, 0xE0,           /*   Usage Minimum (Left Control)       */
    0x29, 0xE7,           /*   Usage Maximum (Right GUI)          */
    0x15, 0x00,           /*   Logical Minimum (0)                */
    0x25, 0x01,           /*   Logical Maximum (1)                */
    0x75, 0x01,           /*   Report Size (1)                    */
    0x95, 0x08,           /*   Report Count (8)                   */
    0x81, 0x02,           /*   Input (Data,Var,Abs)               */
    0x05, 0x07,           /*   Usage Page (Keyboard)              */
    0x19, 0x00,           /*   Usage Minimum (0)                  */
    0x29, 0xFF,           /*   Usage Maximum (255)                */
    0x26, 0xFF, 0x00,     /*   Logical Maximum (255)              */
    0x75, 0x08,           /*   Report Size (8)                    */
    0x95, 0x06,           /*   Report Count (6)                   */
    0x81, 0x00,           /*   Input (Data,Array,Abs)             */
    0xC0,                 /* End Collection                       */
};

/* ------------------------------------------------------------------------- */
/* TinyUSB callbacks                                                         */
/* ------------------------------------------------------------------------- */
uint8_t const *tud_descriptor_device_cb(void) {
  return h420x_device_descriptor;
}

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
  (void)index;
  return h420x_config_descriptor;
}

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
  switch (instance) {
    case 0: return h420x_report_vendor;
    case 1: return h420x_report_digitizer;
    case 2: return h420x_report_keyboard;
    default: return NULL;
  }
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t reqlen) {
  (void)instance;
  (void)report_id;
  (void)report_type;
  (void)buffer;
  (void)reqlen;
  return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize) {
  (void)instance;
  (void)report_id;
  (void)report_type;
  (void)buffer;
  (void)bufsize;
}

/* ------------------------------------------------------------------------- */
/* String descriptors                                                        */
/* ------------------------------------------------------------------------- */
static const char *const string_table[] = {
    "\x09\x04",            /* 0: LANGID 0x0409 (English, US)      */
#if STOCK_IDENTITY
    "HUION",               /* 1: manufacturer (stock H420X)       */
    "Huion Tablet_H420X",  /* 2: product (stock H420X)            */
    H420X_STR(SERIAL_NUMBER), /* 3: serial (stock H420X, or test) */
#else
    "H420X Cleanroom",     /* 1: manufacturer                     */
    "H420X",               /* 2: product                          */
    "H420X-CR-0001",       /* 3: serial                           */
#endif
};

/* OTD's upstream H420X configuration reads these two non-standard string
 * indices. 200 is only required to exist; 201 must match the firmware build
 * ID pattern used by the stock T210/T223 images. */
#if STOCK_IDENTITY
const char h420x_string_200[] = "20210226";
const char h420x_string_201[] = "HUION_T223_220707";
#else
const char h420x_string_200[] = "20261026";
const char h420x_string_201[] = "HUION_T223_261026";
#endif

static uint16_t string_desc_buffer[32];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void)langid;

  unsigned count = 0;
  const char *s = NULL;
  if (index == 0U) {
    string_desc_buffer[1] = 0x0409U;
    count = 1;
  } else {
    if (index == 200U) {
      s = h420x_string_200;
    } else if (index == 201U) {
      s = h420x_string_201;
    } else if (index < (sizeof(string_table) / sizeof(string_table[0]))) {
      s = string_table[index];
    } else {
      return NULL;
    }

    while (s[count] != '\0' && count < 31U) {
      string_desc_buffer[1U + count] = (uint16_t)(uint8_t)s[count];
      count++;
    }
  }

  string_desc_buffer[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2U * count + 2U));
  return string_desc_buffer;
}
