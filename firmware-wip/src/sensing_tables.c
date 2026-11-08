/*
 * H420X sensing tables.
 *
 * The selector/mask values are hardware wiring data for the X/Y multiplexers.
 * They are not application code. Verify the coil order and mux behavior on
 * the actual board before trusting the first tracking result.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "sensing.h"

const mux_entry_t x_mux[19] = {
    { 0xFFF70001U, 0x80U },  /* 0 */
    { 0xFFF70002U, 0xC0U },  /* 1 */
    { 0xFFF70003U, 0xE0U },  /* 2 */
    { 0xFFF70004U, 0xA0U },  /* 3 */
    { 0xFFF70005U, 0x40U },  /* 4 */
    { 0xFFF70006U, 0x20U },  /* 5 */
    { 0xFFF70007U, 0x00U },  /* 6 */
    { 0xFFF70008U, 0x60U },  /* 7 */
    { 0xFFEF0009U, 0x80U },  /* 8 */
    { 0xFFEF000AU, 0xC0U },  /* 9 */
    { 0xFFEF000BU, 0xE0U },  /* 10 */
    { 0xFFEF000CU, 0xA0U },  /* 11 */
    { 0xFFEF000DU, 0x40U },  /* 12 */
    { 0xFFEF000EU, 0x20U },  /* 13 */
    { 0xFFEF000FU, 0x00U },  /* 14 */
    { 0xFFEF0010U, 0x60U },  /* 15 */
    { 0xFFDF0011U, 0xC0U },  /* 16 */
    { 0xFFDF0012U, 0xE0U },  /* 17 */
    { 0xFFDF0013U, 0xA0U },  /* 18 */
};

const mux_entry_t y_mux[13] = {
    { 0xFFBF0001U, 0x60U },  /* 0 */
    { 0xFFBF0002U, 0x00U },  /* 1 */
    { 0xFFBF0003U, 0x20U },  /* 2 */
    { 0xFFBF0004U, 0x40U },  /* 3 */
    { 0xFFBF0005U, 0xA0U },  /* 4 */
    { 0xFFBF0006U, 0xE0U },  /* 5 */
    { 0xFFBF0007U, 0xC0U },  /* 6 */
    { 0xFFBF0008U, 0x80U },  /* 7 */
    { 0xFFDF0009U, 0x40U },  /* 8 */
    { 0xFFDF000AU, 0x00U },  /* 9 */
    { 0xFFDF000BU, 0x20U },  /* 10 */
    { 0xFFDF000CU, 0x60U },  /* 11 */
    { 0xFFDF000DU, 0x80U },  /* 12 */
};

const uint32_t resonance_hz[13] = {
    0U, /* bin 0 */
    452830U, /* bin 1 */
    461538U, /* bin 2 */
    470588U, /* bin 3 */
    480000U, /* bin 4 */
    489796U, /* bin 5 */
    500000U, /* bin 6 */
    503497U, /* bin 7 */
    510638U, /* bin 8 */
    517986U, /* bin 9 */
    525547U, /* bin 10 */
    533333U, /* bin 11 */
    545455U, /* bin 12 */
};

/* Excitation period per bin in CPU cycles at 72 MHz: 72 MHz / resonance_hz[bin],
 * rounded. The burst is scheduled against DWT deadlines, so the mean period is
 * exact regardless of instruction timing. */
const uint16_t excite_period_cyc[13] = {
    0U, 159U, 156U, 153U, 150U, 147U, 144U, 143U, 141U, 139U, 137U, 135U, 132U,
};

const uint16_t axis_pitch[2] = { 1267U, 1248U };

_Static_assert(sizeof(x_mux) / sizeof(x_mux[0]) == 19U, "X mux table size");
_Static_assert(sizeof(y_mux) / sizeof(y_mux[0]) == 13U, "Y mux table size");
_Static_assert(sizeof(resonance_hz) / sizeof(resonance_hz[0]) == 13U,
               "frequency bin table size");
_Static_assert(sizeof(excite_period_cyc) / sizeof(excite_period_cyc[0]) == 13U,
               "excitation period table size");
