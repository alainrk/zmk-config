/*
 * SPDX-License-Identifier: MIT
 *
 * Central half: tell the peripheral half which Bluetooth profile is active,
 * so its display can show the number.
 *
 * ZMK v0.3 does not share the active profile with the peripheral. The only
 * channel from central to peripheral is "run this behavior", so we run the
 * btprof behavior there with the profile index as its parameter.
 *
 * The central has no event for "peripheral connected", so besides profile
 * changes we also send again when the keyboard wakes from idle and when a key
 * on the peripheral is pressed. That covers the peripheral restarting or
 * reconnecting.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/activity.h>
#include <zmk/behavior.h>
#include <zmk/ble.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/split/central.h>

#define BT_PROFILE_BEHAVIOR DEVICE_DT_NAME(DT_NODELABEL(bt_prof_disp))

/* Shortest time between two sends caused by key presses on the peripheral. */
#define RESEND_INTERVAL_MS 2000

static int64_t last_sent;

static void send_work_cb(struct k_work *work) {
    struct zmk_behavior_binding binding = {
        .behavior_dev = BT_PROFILE_BEHAVIOR,
        .param1 = zmk_ble_active_profile_index(),
    };
    struct zmk_behavior_binding_event event = {
        .position = 0,
        .timestamp = k_uptime_get(),
    };

    for (uint8_t i = 0; i < ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT; i++) {
        zmk_split_central_invoke_behavior(i, &binding, event, true);
    }

    last_sent = k_uptime_get();
}

static K_WORK_DELAYABLE_DEFINE(send_work, send_work_cb);

static int bt_profile_sync_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *pos = as_zmk_position_state_changed(eh);
    if (pos != NULL) {
        bool from_peripheral = pos->source != ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL;
        if (!from_peripheral || !pos->state ||
            k_uptime_get() - last_sent < RESEND_INTERVAL_MS) {
            return ZMK_EV_EVENT_BUBBLE;
        }
    }

    const struct zmk_activity_state_changed *act = as_zmk_activity_state_changed(eh);
    if (act != NULL && act->state != ZMK_ACTIVITY_ACTIVE) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    k_work_schedule(&send_work, K_MSEC(50));

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(bt_profile_sync, bt_profile_sync_listener);
ZMK_SUBSCRIPTION(bt_profile_sync, zmk_ble_active_profile_changed);
ZMK_SUBSCRIPTION(bt_profile_sync, zmk_activity_state_changed);
ZMK_SUBSCRIPTION(bt_profile_sync, zmk_position_state_changed);
