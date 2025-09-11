# ZMK WS2812 Widget Driver

A ZMK module that provides WS2812 RGB LED indicator functionality for keyboard status display. This driver combines the architecture of `zmk-rgbled-widget` with WS2812 LED strip support, enabling rich visual feedback for battery levels, connectivity status, and layer changes.

## Features

- **Battery Level Indication**: Visual feedback for battery status with color-coded alerts
- **Connectivity Status**: Shows USB/BLE connection states with different colors
- **Layer-Specific Colors**: Persistent colors for each layer (0=off, 1=red, 2=green, 3=yellow, 4=blue, 5=purple, 6=cyan)
- **Activity State Management**: LEDs turn off during sleep and restore on wake
- **Configurable Colors**: Full RGB color customization via Kconfig
- **Flexible Patterns**: Customizable blink durations and repeat counts
- **Split Keyboard Support**: Proper central/peripheral role handling

## Hardware Requirements

- WS2812/WS2812B/SK6812 compatible LED strip
- SPI or PIO interface (depending on your microcontroller)
- Proper power supply for LED strip

## Installation

1. Add this repository as a submodule to your ZMK config:
```bash
git submodule add https://github.com/your-repo/zmk-ws2812-driver.git modules/zmk-ws2812-driver
```

2. Update your `config/west.yml`:
```yaml
manifest:
  projects:
    - name: zmk-ws2812-driver
      path: modules/zmk-ws2812-driver
      revision: main
  self:
    path: config
```

## Configuration

### 1. Device Tree Setup

Add WS2812 LED strip configuration to your board's `.overlay` file:

```dts
// For nRF52-based boards (SPI)
&pinctrl {
    spi3_default: spi3_default {
        group1 {
            psels = <NRF_PSEL(SPIM_MOSI, 0, 6)>; // Adjust pin as needed
        };
    };
    spi3_sleep: spi3_sleep {
        group1 {
            psels = <NRF_PSEL(SPIM_MOSI, 0, 6)>;
            low-power-enable;
        };
    };
};

&spi3 {
    compatible = "nordic,nrf-spim";
    status = "okay";
    pinctrl-0 = <&spi3_default>;
    pinctrl-1 = <&spi3_sleep>;
    pinctrl-names = "default", "sleep";
    
    led_strip: ws2812@0 {
        compatible = "worldsemi,ws2812-spi";
        reg = <0>;
        spi-max-frequency = <4000000>;
        chain-length = <10>; // Number of LEDs in your strip
        spi-one-frame = <0x70>;
        spi-zero-frame = <0x40>;
        color-mapping = <LED_COLOR_ID_GREEN LED_COLOR_ID_RED LED_COLOR_ID_BLUE>;
    };
};

/ {
    chosen {
        zmk,ws2812-widget = &led_strip;
    };
};
```

### 2. Kconfig Setup

Add to your `.conf` file:

```conf
# Enable WS2812 widget
CONFIG_WS2812_WIDGET=y

# Enable LED strip support
CONFIG_LED_STRIP=y
CONFIG_WS2812_STRIP=y

# SPI support (for nRF52 boards)
CONFIG_SPI=y

# Optional: Customize timing
CONFIG_WS2812_WIDGET_BATTERY_BLINK_MS=200
CONFIG_WS2812_WIDGET_CONN_BLINK_MS=150
CONFIG_WS2812_WIDGET_LAYER_BLINK_MS=100
```

### 3. Keymap Integration

Add the behavior to your keymap:

```dts
#include <behaviors.dtsi>
#include <dt-bindings/zmk/keys.h>
#include <dt-bindings/zmk/bt.h>

// Include the WS2812 widget behavior
#include <behaviors/ws2812_widget.dtsi>

/ {
    keymap {
        compatible = "zmk,keymap";
        
        default_layer {
            bindings = <
                &kp TAB   &kp Q    &kp W    &kp E    &kp R    &kp T
                &kp CAPS  &kp A    &kp S    &kp D    &kp F    &kp G
                &kp LSHFT &kp Z    &kp X    &kp C    &kp V    &kp B
                &kp LCTRL &kp LGUI &kp LALT &ws2812_wdg      &kp SPC
            >;
        };
    };
};
```

## Color Configuration

Customize colors by adding to your `.conf` file:

```conf
# Battery colors (hex RGB values)
CONFIG_WS2812_WIDGET_BATTERY_COLOR_HIGH=0x00FF00      # Green
CONFIG_WS2812_WIDGET_BATTERY_COLOR_MEDIUM=0xFFFF00    # Yellow  
CONFIG_WS2812_WIDGET_BATTERY_COLOR_LOW=0xFF8000       # Orange
CONFIG_WS2812_WIDGET_BATTERY_COLOR_CRITICAL=0xFF0000  # Red

# Connectivity colors
CONFIG_WS2812_WIDGET_CONN_COLOR_USB=0x00FF00          # Green (USB)
CONFIG_WS2812_WIDGET_CONN_COLOR_CONNECTED=0x0000FF    # Blue (BLE connected)
CONFIG_WS2812_WIDGET_CONN_COLOR_ADVERTISING=0x00FFFF  # Cyan (BLE advertising)
CONFIG_WS2812_WIDGET_CONN_COLOR_DISCONNECTED=0xFF0000 # Red (disconnected)

# Layer-specific persistent colors
CONFIG_WS2812_WIDGET_LAYER_0_COLOR=0x000000          # Layer 0: Off/Black
CONFIG_WS2812_WIDGET_LAYER_1_COLOR=0xFF0000          # Layer 1: Red
CONFIG_WS2812_WIDGET_LAYER_2_COLOR=0x00FF00          # Layer 2: Green  
CONFIG_WS2812_WIDGET_LAYER_3_COLOR=0xFFFF00          # Layer 3: Yellow
CONFIG_WS2812_WIDGET_LAYER_4_COLOR=0x0000FF          # Layer 4: Blue
CONFIG_WS2812_WIDGET_LAYER_5_COLOR=0xFF00FF          # Layer 5: Purple/Magenta
CONFIG_WS2812_WIDGET_LAYER_6_COLOR=0x00FFFF          # Layer 6: Cyan

# Layer change blink indication
CONFIG_WS2812_WIDGET_LAYER_COLOR=0xFFFFFF            # White (for blink patterns)

# Feature toggles
CONFIG_WS2812_WIDGET_SHOW_LAYER_CHANGE=y             # Enable layer color display
CONFIG_WS2812_WIDGET_SHOW_LAYER_COLORS=n             # Use persistent colors (not blink sequences)
```

## Usage

The widget provides several indication methods:

### Manual Triggers
- Press the `&ws2812_wdg` behavior key to trigger all enabled indicators
- Use separate functions in custom code: `ws2812_indicate_battery()`, `ws2812_indicate_connectivity()`, `ws2812_indicate_layer()`

### Automatic Indicators
- **Battery**: Automatic critical battery warnings on level changes
- **Connectivity**: Shows status on profile changes and connection events
- **Layers**: Indicates layer changes with configurable debouncing

### Status Patterns
- **Battery**: 3 blinks in appropriate color based on level
- **Connectivity**: Number of blinks = BLE profile number + 1  
- **Layers**: Persistent colors showing active layer (configurable per layer)
- **Activity States**: LEDs automatically turn off during sleep and restore on wake

## Power Considerations

WS2812 LEDs have significant current draw. For battery-powered keyboards:

1. Use a power switching circuit to completely disconnect LEDs when not in use
2. Consider reducing LED count or brightness (see brightness control below)
3. Minimize automatic indication frequency
4. Test battery life impact thoroughly
5. Layer 0 automatically turns LEDs off to save power

## Brightness Control

To reduce power consumption and LED intensity, you can adjust the RGB values to be dimmer. For example, to reduce brightness to approximately half:

```conf
# Dimmed layer colors (roughly 50% brightness)
CONFIG_WS2812_WIDGET_LAYER_0_COLOR=0x000000          # Layer 0: Off/Black
CONFIG_WS2812_WIDGET_LAYER_1_COLOR=0x7F0000          # Layer 1: Dimmed Red
CONFIG_WS2812_WIDGET_LAYER_2_COLOR=0x007F00          # Layer 2: Dimmed Green  
CONFIG_WS2812_WIDGET_LAYER_3_COLOR=0x7F7F00          # Layer 3: Dimmed Yellow
CONFIG_WS2812_WIDGET_LAYER_4_COLOR=0x00007F          # Layer 4: Dimmed Blue
CONFIG_WS2812_WIDGET_LAYER_5_COLOR=0x7F007F          # Layer 5: Dimmed Purple
CONFIG_WS2812_WIDGET_LAYER_6_COLOR=0x007F7F          # Layer 6: Dimmed Cyan

# Dimmed battery colors
CONFIG_WS2812_WIDGET_BATTERY_COLOR_HIGH=0x007F00     # Dimmed Green
CONFIG_WS2812_WIDGET_BATTERY_COLOR_MEDIUM=0x7F7F00   # Dimmed Yellow
CONFIG_WS2812_WIDGET_BATTERY_COLOR_LOW=0x7F4000      # Dimmed Orange
CONFIG_WS2812_WIDGET_BATTERY_COLOR_CRITICAL=0x7F0000 # Dimmed Red
```

**Note**: RGB values are in hexadecimal where FF = 255 (maximum), so 7F = 127 (approximately half brightness). Adjust values as needed for your preference.

## Troubleshooting

### LEDs Not Working
- Check power supply voltage (5V typically)
- Verify data pin connection
- Confirm SPI/PIO configuration
- Check `chain-length` matches actual LED count

### Incorrect Colors
- Verify `color-mapping` in device tree
- Check if your LEDs use GRB vs RGB order
- Confirm hex color values in Kconfig

### Build Errors
- Ensure LED strip drivers are enabled in Kconfig
- Check device tree syntax
- Verify all required includes are present

## Hardware Integration Example

For zmk-config-soa39 integration, add to your board overlay:

```dts
// Assuming WS2812 connected to pin P0.06
&pinctrl {
    spi3_default: spi3_default {
        group1 {
            psels = <NRF_PSEL(SPIM_MOSI, 0, 6)>;
        };
    };
    spi3_sleep: spi3_sleep {
        group1 {
            psels = <NRF_PSEL(SPIM_MOSI, 0, 6)>;
            low-power-enable;
        };
    };
};

&spi3 {
    status = "okay";
    pinctrl-0 = <&spi3_default>;
    pinctrl-1 = <&spi3_sleep>;
    pinctrl-names = "default", "sleep";
    
    ws2812: ws2812@0 {
        compatible = "worldsemi,ws2812-spi";
        reg = <0>;
        spi-max-frequency = <4000000>;
        chain-length = <3>; // Adjust for your setup
        color-mapping = <LED_COLOR_ID_GREEN LED_COLOR_ID_RED LED_COLOR_ID_BLUE>;
    };
};

/ {
    chosen {
        zmk,ws2812-widget = &ws2812;
    };
};
```

## Contributing

This project is designed to be a drop-in replacement for simple GPIO LED indicators but with the enhanced capability of WS2812 RGB LEDs. Contributions for additional features, optimizations, and hardware support are welcome.

## License

This project follows the same MIT license as ZMK firmware.