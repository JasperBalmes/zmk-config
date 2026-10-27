/*
 * Animated status screen (cheering figure / spinning globe) for the Eyelash Corne OLEDs.
 *
 *   top      Bluetooth icon + battery %, then BT profile 1-5 (left) / keys per minute (right)
 *   middle   left: a cheering figure with lines rising past it
 *            right: a slowly spinning globe
 *   bottom   key lights: GUI + SPACE (left)  ENTER + ALT (right)
 *   very bot LWR (left) / RSE (right) while that layer key is held
 *
 * All drawing happens in dogs_render.c into a portrait 32x128 buffer. This file only
 * feeds it live state and copies the rotated result into an LVGL canvas.
 *
 * Key lights and layer boxes follow the *physical* thumb-key positions, because the right
 * half (a split peripheral) is never told the active layer by the left half.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <lvgl.h>

#include <zmk/battery.h>
#include <zmk/ble.h>
#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE) && !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#include <zmk/split/bluetooth/peripheral.h>
#endif

#include "dogs_art.h"
#include "dogs_render.h"

/* The left half is the BLE central; the right half is the peripheral. */
#define IS_RIGHT_HALF (IS_ENABLED(CONFIG_ZMK_SPLIT) && !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL))

#if IS_RIGHT_HALF
#define POS_KEY_A CONFIG_DOGS_OLED_POS_ENTER
#define POS_KEY_B CONFIG_DOGS_OLED_POS_ALT
#define POS_LAYER CONFIG_DOGS_OLED_POS_RSE
#else
#define POS_KEY_A CONFIG_DOGS_OLED_POS_GUI
#define POS_KEY_B CONFIG_DOGS_OLED_POS_SPACE
#define POS_LAYER CONFIG_DOGS_OLED_POS_LWR
#endif

#define TICK_MS 40
#define FRAME_TICKS (CONFIG_DOGS_OLED_FRAME_MS / TICK_MS > 0 ? CONFIG_DOGS_OLED_FRAME_MS / TICK_MS : 1)
#define SLOW_TICKS (480 / TICK_MS) /* battery / Bluetooth poll + icon blink (12 ticks) */
#define KPM_TICKS (1000 / TICK_MS)  /* one second */
#define LINGER_TICKS 3             /* keep a key lit for at least this many ticks (~120 ms) */

/* bit 0 = key A, bit 1 = key B, bit 2 = layer key */
static atomic_t held;  /* currently pressed */
static atomic_t tapped; /* pressed at some point since the last tick (catches very short taps) */
static atomic_t press_count; /* every key press on this half since the last second */

static int dogs_position_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (ev->state) {
        atomic_inc(&press_count);
    }

    int bit = -1;
    if (ev->position == POS_KEY_A) {
        bit = 0;
    } else if (ev->position == POS_KEY_B) {
        bit = 1;
    } else if (ev->position == POS_LAYER) {
        bit = 2;
    }

    if (bit >= 0) {
        if (ev->state) {
            atomic_set_bit(&held, bit);
            atomic_set_bit(&tapped, bit);
        } else {
            atomic_clear_bit(&held, bit);
        }
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(dogs_oled_position, dogs_position_listener);
ZMK_SUBSCRIPTION(dogs_oled_position, zmk_position_state_changed);

/* ---- LVGL side ---- */

static uint8_t canvas_buf[LV_CANVAS_BUF_SIZE_INDEXED_1BIT(DOGS_LW, DOGS_LH)] __aligned(4);
static uint8_t portrait[DOGS_PBYTES];
static uint8_t landscape[DOGS_LBYTES];

static lv_obj_t *canvas;
static struct dogs_state state = {.frame = 0, .bt_visible = true, .profile = -1, .battery = -1};

static void redraw(void) {
    dogs_render(portrait, &state, IS_RIGHT_HALF);
    dogs_rotate(portrait, landscape, IS_ENABLED(CONFIG_DOGS_OLED_ORIENTATION_CCW));
    /* the first 8 bytes of an indexed-1bit canvas buffer are the palette */
    memcpy(canvas_buf + 8, landscape, DOGS_LBYTES);
    lv_obj_invalidate(canvas);
}

static bool state_equal(const struct dogs_state *a, const struct dogs_state *b) {
    return a->frame == b->frame && a->bt_visible == b->bt_visible && a->profile == b->profile &&
           a->battery == b->battery && a->key_a == b->key_a && a->key_b == b->key_b &&
           a->layer == b->layer && a->kpm == b->kpm;
}

/* Keys per minute = presses in the last KPM_WINDOW_S seconds, scaled up to a minute. */
static void update_kpm(void) {
    static uint16_t window[CONFIG_DOGS_OLED_KPM_WINDOW_S];
    static int idx;
    uint32_t sum = 0;

    window[idx] = (uint16_t)atomic_set(&press_count, 0);
    idx = (idx + 1) % CONFIG_DOGS_OLED_KPM_WINDOW_S;
    for (int i = 0; i < CONFIG_DOGS_OLED_KPM_WINDOW_S; i++) {
        sum += window[i];
    }
    uint32_t kpm = sum * 60 / CONFIG_DOGS_OLED_KPM_WINDOW_S;
    state.kpm = kpm > 999 ? 999 : (uint16_t)kpm;
}

static void poll_slow_state(uint32_t ticks) {
    bool blink_on = ((ticks / SLOW_TICKS) & 1) == 0;
    bool connected;

    if (IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)) {
        state.battery = (int8_t)zmk_battery_state_of_charge();
    }

#if IS_RIGHT_HALF
#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE)
    connected = zmk_split_bt_peripheral_is_connected();
#else
    connected = true;
#endif
    state.profile = -1;
#else
    connected = zmk_ble_active_profile_is_connected();
    state.profile = (int8_t)zmk_ble_active_profile_index();
#endif
    /* steady icon when linked, blinking while searching / pairing */
    state.bt_visible = connected || blink_on;
}

static void tick(lv_timer_t *timer) {
    ARG_UNUSED(timer);
    static uint32_t ticks;
    static uint8_t linger[3];
    struct dogs_state before = state;

    ticks++;

    if (ticks % FRAME_TICKS == 0) {
        state.frame = (state.frame + 1) % DOGS_DOG_FRAMES;
    }

    atomic_val_t now = atomic_get(&held) | atomic_set(&tapped, 0);
    for (int i = 0; i < 3; i++) {
        if (now & BIT(i)) {
            linger[i] = LINGER_TICKS;
        } else if (linger[i] > 0) {
            linger[i]--;
        }
    }
    state.key_a = linger[0] > 0;
    state.key_b = linger[1] > 0;
    /* the layer box follows the key being held, with no linger */
    state.layer = (atomic_get(&held) & BIT(2)) != 0;

    if (ticks % SLOW_TICKS == 1) {
        poll_slow_state(ticks);
    }
    if (ticks % KPM_TICKS == 0) {
        update_kpm();
    }

    if (!state_equal(&before, &state)) {
        redraw();
    }
}

lv_obj_t *zmk_display_status_screen() {
    lv_obj_t *screen = lv_obj_create(NULL);

#if IS_ENABLED(CONFIG_DOGS_OLED_INVERT)
    lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);
#else
    lv_obj_set_style_bg_color(screen, lv_color_white(), LV_PART_MAIN);
#endif
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    canvas = lv_canvas_create(screen);
    lv_canvas_set_buffer(canvas, canvas_buf, DOGS_LW, DOGS_LH, LV_IMG_CF_INDEXED_1BIT);
    /* bit 1 = lit pixel. On the Eyelash SSD1306 (inversion-on) that is the "black" LVGL colour. */
#if IS_ENABLED(CONFIG_DOGS_OLED_INVERT)
    lv_canvas_set_palette(canvas, 0, lv_color_black());
    lv_canvas_set_palette(canvas, 1, lv_color_white());
#else
    lv_canvas_set_palette(canvas, 0, lv_color_white());
    lv_canvas_set_palette(canvas, 1, lv_color_black());
#endif
    lv_obj_set_pos(canvas, 0, 0);

    poll_slow_state(0);
    redraw();
    lv_timer_create(tick, TICK_MS, NULL);

    return screen;
}
