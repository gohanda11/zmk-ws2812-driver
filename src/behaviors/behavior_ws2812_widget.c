#define DT_DRV_COMPAT zmk_behavior_ws2812_widget

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>

#include <zmk/behavior.h>

#include <zmk_ws2812_widget/widget.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct behavior_ws2812_wdg_config {
    bool indicate_battery;
    bool indicate_connectivity;
    bool indicate_layer;
};

static int __maybe_unused behavior_ws2812_wdg_init(const struct device *dev) { return 0; }

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
#if IS_ENABLED(CONFIG_WS2812_WIDGET)
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_ws2812_wdg_config *cfg = dev->config;

#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING) && IS_ENABLED(CONFIG_WS2812_WIDGET_SHOW_BATTERY)
    if (cfg->indicate_battery) {
        ws2812_indicate_battery();
    }
#endif
#if (IS_ENABLED(CONFIG_ZMK_USB) || IS_ENABLED(CONFIG_ZMK_BLE)) && IS_ENABLED(CONFIG_WS2812_WIDGET_SHOW_CONNECTIVITY)
    if (cfg->indicate_connectivity) {
        ws2812_indicate_connectivity();
    }
#endif
#if (!IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)) && IS_ENABLED(CONFIG_WS2812_WIDGET_SHOW_LAYER_CHANGE)
    if (cfg->indicate_layer) {
        ws2812_indicate_layer();
    }
#endif
#endif // IS_ENABLED(CONFIG_WS2812_WIDGET)

    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api __maybe_unused behavior_ws2812_wdg_driver_api = {
    .binding_pressed = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
    .locality = BEHAVIOR_LOCALITY_GLOBAL,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
};

#define WS2812_WDG_INST(n)                                                                          \
    static struct behavior_ws2812_wdg_config behavior_ws2812_wdg_config_##n = {                     \
        .indicate_battery = DT_INST_PROP(n, indicate_battery),                                      \
        .indicate_connectivity = DT_INST_PROP(n, indicate_connectivity),                            \
        .indicate_layer = DT_INST_PROP(n, indicate_layer),                                          \
    };                                                                                              \
    BEHAVIOR_DT_INST_DEFINE(n, behavior_ws2812_wdg_init, NULL, NULL, &behavior_ws2812_wdg_config_##n, \
                            POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                       \
                            &behavior_ws2812_wdg_driver_api);

DT_INST_FOREACH_STATUS_OKAY(WS2812_WDG_INST)