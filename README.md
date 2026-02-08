# MorningLight

ESP32-based sunrise alarm clock with RGB LED control and web interface.

## Features

### Implemented

- **WiFi Management**
  - AP mode for initial setup (SSID: MorningLight-XXXX)
  - STA mode for normal operation
  - Automatic fallback to AP mode if connection fails
  - Captive portal for easy configuration
  - Network scanning and selection via web UI
  - Manual SSID entry for hidden networks

- **Web Interface**
  - Mobile-first responsive design
  - Real-time status display (WiFi, time, brightness)
  - Quick Test panel with color temperature and brightness control
  - Alarm management (up to 8 alarms)
  - Settings page for timezone, brightness limit, and PWM frequency
  - Factory reset (resets all settings to defaults, preserves WiFi credentials)

- **LED Controller**
  - Two LED types supported (selectable via web UI):
    - **PWM RGB**: 3-channel PWM for discrete RGB LEDs with MOSFETs
    - **WS2811 Addressable**: Single-wire addressable LED strips
  - 12-bit PWM resolution (0-4095) for PWM mode
  - RGB color mixing
  - Gamma correction (2.2) for perceptual brightness
  - Color temperature control (2000K-6500K)
  - Configurable LED count for addressable strips (up to 300 LEDs)
  - Configurable PWM frequency (100-500 Hz) for PWM mode
  - Smooth fade transitions

- **Sunrise Simulation**
  - Scheduled alarms with day-of-week selection
  - Configurable duration (5-120 minutes)
  - Multiple brightness curves (linear, ease-in-out, logarithmic)
  - Color temperature simulation (warm to cool)
  - Per-alarm brightness and color settings
  - **Auto turn-off (cool-down)**: configurable 1-60 minute gradual dim-to-off after sunrise completes
  - Cool-down works with both classic and animated sunrise modes
  - Client-side overlap detection prevents saving alarms with conflicting time windows

- **Wave Animation Engine**
  - Parametric sine-wave animations for ambient lighting
  - 5 configurable presets (2 built-in + 3 custom)
  - Parameters: wavelength, amplitude, speed, base brightness, variation, color temperature
  - **WS2811 mode**: True spatial wave - each LED has independent brightness based on position
  - **PWM mode**: Temporal wave - all LEDs pulse together over time
  - Organic variation using value noise for natural-looking movement
  - Amplitude scales with brightness during sunrise ramp (starts subtle, grows with brightness)
  - Web UI controls for real-time parameter adjustment
  - Logarithmic sliders for speed (0.05-5.0 Hz) and color temperature (2000-6500K)
  - Coarser slider increments for amplitude, base brightness, and variation (steps of 10)
  - Mutual exclusion with sunrise (animation stops when sunrise starts and vice versa)

- **Dark Mode**
  - Up to 3 configurable no-light schedules
  - Per-schedule day-of-week selection
  - Overnight window support (e.g., 22:00-06:00)
  - Highest priority: actively cancels running sunrise/animation when dark mode window starts
  - Blocks alarm triggers, animation starts, and LED test commands
  - Per-schedule "Allow Override" toggle to permit lights during a window
  - LED "off" commands always allowed (turning off is fine)
  - Dark Mode Active banner on Home page when blocking
  - Settings live in the Alarms page

- **Time Management**
  - NTP synchronization (pool.ntp.org, time.google.com)
  - Configurable timezone (UTC-12 to UTC+14)
  - Automatic periodic sync

- **Configuration Storage**
  - NVS-based persistent storage
  - WiFi credentials
  - Alarm schedules
  - Dark mode schedules
  - User preferences (brightness, PWM frequency)

- **Temperature Monitoring**
  - Internal NTC thermistor (10K, B=3950) on ADC
  - Two external DS18B20 1-Wire sensors on separate buses
  - Background polling task (configurable interval, default 5s)
  - Readings displayed on web UI home screen
  - Exposed via `/api/status` (null when sensor not connected)
  - Non-fatal: device operates normally without sensors attached

- **Button Control**
  - Reset button (GPIO 0) - factory reset on long press
  - Scenario button (GPIO 4) - manual light control

- **Status LED**
  - Visual feedback for device state
  - Blinking patterns for different modes

## Pin Map

| Function | GPIO | Description |
|----------|------|-------------|
| Red LED | 25 | PWM output for red channel (PWM mode) |
| Green LED | 26 | PWM output for green channel (PWM mode) |
| Blue LED | 27 | PWM output for blue channel (PWM mode) |
| Addressable LED | 18 | Data line for WS2811/WS2812 strips |
| Reset Button | 0 | Boot button, long press for factory reset |
| Scenario Button | 4 | Manual light control |
| Status LED | 2 | Built-in LED for status indication |
| NTC Thermistor | 34 | Internal temperature sensor (ADC1_CH6) |
| DS18B20 #1 | 15 | External 1-Wire temperature sensor |
| DS18B20 #2 | 16 | External 1-Wire temperature sensor |

## Hardware

- ESP32 DevKit or compatible board
- For PWM mode: N-channel MOSFETs (e.g., IRLZ44N) for LED switching
- For addressable mode: WS2811 or WS2812 LED strip
- 5V/12V/24V power supply (depending on LED type)

### PWM Mode - MOSFET Wiring (N-channel)

```
ESP32 GPIO ---> MOSFET Gate
LED Strip  ---> MOSFET Drain
GND        ---> MOSFET Source
```

Higher PWM duty cycle = brighter LED output.

### Addressable Mode - WS2811 Wiring

```
ESP32 GPIO 18 ---> Level Shifter ---> WS2811 DIN
ESP32 GND     ---> WS2811 GND
5V/12V        ---> WS2811 VCC
```

**Important**: WS2811 requires 5V logic levels. The ESP32 outputs 3.3V, so a level shifter is required. A simple N-channel MOSFET level shifter circuit works well:

```
3.3V ----+---- 10K ----+---- 5V
         |             |
    ESP32 GPIO    WS2811 DIN
         |             |
         +-- MOSFET ---+
              Source/Drain
              (Gate to 3.3V via 10K)
```

## Web API

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/status` | GET | Device status (WiFi, time, brightness, dark_mode_active, temperatures) |
| `/api/wifi/scan` | GET | Scan for WiFi networks |
| `/api/wifi/connect` | POST | Connect to WiFi network |
| `/api/config` | GET | Get device configuration |
| `/api/config` | POST | Update device configuration |
| `/api/alarms` | GET | Get all alarms |
| `/api/alarms` | POST | Create/update alarm |
| `/api/led/test` | POST | Test LED (color_temp, brightness, off) |
| `/api/time/sync` | POST | Force NTP sync |
| `/api/animation/presets` | GET | Get all animation presets |
| `/api/animation/presets` | POST | Update animation preset |
| `/api/animation/start` | POST | Start animation (preset_id) |
| `/api/animation/stop` | POST | Stop running animation |
| `/api/darkmode` | GET | Get all dark mode schedules |
| `/api/darkmode` | POST | Create/update dark mode schedule |
| `/api/factory-reset` | POST | Factory reset (preserves WiFi credentials) |
| `/api/reboot` | POST | Reboot device |
| `/api/config/export` | GET | Export configuration as JSON file |
| `/api/config/import` | POST | Import configuration from JSON file |

## Wave Animation Parameters

The wave animation creates a moving sine-wave pattern across the LED strip. The brightness of each LED is calculated using:

```
brightness(x, t) = base + amplitude × sin(spatial_phase + temporal_phase) × variation
```

### Parameter Reference

| Parameter | Range | Description |
|-----------|-------|-------------|
| **Wavelength** | 2-300 LEDs | Distance between wave peaks. Lower values create tighter waves, higher values create gentle rolling effects. |
| **Amplitude** | 0-100% | Contrast between brightest and dimmest points. 0% = flat brightness, 100% = full range from dark to bright. |
| **Speed** | 0.0-5.0 Hz | How fast the wave moves. 0.5 Hz = one complete wave cycle every 2 seconds. |
| **Base Brightness** | 0-100% | The center brightness level around which the wave oscillates. |
| **Variation** | 0-100% | Adds organic randomness using value noise. 0% = pure sine wave, higher values add natural-looking irregularity. |
| **Color Temperature** | 2000-6500K | The color of the light from warm (2000K) to cool daylight (6500K). |

### Built-in Presets

| Preset | Wavelength | Amplitude | Speed | Base | Variation | Color |
|--------|------------|-----------|-------|------|-----------|-------|
| **Gentle** | 30 LEDs | 30% | 0.2 Hz | 50% | 20% | 3000K |
| **Ocean** | 50 LEDs | 50% | 0.3 Hz | 40% | 40% | 4500K |

### LED Type Behavior

- **WS2811 (Addressable)**: True spatial wave - each LED displays a different brightness based on its position, creating a visible wave traveling along the strip.
- **PWM (3-channel)**: Temporal wave only - all LEDs share the same output, so they pulse together over time. The wavelength parameter has no visible effect in this mode.

## Building

### Prerequisites

- ESP-IDF v5.x or later
- Python 3.8+

### Build Commands

```bash
# Set up ESP-IDF environment
source ~/esp/esp-idf/export.sh

# Build
idf.py build

# Flash
idf.py -p /dev/ttyUSB0 flash

# Monitor
idf.py -p /dev/ttyUSB0 monitor
```

## Configuration

Default settings can be modified in `sdkconfig.defaults` or via `idf.py menuconfig`.

### Key Configuration Options

- `ML_GPIO_LED_R/G/B` - LED PWM GPIO pins
- `ML_LED_PWM_FREQ_HZ` - Default PWM frequency
- `ML_LED_MAX_BRIGHTNESS` - Maximum brightness limit
- `ML_WIFI_AP_SSID_PREFIX` - AP mode SSID prefix
- `ML_SUNRISE_DEFAULT_DURATION_MIN` - Default sunrise duration
- `ML_ANIM_UPDATE_INTERVAL_MS` - Animation frame rate (default 33ms = ~30fps)
- `ML_ANIM_TASK_STACK_SIZE` - Animation task stack size
- `ML_GPIO_NTC_ADC` - NTC thermistor ADC GPIO (default 34)
- `ML_GPIO_DS18B20_1` / `ML_GPIO_DS18B20_2` - DS18B20 sensor GPIOs (default 15, 16)
- `ML_NTC_BETA` - NTC beta coefficient (default 3950)
- `ML_TEMP_UPDATE_INTERVAL_MS` - Temperature polling interval (default 5000ms)

## Future Features

- [x] Addressable LED support (WS2811, WS2812)
- [x] Wave animation engine with spatial effects
- [x] Auto turn-off / cool-down after sunrise
- [x] Dark mode (no-light schedules with active cutoff)
- [ ] SK6812 RGBW support
- [ ] Sound/buzzer alarm option
- [ ] MQTT integration for smart home systems
- [ ] Home Assistant auto-discovery
- [ ] OTA firmware updates
- [ ] Sleep tracking integration
- [x] Temperature monitoring (NTC + DS18B20)
- [ ] Ambient light sensor for auto-brightness
- [ ] Multi-room synchronization

## License

MIT License

## Contributing

Contributions welcome! Please open an issue or pull request.
