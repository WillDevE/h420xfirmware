/*
 * Optional adaptive output smoothing (build switch -Smooth).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef H420X_SMOOTH_H
#define H420X_SMOOTH_H

#include <stdint.h>

/* Filter the position in place. The first call, and the first call after a
 * pause of more than SMOOTH_RESET_CYC cycles (the pen was lost), seeds the
 * filter with the raw position and leaves it unchanged. */
void smooth_apply(int32_t *x, int32_t *y);

#endif /* H420X_SMOOTH_H */
