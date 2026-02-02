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

- **Web Interface**
  - Mobile-first responsive design
  - Real-time status display (WiFi, time, brightness)
  - Quick Test panel with color temperature and brightness control
  - Alarm management (up to 8 alarms)
  - Settings page for timezone, brightness limit, and PWM frequency

- **LED Controller**
  - 12-bit PWM resolution (0-4095)
  - RGB color mixing
  - Gamma correction (2.2) for perceptual brightness
  - Color temperature control (2000K-6500K)
  - N-channel MOSFET support
  - Configurable PWM frequency (100-40000 Hz)
  - Smooth fade transitions

- **Sunrise Simulation**
  - Scheduled alarms with day-of-week selection
  - Configurable duration (5-120 minutes)
  - Multiple brightness curves (linear, ease-in-out, logarithmic)
  - Color temperature simulation (warm to cool)
  - Per-alarm brightness and color settings

- **Time Management**
  - NTP synchronization (pool.ntp.org, time.google.com)
  - Configurable timezone (UTC-12 to UTC+14)
  - Automatic periodic sync

- **Configuration Storage**
  - NVS-based persistent storage
  - WiFi credentials
  - Alarm schedules
  - User preferences (brightness, PWM frequency)

- **Button Control**
  - Reset button (GPIO 0) - factory reset on long press
  - Scenario button (GPIO 4) - manual light control

- **Status LED**
  - Visual feedback for device state
  - Blinking patterns for different modes

## Pin Map

| Function | GPIO | Description |
|----------|------|-------------|
| Red LED | 25 | PWM output for red channel |
| Green LED | 26 | PWM output for green channel |
| Blue LED | 27 | PWM output for blue channel |
| Addressable LED | 18 | Data line for WS2812 (future) |
| Reset Button | 0 | Boot button, long press for factory reset |
| Scenario Button | 4 | Manual light control |
| Status LED | 2 | Built-in LED for status indication |

## Hardware

- ESP32 DevKit or compatible board
- N-channel MOSFETs (e.g., IRLZ44N) for LED switching
- RGB LED strip or high-power LEDs
- 12V/24V power supply (depending on LED strip)

### MOSFET Wiring (N-channel)

```
ESP32 GPIO ---> MOSFET Gate
LED Strip  ---> MOSFET Drain
GND        ---> MOSFET Source
```

Higher PWM duty cycle = brighter LED output.

## Web API

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/status` | GET | Device status (WiFi, time, brightness) |
| `/api/wifi/scan` | GET | Scan for WiFi networks |
| `/api/wifi/connect` | POST | Connect to WiFi network |
| `/api/config` | GET | Get device configuration |
| `/api/config` | POST | Update device configuration |
| `/api/alarms` | GET | Get all alarms |
| `/api/alarms` | POST | Create/update alarm |
| `/api/led/test` | POST | Test LED (color_temp, brightness, off) |
| `/api/time/sync` | POST | Force NTP sync |

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

## Future Features

- [ ] Addressable LED support (WS2812B, SK6812)
- [ ] Multiple sunrise profiles/presets
- [ ] Sunset simulation (gradual dimming)
- [ ] Sound/buzzer alarm option
- [ ] MQTT integration for smart home systems
- [ ] Home Assistant auto-discovery
- [ ] OTA firmware updates
- [ ] Sleep tracking integration
- [ ] Ambient light sensor for auto-brightness
- [ ] Motion sensor for wake detection
- [ ] Bluetooth control app
- [ ] Voice assistant integration (Alexa, Google Home)
- [ ] Calendar integration for alarm scheduling
- [ ] Weather-based color temperature adjustment
- [ ] Multi-room synchronization

## License

MIT License

## Contributing

Contributions welcome! Please open an issue or pull request.
