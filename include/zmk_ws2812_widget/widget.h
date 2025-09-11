#pragma once

#include <stdint.h>

/**
 * @brief Indicate current battery status with WS2812 LED colors/patterns
 */
void ws2812_indicate_battery(void);

/**
 * @brief Indicate current connectivity status with WS2812 LED colors/patterns  
 */
void ws2812_indicate_connectivity(void);

/**
 * @brief Indicate current layer with WS2812 LED colors/patterns
 */
void ws2812_indicate_layer(void);