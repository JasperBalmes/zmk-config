/*
 * Portrait renderer for the dancing-dogs OLED screen.
 *
 * Layout (32 x 128 portrait, y from the top):
 *    1.. 9  Bluetooth icon + battery %
 *   12..18  left: Bluetooth profile 1-5   right: keys per minute
 *   20      dotted divider
 *   22..85  animation (a spinning globe on each screen)
 *   87      dotted divider
 *   90..103 two key lights (GUI / SPACE   or   ENTER / ALT)
 *  107..124 layer box (LWR   or   RSE)
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include "dogs_art.h"
#include "dogs_render.h"

#define PB (DOGS_PW / 8)

#define Y_ICON 1
#define Y_TEXT 3
#define Y_STRIP 12
#define Y_DIV1 20
#define Y_DOG 22
#define Y_DIV2 87
#define Y_KEYS 90
#define KEY_W 14
#define KEY_H 14
#define Y_LAYER 107
#define LAYER_W 30
#define LAYER_H 18

enum { MODE_SET, MODE_CLEAR };

static void px(uint8_t *b, int x, int y, bool on) {
    if (x < 0 || x >= DOGS_PW || y < 0 || y >= DOGS_PH) {
        return;
    }
    uint8_t m = 0x80 >> (x & 7);
    if (on) {
        b[y * PB + (x >> 3)] |= m;
    } else {
        b[y * PB + (x >> 3)] &= ~m;
    }
}

static void fill_rect(uint8_t *b, int x, int y, int w, int h, bool on) {
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            px(b, x + i, y + j, on);
        }
    }
}

/* box with the four corner pixels removed, either filled or just a 1px outline */
static void round_box(uint8_t *b, int x, int y, int w, int h, bool filled) {
    if (filled) {
        fill_rect(b, x, y, w, h, true);
    } else {
        for (int i = 0; i < w; i++) {
            px(b, x + i, y, true);
            px(b, x + i, y + h - 1, true);
        }
        for (int j = 0; j < h; j++) {
            px(b, x, y + j, true);
            px(b, x + w - 1, y + j, true);
        }
    }
    px(b, x, y, false);
    px(b, x + w - 1, y, false);
    px(b, x, y + h - 1, false);
    px(b, x + w - 1, y + h - 1, false);
}

static void blit(uint8_t *b, const struct dogs_bitmap *bm, int x, int y, int mode) {
    int nb = (bm->w + 7) / 8;
    for (int j = 0; j < bm->h; j++) {
        for (int i = 0; i < bm->w; i++) {
            if (bm->rows[j * nb + (i >> 3)] & (0x80 >> (i & 7))) {
                px(b, x + i, y + j, mode == MODE_SET);
            }
        }
    }
}

static void glyph3x5(uint8_t *b, int idx, int x, int y, int mode) {
    for (int j = 0; j < 5; j++) {
        for (int i = 0; i < 3; i++) {
            if (dogs_font3x5[idx][j] & (4 >> i)) {
                px(b, x + i, y + j, mode == MODE_SET);
            }
        }
    }
}

static void dotted_line(uint8_t *b, int y) {
    for (int x = 0; x < DOGS_PW; x += 2) {
        px(b, x, y, true);
    }
}

/* right-aligned battery text such as "100%", "87%", "5%", or "--" */
static void battery_text(uint8_t *b, int right_x, int y, int pct) {
    int idx[4];
    int n = 0;
    if (pct < 0) {
        idx[n++] = 11;
        idx[n++] = 11;
    } else {
        if (pct > 100) {
            pct = 100;
        }
        if (pct >= 100) {
            idx[n++] = 1;
            idx[n++] = 0;
            idx[n++] = 0;
        } else if (pct >= 10) {
            idx[n++] = pct / 10;
            idx[n++] = pct % 10;
        } else {
            idx[n++] = pct;
        }
        idx[n++] = 10; /* % */
    }
    int x = right_x - (n * 4 - 1) + 1;
    for (int i = 0; i < n; i++) {
        glyph3x5(b, idx[i], x + i * 4, y, MODE_SET);
    }
}

static void key_light(uint8_t *b, int x, const struct dogs_bitmap *icon, bool lit) {
    round_box(b, x, Y_KEYS, KEY_W, KEY_H, lit);
    int gx = x + (KEY_W - icon->w) / 2;
    int gy = Y_KEYS + (KEY_H - icon->h) / 2;
    blit(b, icon, gx, gy, lit ? MODE_CLEAR : MODE_SET);
}

void dogs_render(uint8_t *pbuf, const struct dogs_state *st, bool right_half) {
    memset(pbuf, 0, DOGS_PBYTES);

    /* ---- top: Bluetooth icon + battery % ---- */
    if (st->bt_visible) {
        blit(pbuf, &dogs_icon_bt, 1, Y_ICON, MODE_SET);
    }
    battery_text(pbuf, 30, Y_TEXT, st->battery);

    /* ---- second row ---- */
    if (!right_half) {
        /* profile numbers 1..5, the active one in a lit box */
        for (int i = 0; i < 5; i++) {
            int x = 1 + i * 6;
            bool active = (st->profile == i);
            if (active) {
                fill_rect(pbuf, x, Y_STRIP, 5, 7, true);
            }
            glyph3x5(pbuf, i + 1, x + 1, Y_STRIP + 1, active ? MODE_CLEAR : MODE_SET);
        }
    } else {
        /* keys per minute: "KPM" on the left, the number right-aligned */
        glyph3x5(pbuf, 12, 1, Y_STRIP + 1, MODE_SET);
        glyph3x5(pbuf, 13, 5, Y_STRIP + 1, MODE_SET);
        glyph3x5(pbuf, 14, 9, Y_STRIP + 1, MODE_SET);
        int k = st->kpm > 999 ? 999 : st->kpm;
        int digits[3];
        int n = 0;
        if (k >= 100) {
            digits[n++] = k / 100;
        }
        if (k >= 10) {
            digits[n++] = (k / 10) % 10;
        }
        digits[n++] = k % 10;
        int x0 = 30 - (n * 4 - 1) + 1;
        for (int i = 0; i < n; i++) {
            glyph3x5(pbuf, digits[i], x0 + i * 4, Y_STRIP + 1, MODE_SET);
        }
    }

    dotted_line(pbuf, Y_DIV1);

    /* ---- animation ---- */
    const uint8_t *dog = right_half ? dogs_dog_right[st->frame % DOGS_DOG_FRAMES]
                                    : dogs_dog_left[st->frame % DOGS_DOG_FRAMES];
    memcpy(pbuf + Y_DOG * PB, dog, DOGS_DOG_BYTES);

    dotted_line(pbuf, Y_DIV2);

    /* ---- key lights ---- */
    key_light(pbuf, 1, right_half ? &dogs_icon_enter : &dogs_icon_cmd, st->key_a);
    key_light(pbuf, 17, right_half ? &dogs_icon_opt : &dogs_icon_space, st->key_b);

    /* ---- layer indicator ---- */
    if (st->layer) {
        round_box(pbuf, 1, Y_LAYER, LAYER_W, LAYER_H, true);
        static const uint8_t lwr[3] = {DOGS_BIG_L, DOGS_BIG_W, DOGS_BIG_R};
        static const uint8_t rse[3] = {DOGS_BIG_R, DOGS_BIG_S, DOGS_BIG_E};
        const uint8_t *word = right_half ? rse : lwr;
        for (int i = 0; i < 3; i++) {
            blit(pbuf, &dogs_big[word[i]], 3 + i * 9, Y_LAYER + (LAYER_H - 11) / 2, MODE_CLEAR);
        }
    }
}

void dogs_rotate(const uint8_t *pbuf, uint8_t *lbuf, bool ccw) {
    const int lb = DOGS_LW / 8;
    memset(lbuf, 0, DOGS_LBYTES);
    for (int y = 0; y < DOGS_LH; y++) {
        for (int x = 0; x < DOGS_LW; x++) {
            int sx = ccw ? (DOGS_LH - 1 - y) : y;
            int sy = ccw ? x : (DOGS_PH - 1 - x);
            if (pbuf[sy * PB + (sx >> 3)] & (0x80 >> (sx & 7))) {
                lbuf[y * lb + (x >> 3)] |= 0x80 >> (x & 7);
            }
        }
    }
}
