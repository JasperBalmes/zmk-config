/*
 * Portrait renderer for the dancing-dogs OLED screen. No ZMK/LVGL dependencies, so it
 * can be compiled and tested on a PC.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define DOGS_PW 32  /* portrait width  (what you see on the vertically mounted OLED) */
#define DOGS_PH 128 /* portrait height */
#define DOGS_PBYTES ((DOGS_PW / 8) * DOGS_PH)

#define DOGS_LW 128 /* landscape width  (what the SSD1306 driver wants) */
#define DOGS_LH 32
#define DOGS_LBYTES ((DOGS_LW / 8) * DOGS_LH)

struct dogs_state {
    uint8_t frame;     /* dance frame */
    bool bt_visible;   /* false during the "off" phase of a blinking Bluetooth icon */
    int8_t profile;    /* active Bluetooth profile 0..4 (left half); -1 = not shown */
    int8_t battery;    /* 0..100, or -1 if unknown */
    bool key_a;        /* left: GUI    right: ENTER */
    bool key_b;        /* left: SPACE  right: ALT */
    bool layer;        /* left: LWR    right: RSE */
    uint16_t kpm;      /* keys per minute (shown on the right screen only) */
};

/* Draw the whole screen into a 32x128 1-bit buffer (4 bytes per row, MSB-first, 1 = lit). */
void dogs_render(uint8_t *pbuf, const struct dogs_state *st, bool right_half);

/* Turn the portrait buffer into the 128x32 landscape buffer (16 bytes per row). */
void dogs_rotate(const uint8_t *pbuf, uint8_t *lbuf, bool ccw);
