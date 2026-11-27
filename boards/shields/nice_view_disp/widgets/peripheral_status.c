/*
 *
 * Copyright (c) 2023 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 */

#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/battery.h>
#include <zmk/display.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/split/bluetooth/peripheral.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/usb.h>
#include <zmk/ble.h>
#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
#include <zmk/events/hid_indicators_changed.h>
#include <zmk/hid_indicators.h>
#endif

#include "peripheral_status.h"

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

// Active Bluetooth profile, as told by the central half. -1 until the first
// update arrives (the peripheral cannot know it on its own).
static atomic_t bt_profile = ATOMIC_INIT(-1);

struct peripheral_status_state {
    bool connected;
};

static void draw_top(lv_obj_t *widget, lv_color_t cbuf[], const struct status_state *state) {
    lv_obj_t *canvas = lv_obj_get_child(widget, 0);

    lv_draw_label_dsc_t label_dsc;
    init_label_dsc(&label_dsc, LVGL_FOREGROUND, &lv_font_montserrat_16, LV_TEXT_ALIGN_RIGHT);
    lv_draw_rect_dsc_t rect_black_dsc;
    init_rect_dsc(&rect_black_dsc, LVGL_BACKGROUND);

    // Fill background
    lv_canvas_draw_rect(canvas, 0, 0, CANVAS_SIZE, CANVAS_SIZE, &rect_black_dsc);

    // Draw battery
    draw_battery(canvas, state);

    // Draw output status
    lv_canvas_draw_text(canvas, 0, 0, CANVAS_SIZE, &label_dsc,
                        state->connected ? LV_SYMBOL_WIFI : LV_SYMBOL_CLOSE);

    // Draw CAPS indicator when Caps Lock is on.
    if (state->caps_on) {
        lv_draw_label_dsc_t caps_dsc;
        init_label_dsc(&caps_dsc, LVGL_FOREGROUND, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
        lv_canvas_draw_text(canvas, 0, 28, CANVAS_SIZE, &caps_dsc, "CAPS");
    }

    // Rotate canvas
    rotate_canvas(canvas, cbuf);
}

// Draw "BT" and the profile number in big digits. The number starts at 1, like
// on the central display.
static void draw_bt_profile(lv_obj_t *widget, lv_color_t cbuf[]) {
    lv_obj_t *canvas = lv_obj_get_child(widget, 1);

    lv_draw_rect_dsc_t rect_black_dsc;
    init_rect_dsc(&rect_black_dsc, LVGL_BACKGROUND);
    lv_draw_label_dsc_t title_dsc;
    init_label_dsc(&title_dsc, LVGL_FOREGROUND, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
    lv_draw_label_dsc_t number_dsc;
    init_label_dsc(&number_dsc, LVGL_FOREGROUND, &lv_font_montserrat_48, LV_TEXT_ALIGN_CENTER);

    lv_canvas_draw_rect(canvas, 0, 0, CANVAS_SIZE, CANVAS_SIZE, &rect_black_dsc);

    char text[4] = "-";
    int index = atomic_get(&bt_profile);
    if (index >= 0) {
        snprintf(text, sizeof(text), "%d", (index + 1) % 100);
    }

    lv_canvas_draw_text(canvas, 0, 0, CANVAS_SIZE, &title_dsc, "BT");
    lv_canvas_draw_text(canvas, 0, 16, CANVAS_SIZE, &number_dsc, text);

    rotate_canvas(canvas, cbuf);
}

static void bt_profile_work_cb(struct k_work *work) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        draw_bt_profile(widget->obj, widget->cbuf2);
    }
}

static K_WORK_DEFINE(bt_profile_work, bt_profile_work_cb);

void zmk_widget_status_set_bt_profile(uint8_t index) {
    if (atomic_set(&bt_profile, index) == index) {
        return;
    }

    // Drawing must happen on the display work queue. If the display is not up
    // yet, zmk_widget_status_init() draws the stored value later.
    if (zmk_display_is_initialized()) {
        k_work_submit_to_queue(zmk_display_work_q(), &bt_profile_work);
    }
}

static void set_battery_status(struct zmk_widget_status *widget,
                               struct battery_status_state state) {
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    widget->state.charging = state.usb_present;
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */

    widget->state.battery = state.level;

    draw_top(widget->obj, widget->cbuf, &widget->state);
}

static void battery_status_update_cb(struct battery_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_battery_status(widget, state); }
}

static struct battery_status_state battery_status_get_state(const zmk_event_t *eh) {
    return (struct battery_status_state){
        .level = zmk_battery_state_of_charge(),
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
        .usb_present = zmk_usb_is_powered(),
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_battery_status, struct battery_status_state,
                            battery_status_update_cb, battery_status_get_state)

ZMK_SUBSCRIPTION(widget_battery_status, zmk_battery_state_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(widget_battery_status, zmk_usb_conn_state_changed);
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */

static struct peripheral_status_state get_state(const zmk_event_t *_eh) {
    return (struct peripheral_status_state){.connected = zmk_split_bt_peripheral_is_connected()};
}

static void set_connection_status(struct zmk_widget_status *widget,
                                  struct peripheral_status_state state) {
    widget->state.connected = state.connected;

    draw_top(widget->obj, widget->cbuf, &widget->state);
}

static void output_status_update_cb(struct peripheral_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_connection_status(widget, state); }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_peripheral_status, struct peripheral_status_state,
                            output_status_update_cb, get_state)
ZMK_SUBSCRIPTION(widget_peripheral_status, zmk_split_peripheral_status_changed);

#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
struct caps_status_state {
    bool caps_on;
};

static void set_caps_status(struct zmk_widget_status *widget, struct caps_status_state state) {
    widget->state.caps_on = state.caps_on;

    draw_top(widget->obj, widget->cbuf, &widget->state);
}

static void caps_status_update_cb(struct caps_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_caps_status(widget, state); }
}

static struct caps_status_state caps_status_get_state(const zmk_event_t *eh) {
    // zmk_hid_indicators_get_current_profile() is not linked on the peripheral,
    // so rely solely on the forwarded event (defaults off until the first one).
    const struct zmk_hid_indicators_changed *ev = as_zmk_hid_indicators_changed(eh);
    zmk_hid_indicators_t indicators = (ev != NULL) ? ev->indicators : 0;
    return (struct caps_status_state){.caps_on = (indicators & BIT(1)) != 0};
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_caps_status, struct caps_status_state, caps_status_update_cb,
                            caps_status_get_state)
ZMK_SUBSCRIPTION(widget_caps_status, zmk_hid_indicators_changed);
#endif

#ifdef CONFIG_NICE_VIEW_DISP_ROTATE_180 // sets positions for default and flipped canvases
int top_pos = 0;
int bt_pos = 68;
#else
int top_pos = 92;
int bt_pos = 24;
#endif

int zmk_widget_status_init(struct zmk_widget_status *widget, lv_obj_t *parent) {
    widget->obj = lv_obj_create(parent);
    lv_obj_set_size(widget->obj, 160, 68);
    lv_obj_t *top = lv_canvas_create(widget->obj);
    lv_obj_align(top, LV_ALIGN_TOP_LEFT, top_pos, 0);
    lv_canvas_set_buffer(top, widget->cbuf, CANVAS_SIZE, CANVAS_SIZE, LV_IMG_CF_TRUE_COLOR);

    // Child 1: the Bluetooth profile number (draw_bt_profile relies on the order).
    lv_obj_t *bt = lv_canvas_create(widget->obj);
    lv_obj_align(bt, LV_ALIGN_TOP_LEFT, bt_pos, 0);
    lv_canvas_set_buffer(bt, widget->cbuf2, CANVAS_SIZE, CANVAS_SIZE, LV_IMG_CF_TRUE_COLOR);
    draw_bt_profile(widget->obj, widget->cbuf2);

    sys_slist_append(&widgets, &widget->node);
    widget_battery_status_init();
    widget_peripheral_status_init();
#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
    widget_caps_status_init();
#endif

    return 0;
}

lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget) { return widget->obj; }
