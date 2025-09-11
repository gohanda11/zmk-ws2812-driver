#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>

#include <zmk/battery.h>
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/split/bluetooth/peripheral.h>

#if __has_include(<zmk/split/central.h>)
#include <zmk/split/central.h>
#else
#include <zmk/split/bluetooth/central.h>
#endif

#include <zephyr/logging/log.h>
#include <zmk_ws2812_widget/widget.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define WS2812_STRIP_NODE DT_CHOSEN(zmk_ws2812_widget)

#if !DT_NODE_EXISTS(WS2812_STRIP_NODE)
#error "WS2812 widget chosen node zmk,ws2812-widget not found"
#endif

// WS2812 LED strip device
static const struct device *led_strip = DEVICE_DT_GET(WS2812_STRIP_NODE);
static const uint32_t num_pixels = DT_PROP(WS2812_STRIP_NODE, chain_length);

// Using official Zephyr struct led_rgb from zephyr/drivers/led_strip.h

// Convert hex color to RGB struct
static struct led_rgb hex_to_rgb(uint32_t hex_color) {
    return (struct led_rgb) {
        .r = (hex_color >> 16) & 0xFF,
        .g = (hex_color >> 8) & 0xFF,
        .b = hex_color & 0xFF
    };
}

// Blink pattern structure
struct blink_pattern {
    struct led_rgb color;
    uint16_t duration_ms;
    uint16_t pause_ms;
    uint8_t repeat_count;
};

// flag to indicate whether the initial boot up sequence is complete
static bool initialized = false;

// Current LED state for persistent colors
static struct led_rgb current_color = {0, 0, 0};

// Set all LEDs to the specified color
static int set_leds_color(struct led_rgb color) {
    struct led_rgb pixels[num_pixels];
    
    for (int i = 0; i < num_pixels; i++) {
        pixels[i] = color;
    }
    
    int rc = led_strip_update_rgb(led_strip, pixels, num_pixels);
    if (rc == 0) {
        current_color = color;
    }
    return rc;
}

// Execute a blink pattern
static void execute_blink_pattern(struct blink_pattern pattern) {
    for (int i = 0; i < pattern.repeat_count; i++) {
        // Turn on with pattern color
        set_leds_color(pattern.color);
        k_sleep(K_MSEC(pattern.duration_ms));
        
        // Turn off or pause
        if (pattern.pause_ms > 0) {
            set_leds_color((struct led_rgb){0, 0, 0});
            k_sleep(K_MSEC(pattern.pause_ms));
        }
    }
}

// Message queue for blink patterns
K_MSGQ_DEFINE(led_msgq, sizeof(struct blink_pattern), 16, 1);

// Battery indication
#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING) && IS_ENABLED(CONFIG_WS2812_WIDGET_SHOW_BATTERY)
static struct led_rgb get_battery_color(uint8_t battery_level) {
    if (battery_level == 0) {
        return hex_to_rgb(CONFIG_WS2812_WIDGET_COLOR_OFF);
    }
    if (battery_level >= CONFIG_WS2812_WIDGET_BATTERY_LEVEL_HIGH) {
        return hex_to_rgb(CONFIG_WS2812_WIDGET_BATTERY_COLOR_HIGH);
    }
    if (battery_level >= CONFIG_WS2812_WIDGET_BATTERY_LEVEL_LOW) {
        return hex_to_rgb(CONFIG_WS2812_WIDGET_BATTERY_COLOR_MEDIUM);
    }
    if (battery_level <= CONFIG_WS2812_WIDGET_BATTERY_LEVEL_CRITICAL) {
        return hex_to_rgb(CONFIG_WS2812_WIDGET_BATTERY_COLOR_CRITICAL);
    }
    return hex_to_rgb(CONFIG_WS2812_WIDGET_BATTERY_COLOR_LOW);
}

void ws2812_indicate_battery(void) {
    uint8_t battery_level = zmk_battery_state_of_charge();
    int retry = 0;
    
    while (battery_level == 0 && retry++ < 10) {
        k_sleep(K_MSEC(100));
        battery_level = zmk_battery_state_of_charge();
    }
    
    struct blink_pattern pattern = {
        .color = get_battery_color(battery_level),
        .duration_ms = CONFIG_WS2812_WIDGET_BATTERY_BLINK_MS,
        .pause_ms = CONFIG_WS2812_WIDGET_INTERVAL_MS,
        .repeat_count = 3
    };
    
    LOG_INF("Indicating battery level %d with color r:%d g:%d b:%d", 
            battery_level, pattern.color.r, pattern.color.g, pattern.color.b);
    
    k_msgq_put(&led_msgq, &pattern, K_NO_WAIT);
}

static int led_battery_listener_cb(const zmk_event_t *eh) {
    if (!initialized) {
        return 0;
    }
    
    uint8_t battery_level = as_zmk_battery_state_changed(eh)->state_of_charge;
    
    if (battery_level > 0 && battery_level <= CONFIG_WS2812_WIDGET_BATTERY_LEVEL_CRITICAL) {
        LOG_INF("Critical battery level %d, blinking warning", battery_level);
        
        struct blink_pattern pattern = {
            .color = hex_to_rgb(CONFIG_WS2812_WIDGET_BATTERY_COLOR_CRITICAL),
            .duration_ms = CONFIG_WS2812_WIDGET_BATTERY_BLINK_MS,
            .pause_ms = CONFIG_WS2812_WIDGET_BATTERY_BLINK_MS,
            .repeat_count = 5
        };
        k_msgq_put(&led_msgq, &pattern, K_NO_WAIT);
    }
    return 0;
}

ZMK_LISTENER(led_battery_listener, led_battery_listener_cb);
ZMK_SUBSCRIPTION(led_battery_listener, zmk_battery_state_changed);
#endif // Battery reporting

// Connectivity indication
#if IS_ENABLED(CONFIG_WS2812_WIDGET_SHOW_CONNECTIVITY)
static void indicate_connectivity_internal(void) {
    struct blink_pattern pattern = {
        .duration_ms = CONFIG_WS2812_WIDGET_CONN_BLINK_MS,
        .pause_ms = CONFIG_WS2812_WIDGET_INTERVAL_MS,
        .repeat_count = 1
    };

#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    switch (zmk_endpoints_selected().transport) {
    case ZMK_TRANSPORT_USB:
#if IS_ENABLED(CONFIG_WS2812_WIDGET_CONN_SHOW_USB)
        LOG_INF("USB connected");
        pattern.color = hex_to_rgb(CONFIG_WS2812_WIDGET_CONN_COLOR_USB);
        break;
#endif
    default: // ZMK_TRANSPORT_BLE
#if IS_ENABLED(CONFIG_ZMK_BLE)
        uint8_t profile_index = zmk_ble_active_profile_index();
        if (zmk_ble_active_profile_is_connected()) {
            LOG_INF("Profile %d connected", profile_index);
            pattern.color = hex_to_rgb(CONFIG_WS2812_WIDGET_CONN_COLOR_CONNECTED);
            pattern.repeat_count = profile_index + 1;
        } else if (zmk_ble_active_profile_is_open()) {
            LOG_INF("Profile %d advertising", profile_index);  
            pattern.color = hex_to_rgb(CONFIG_WS2812_WIDGET_CONN_COLOR_ADVERTISING);
            pattern.repeat_count = profile_index + 1;
        } else {
            LOG_INF("Profile %d disconnected", profile_index);
            pattern.color = hex_to_rgb(CONFIG_WS2812_WIDGET_CONN_COLOR_DISCONNECTED);
            pattern.repeat_count = profile_index + 1;
        }
#endif
        break;
    }
#elif IS_ENABLED(CONFIG_ZMK_SPLIT_BLE)
    if (zmk_split_bt_peripheral_is_connected()) {
        LOG_INF("Peripheral connected");
        pattern.color = hex_to_rgb(CONFIG_WS2812_WIDGET_CONN_COLOR_CONNECTED);
    } else {
        LOG_INF("Peripheral disconnected");
        pattern.color = hex_to_rgb(CONFIG_WS2812_WIDGET_CONN_COLOR_DISCONNECTED);
    }
#endif

    k_msgq_put(&led_msgq, &pattern, K_NO_WAIT);
}

static int led_output_listener_cb(const zmk_event_t *eh) {
    if (initialized) {
        indicate_connectivity_internal();
    }
    return 0;
}

// Debouncing to ignore all but last connectivity event
static struct k_work_delayable indicate_connectivity_work;
static void indicate_connectivity_cb(struct k_work *work) { 
    indicate_connectivity_internal(); 
}

void ws2812_indicate_connectivity() { 
    k_work_reschedule(&indicate_connectivity_work, K_MSEC(16)); 
}

ZMK_LISTENER(led_output_listener, led_output_listener_cb);

#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#if IS_ENABLED(CONFIG_WS2812_WIDGET_CONN_SHOW_USB)
ZMK_SUBSCRIPTION(led_output_listener, zmk_endpoint_changed);
#endif
#if IS_ENABLED(CONFIG_ZMK_BLE)
ZMK_SUBSCRIPTION(led_output_listener, zmk_ble_active_profile_changed);
#endif
#elif IS_ENABLED(CONFIG_ZMK_SPLIT_BLE)
ZMK_SUBSCRIPTION(led_output_listener, zmk_split_peripheral_status_changed);
#endif
#endif // Connectivity

// Layer color mapping: 0=off, 1=red, 2=green, 3=yellow, 4=blue, 5=purple, 6=cyan
static struct led_rgb get_layer_color(uint8_t layer) {
    switch (layer) {
        case 0: return hex_to_rgb(CONFIG_WS2812_WIDGET_LAYER_0_COLOR);
        case 1: return hex_to_rgb(CONFIG_WS2812_WIDGET_LAYER_1_COLOR);
        case 2: return hex_to_rgb(CONFIG_WS2812_WIDGET_LAYER_2_COLOR);
        case 3: return hex_to_rgb(CONFIG_WS2812_WIDGET_LAYER_3_COLOR);
        case 4: return hex_to_rgb(CONFIG_WS2812_WIDGET_LAYER_4_COLOR);
        case 5: return hex_to_rgb(CONFIG_WS2812_WIDGET_LAYER_5_COLOR);
        case 6: return hex_to_rgb(CONFIG_WS2812_WIDGET_LAYER_6_COLOR);
        default: return hex_to_rgb(CONFIG_WS2812_WIDGET_COLOR_WHITE);
    }
}

// Layer indication  
#if IS_ENABLED(CONFIG_WS2812_WIDGET_SHOW_LAYER_CHANGE)
#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

void ws2812_indicate_layer(void) {
    uint8_t layer = zmk_keymap_highest_layer_active();
    struct led_rgb color = get_layer_color(layer);
    
    LOG_INF("Setting layer %d color: r:%d g:%d b:%d", layer, color.r, color.g, color.b);
    
    // Set persistent color directly instead of using blink pattern
    set_leds_color(color);
    current_color = color;
}
#endif // Split check

static struct k_work_delayable layer_indicate_work;
static int led_layer_listener_cb(const zmk_event_t *eh) {
#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    // Check if this is an activity state changed event
    struct zmk_activity_state_changed *activity_ev = as_zmk_activity_state_changed(eh);
    if (activity_ev != NULL) {
        switch (activity_ev->state) {
        case ZMK_ACTIVITY_SLEEP:
            LOG_INF("Detected sleep activity state, turn off LED");
            set_leds_color((struct led_rgb){0, 0, 0});
            current_color = (struct led_rgb){0, 0, 0};
            break;
        case ZMK_ACTIVITY_ACTIVE:
            // When waking up, restore the current layer color
            if (initialized) {
                k_work_reschedule(&layer_indicate_work, K_MSEC(CONFIG_WS2812_WIDGET_LAYER_DEBOUNCE_MS));
            }
            break;
        default:
            break;
        }
        return 0;
    }

    // It must be a layer change event - handle both activation and deactivation
    if (initialized) {
        k_work_reschedule(&layer_indicate_work, K_MSEC(CONFIG_WS2812_WIDGET_LAYER_DEBOUNCE_MS));
    }
#endif
    return 0;
}

static void indicate_layer_cb(struct k_work *work) { 
#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    ws2812_indicate_layer(); 
#endif
}

#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
ZMK_LISTENER(led_layer_listener, led_layer_listener_cb);
ZMK_SUBSCRIPTION(led_layer_listener, zmk_layer_state_changed);
ZMK_SUBSCRIPTION(led_layer_listener, zmk_activity_state_changed);
#endif
#endif // Layer change

// LED processing thread
extern void led_process_thread(void *d0, void *d1, void *d2) {
    ARG_UNUSED(d0);
    ARG_UNUSED(d1);
    ARG_UNUSED(d2);

    k_work_init_delayable(&indicate_connectivity_work, indicate_connectivity_cb);
#if IS_ENABLED(CONFIG_WS2812_WIDGET_SHOW_LAYER_CHANGE)
    k_work_init_delayable(&layer_indicate_work, indicate_layer_cb);
#endif

    while (true) {
        struct blink_pattern pattern;
        k_msgq_get(&led_msgq, &pattern, K_FOREVER);
        
        LOG_DBG("Executing blink pattern: r:%d g:%d b:%d, duration:%d, repeat:%d",
                pattern.color.r, pattern.color.g, pattern.color.b, 
                pattern.duration_ms, pattern.repeat_count);
        
        execute_blink_pattern(pattern);
        
        // Return to current layer color after pattern
#if IS_ENABLED(CONFIG_WS2812_WIDGET_SHOW_LAYER_CHANGE)
#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
        uint8_t layer = zmk_keymap_highest_layer_active();
        struct led_rgb layer_color = get_layer_color(layer);
        set_leds_color(layer_color);
        current_color = layer_color;
#else
        set_leds_color((struct led_rgb){0, 0, 0});
#endif
#else
        set_leds_color((struct led_rgb){0, 0, 0});
#endif
        k_sleep(K_MSEC(CONFIG_WS2812_WIDGET_INTERVAL_MS));
    }
}

K_THREAD_DEFINE(led_process_tid, 1024, led_process_thread, NULL, NULL, NULL,
                K_LOWEST_APPLICATION_THREAD_PRIO, 0, 100);

// Initialization thread
extern void led_init_thread(void *d0, void *d1, void *d2) {
    ARG_UNUSED(d0);
    ARG_UNUSED(d1);
    ARG_UNUSED(d2);

    // Check LED strip device
    if (!device_is_ready(led_strip)) {
        LOG_ERR("WS2812 LED strip device not ready");
        return;
    }
    
    LOG_INF("WS2812 LED strip initialized with %d pixels", num_pixels);

#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING) && IS_ENABLED(CONFIG_WS2812_WIDGET_SHOW_BATTERY)
    LOG_INF("Indicating initial battery status");
    ws2812_indicate_battery();
    k_sleep(K_MSEC(CONFIG_WS2812_WIDGET_BATTERY_BLINK_MS + CONFIG_WS2812_WIDGET_INTERVAL_MS));
#endif

#if IS_ENABLED(CONFIG_WS2812_WIDGET_SHOW_CONNECTIVITY)
    LOG_INF("Indicating initial connectivity status");
    ws2812_indicate_connectivity();
#endif

#if IS_ENABLED(CONFIG_WS2812_WIDGET_SHOW_LAYER_CHANGE)
#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    // Set initial layer color
    k_sleep(K_MSEC(CONFIG_WS2812_WIDGET_INTERVAL_MS));
    uint8_t initial_layer = zmk_keymap_highest_layer_active();
    struct led_rgb initial_color = get_layer_color(initial_layer);
    set_leds_color(initial_color);
    current_color = initial_color;
    LOG_INF("Set initial layer %d color: r:%d g:%d b:%d", initial_layer, initial_color.r, initial_color.g, initial_color.b);
#endif
#endif

    initialized = true;
    LOG_INF("Finished initializing WS2812 LED widget");
}

K_THREAD_DEFINE(led_init_tid, 1024, led_init_thread, NULL, NULL, NULL,
                K_LOWEST_APPLICATION_THREAD_PRIO, 0, 200);