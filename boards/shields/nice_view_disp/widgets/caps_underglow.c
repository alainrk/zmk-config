/*
 * Caps Lock -> solid red RGB underglow indicator.
 *
 * Listens for the host's HID "Caps Lock" LED state and turns the underglow
 * solid red while Caps Lock is on, restoring the previous state when it
 * turns off.
 *
 * Notes / limitations (ZMK v0.3):
 *   - ZMK exposes no getter for the live underglow colour/effect, so the
 *     colour restored on Caps-off is the fixed `normal_color` below, NOT an
 *     arbitrary colour you may have set via the RAISE-layer RGB keys.
 *     Only the on/off state is restored exactly.
 *   - Keep the underglow on the Solid effect (the board default) for a steady
 *     red. Animated effects (breathe/spectrum/swirl) keep animating and will
 *     just be tinted red rather than showing a solid block of red.
 *   - Compiled on BOTH halves. ZMK v0.3 has no split underglow state sync, so
 *     each half must drive its own strip: the central reacts to the host's
 *     HID indicator report, the peripheral reacts to the same event forwarded
 *     over the split (needs CONFIG_ZMK_SPLIT_PERIPHERAL_HID_INDICATORS=y).
 *     Driving only the central would light just one half AND desync the halves,
 *     breaking the global &rgb_ug raise-layer controls on the peripheral.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/event_manager.h>
#include <zmk/events/hid_indicators_changed.h>
#include <zmk/rgb_underglow.h>

#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW)

/* HID keyboard LED output report bit order: NumLock=0, CapsLock=1, ScrollLock=2 */
#define CAPS_LOCK_LED_BIT BIT(1)

/* Solid red shown while Caps Lock is on. Ranges: h 0-360, s 0-100, b 0-100. */
static const struct zmk_led_hsb caps_color = {.h = 0, .s = 100, .b = 100};

/* Colour restored when Caps Lock turns off (if underglow was on).
 * Defaults match the board defconfig: HUE_START=200, SAT_START=98. */
static const struct zmk_led_hsb normal_color = {.h = 200, .s = 98, .b = 100};

static bool caps_active;
static bool restore_on; /* underglow on/off state captured when caps turned on */

static int caps_underglow_listener(const zmk_event_t *eh) {
    const struct zmk_hid_indicators_changed *ev = as_zmk_hid_indicators_changed(eh);
    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    bool caps_now = (ev->indicators & CAPS_LOCK_LED_BIT) != 0;
    if (caps_now == caps_active) {
        return ZMK_EV_EVENT_BUBBLE;
    }
    caps_active = caps_now;

    if (caps_now) {
        /* Remember whether the underglow was on, then force solid red on. */
        restore_on = false;
        zmk_rgb_underglow_get_state(&restore_on);
        if (!restore_on) {
            zmk_rgb_underglow_on();
        }
        zmk_rgb_underglow_set_hsb(caps_color);
    } else {
        /* Always set the colour back first so the persisted colour is the
         * normal one rather than red, then restore the on/off state. */
        zmk_rgb_underglow_set_hsb(normal_color);
        if (!restore_on) {
            zmk_rgb_underglow_off();
        }
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(caps_underglow, caps_underglow_listener);
ZMK_SUBSCRIPTION(caps_underglow, zmk_hid_indicators_changed);

#endif /* CONFIG_ZMK_RGB_UNDERGLOW */
