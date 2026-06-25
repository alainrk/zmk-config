/*
 * Caps Lock -> solid red RGB underglow indicator.
 *
 * Listens for the host's HID "Caps Lock" LED state and turns the underglow
 * solid red while Caps Lock is on, restoring the previous state when it
 * turns off.
 *
 * Notes (ZMK v0.3):
 *   - On Caps-off the previous colour and on/off state are restored exactly:
 *     the colour is captured on Caps-on via calc_hue(0) (a pure read of the
 *     live colour, the only public way to read it in v0.3).
 *   - The effect is never changed, so an animated effect (breathe/spectrum/
 *     swirl) is preserved; while Caps is on it just animates tinted red rather
 *     than showing a solid block of red. Keep the Solid effect for steady red.
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

static bool caps_active;
static bool restore_on;                 /* on/off state captured when caps turned on */
static struct zmk_led_hsb restore_color; /* colour captured when caps turned on */

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
        /* Capture the current colour and on/off state, then force solid red on.
         * calc_hue(0) returns the live state.color unchanged (a pure read), the
         * only public way to read the current colour in ZMK v0.3. We never touch
         * the effect, so an animated effect is preserved (just tinted red). */
        restore_color = zmk_rgb_underglow_calc_hue(0);
        restore_on = false;
        zmk_rgb_underglow_get_state(&restore_on);
        if (!restore_on) {
            zmk_rgb_underglow_on();
        }
        zmk_rgb_underglow_set_hsb(caps_color);
    } else {
        /* Restore the exact colour we captured, then the on/off state. */
        zmk_rgb_underglow_set_hsb(restore_color);
        if (!restore_on) {
            zmk_rgb_underglow_off();
        }
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(caps_underglow, caps_underglow_listener);
ZMK_SUBSCRIPTION(caps_underglow, zmk_hid_indicators_changed);

#endif /* CONFIG_ZMK_RGB_UNDERGLOW */
