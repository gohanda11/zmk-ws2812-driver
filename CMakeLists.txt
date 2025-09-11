if(CONFIG_WS2812_WIDGET)

target_sources_ifdef(CONFIG_WS2812_WIDGET app PRIVATE src/widget.c)
target_sources_ifdef(CONFIG_WS2812_WIDGET app PRIVATE src/behaviors/behavior_ws2812_widget.c)

target_include_directories(app PRIVATE include)

endif()