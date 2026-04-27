# little-printer

A XIAO ESP32-C3 drives an MC206H thermal printer to print a daily briefing at 09:00 America/Los_Angeles, every day, on a 58mm receipt roll.

The briefing contains:

- The current date (uppercase weekday + month, large font)
- A small weather sprite (sun / cloud / rain / snow)
- Current weather for San Carlos, CA — temperature, conditions, wind
- One random inspirational quote with attribution

Manual trigger: type `p` on the USB-CDC serial console to print on demand. Otherwise the device is silent until 09:00.

## Hardware

- XIAO ESP32-C3 (Seeed Studio)
- MC206H thermal printer (58mm paper, ESC/POS, TTL serial @ 9600 8N1)
- 5–9V DC supply, minimum 2.5A
- 58mm thermal paper roll

Full wiring (4-pin TTL JST + 2-pin power JST) and the rationale behind it lives in [SPEC.md](./SPEC.md).

## Build and flash

Requires ESP-IDF v5.x with the export script sourced (`. $IDF_PATH/export.sh`).

```bash
idf.py set-target esp32c3
idf.py menuconfig          # set Wi-Fi SSID/password under "Briefing Printer Config"
idf.py build flash monitor # Ctrl+] to exit monitor
```

On boot, the firmware connects to Wi-Fi, syncs time via NTP, and waits. The first scheduled print fires at the configured `HH:MM` the next time that minute boundary occurs (de-duplicated to one print per calendar day).

## Configuration

Available under `idf.py menuconfig → Briefing Printer Config`:

| Key                   | Default | Notes                                              |
| --------------------- | ------- | -------------------------------------------------- |
| `WIFI_SSID`           | (empty) | 2.4 GHz only — the C3 doesn't do 5 GHz             |
| `WIFI_PASSWORD`       | (empty) |                                                    |
| `PRINT_HOUR`          | 9       | Local hour to fire the daily briefing              |
| `PRINT_MINUTE`        | 0       |                                                    |
| `PRINTER_BAUD`        | 9600    | Match the printer's self-test report               |

Pins, timezone, and location coordinates are compile-time constants in `main/config.h`.

## Repo layout

```
little-printer/
├── CMakeLists.txt              top-level IDF project
├── sdkconfig.defaults
├── SPEC.md                     full project specification
├── README.md                   you are here
├── main/                       app code (Wi-Fi, NTP, APIs, briefing, scheduler)
├── components/thermal_printer/ ESC/POS UART driver
├── host_tests/                 host-side unit tests (cc, no IDF)
├── diag/                       printer-acceptance firmware (separate IDF project)
├── diagnostic-firmware-spec.md spec for diag/
└── docs/superpowers/           design docs and the implementation plan
```

The `diag/` subfolder is its own minimal IDF project that exercises a freshly-acquired printer at three escalating heat levels. Useful as a smoke test if anything ever seems wrong with the printer; not part of the main build.

## Tests

Pure helpers (text wrap, weather-code lookup) have host-side unit tests:

```bash
cc -Wall -o /tmp/test_text_wrap host_tests/test_text_wrap.c main/text_wrap.c
/tmp/test_text_wrap
cc -Wall -o /tmp/test_weather_code host_tests/test_weather_code.c
/tmp/test_weather_code
```

Hardware-touching code is verified by flashing and observing the actual printer.

## Known polish items

- Weather sprites at 24×24 are too small to be recognisable. Bigger sprites (or a real icon font) would be a nice v1.1.
- A 10kΩ pullup from GPIO21 to 3.3V would silence the burst of garbage characters that prints during `idf.py flash` (the boot ROM runs the pin during programming, our firmware can't drive it then).

## Future scope (intentionally out of scope for v1)

- MQTT subscriber for ad-hoc "print this" messages
- Physical button on a spare GPIO for on-demand quotes
- Deep sleep + RTC alarm for battery operation
- OTA updates
- Additional content sources (Bluesky mentions, xkcd of the day, etc.)

Architecturally, the cleanest path to multi-source is to move "what should we print" off the C3 entirely — run a small service that owns all integrations and publishes to an MQTT topic, reducing the C3 to a dumb MQTT-to-print bridge.

## Credits

Open weather data: [Open-Meteo](https://open-meteo.com/). Quotes: [ZenQuotes](https://zenquotes.io/). ESC/POS reverse engineering: Adafruit's `Adafruit_Thermal.cpp` (originally for the CSN-A2; the byte sequences carry over to the MC206H cleanly).
