# Desk Thermal Printer — Daily Briefing

> **Status:** This is the original v1 design document, written before the printer migration and before the public-message-queue feature. Hardware/wiring/ESC-POS/APIs are still authoritative and current. The scheduler/firmware-task layout has evolved — for current behaviour see [README.md](./README.md) and the per-feature design docs in `docs/superpowers/specs/`. The "Future extensions" section near the bottom no longer reflects what's in flight.

## Project summary

A XIAO ESP32-C3 drives a MC206H thermal printer to print a daily briefing at **09:00 America/Los_Angeles** containing:

- Current weather for San Carlos, CA
- One random inspirational quote

Target framework is **ESP-IDF** (v5.x). Scope is intentionally small for v1. Future extensions (MQTT, webhooks, on-demand button) are called out at the end but out of scope here.

---

## Hardware

- XIAO ESP32-C3 (Seeed Studio)
- MC206H thermal printer (58mm paper)
- Thermal paper roll, 58mm
- **External 5V–9V DC power supply, minimum 2.5A (3A recommended)** — this is not negotiable; see power notes. The MC206H ships with a 2-pin JST power connector; if your supply has a barrel jack (or anything else), splice a JST-to-barrel adapter between them. The unit used here is on a 9V 3A barrel supply via such an adapter.
- The MC206H's data leads come pre-terminated in a 4-pin JST. Hookup wire is only needed if you splice into it.

---

## Wiring

### Data — printer to XIAO C3 (4-pin JST, 3 wires used)

| Printer lead          | JST color (this MC206H) | → | XIAO C3 pin (label / GPIO)  |
| --------------------- | ----------------------- | - | --------------------------- |
| GND (data)            | Black                   | → | GND                         |
| DTR                   | Red                     | → | **not connected**           |
| RX (printer input)    | White                   | → | **D6 / GPIO21** (UART1 TX)  |
| TX (printer output)   | Blue                    | → | **D7 / GPIO20** (UART1 RX) — optional, for status reads |

Code uses GPIO numbers (`21`, `20`); the board silkscreen uses D6/D7. They refer to the same pins.

**About DTR:** the printer drives DTR as a hardware-flow-control output (asserts when its input buffer fills). At our data rates (a few hundred bytes once a day, paced with `vTaskDelay` and `uart_wait_tx_done`) the buffer never fills, so DTR is unused. Leave it floating — do **not** tie it to ground or to a C3 GPIO.

JST color assignments are not standardized across vendors. If a future replacement printer's JST has different colors, identify the leads by position on the connector or by continuity-testing to the printer's PCB silkscreen.

### Power — printer to external supply (separate from C3)

The MC206H's power leads come on a 2-pin JST connector. Adapt to whatever your supply uses.

| Printer lead            | → | Connection                    |
| ----------------------- | - | ----------------------------- |
| + (red)                 | → | Supply +V (5V–9V)             |
| − (black)               | → | Supply GND                    |

**Common ground:** the printer's data GND lead and its power GND are internally bonded inside the printer, so connecting the data-GND lead to the C3 (per the data wiring table above) establishes the common ground both rails need. Tying the supply GND directly to the C3 GND on top of that is redundant but harmless. Without any common ground, the UART won't work reliably.

During development, power the C3 via its USB-C port. For the finished build, either keep USB-C (simplest) or tap the 5V supply through a small 3.3V buck into the C3's 5V pin.

### Critical power notes

- The MC206H's spec sheet calls for a minimum 2.5A supply when the thermal head fires. Do **not** power it from USB, the C3's pins, or anything that isn't a proper wall adapter or bench supply rated for the current. Undersized supplies cause dropped characters, brownouts, garbled prints, and occasionally toasted USB ports.
- The printer's RX is 5V-tolerant and accepts 3.3V logic from the C3's TX without level shifting.
- If you wire up printer TX → C3 RX for status reads, note that thermal printers in this class often drive 5V on TX. Use a simple divider (e.g., 10 kΩ + 20 kΩ) or a cheap bidirectional level shifter on that single line. Outbound (C3 → printer) needs nothing.

### Determining baud rate

With the printer unplugged, **hold the FEED button, then plug in power**. The printer prints a self-test page that includes its configured baud rate (usually 9600 or 19200). Use whatever it reports.

---

## Software stack

**ESP-IDF v5.x.** Assumes `idf.py` is on PATH and the export script has been sourced in the shell.

IDF components used (all first-party, no external deps for v1):

- `esp_wifi`, `esp_netif`, `esp_event` — networking
- `nvs_flash` — required for Wi-Fi config storage
- `esp_http_client` + `esp_crt_bundle` — HTTPS client with Mozilla root cert bundle
- `esp_netif_sntp` — NTP time sync
- `driver/uart` — printer serial link
- `cJSON` (bundled with IDF) — API response parsing
- FreeRTOS — tasks and timing

No Arduino, no external thermal printer library. The `thermal_printer` component is hand-rolled; it's small.

### Common commands

```bash
idf.py set-target esp32c3
idf.py menuconfig                       # configure Wi-Fi creds, etc.
idf.py build
idf.py -p /dev/cu.usbmodem* flash monitor   # upload + open console
# Ctrl+] to exit monitor
```

---

## Project structure

```
project/
├── CMakeLists.txt
├── sdkconfig                      # generated by menuconfig, gitignored
├── sdkconfig.defaults             # committed defaults
├── main/
│   ├── CMakeLists.txt
│   ├── Kconfig.projbuild          # user-visible menuconfig options
│   ├── main.c                     # app_main(), briefing scheduler task
│   ├── wifi.c / wifi.h
│   ├── time_sync.c / time_sync.h
│   ├── weather.c / weather.h
│   ├── quote.c / quote.h
│   └── briefing.c / briefing.h    # orchestrates fetch + format + print
└── components/
    └── thermal_printer/
        ├── CMakeLists.txt
        ├── include/thermal_printer.h
        └── thermal_printer.c
```

---

## Configuration

Use Kconfig for anything secret or likely to change (Wi-Fi creds, print time). Pins and location can stay as `#define`s in a small `config.h` since they're hardware constants.

`main/Kconfig.projbuild`:

```kconfig
menu "Briefing Printer Config"

config WIFI_SSID
    string "Wi-Fi SSID"
    default ""

config WIFI_PASSWORD
    string "Wi-Fi Password"
    default ""

config PRINT_HOUR
    int "Hour to print briefing (0-23, local time)"
    default 9
    range 0 23

config PRINT_MINUTE
    int "Minute to print briefing (0-59)"
    default 0
    range 0 59

config PRINTER_BAUD
    int "Printer UART baud rate"
    default 9600
    help
      Match the rate reported on the printer's self-test page.

endmenu
```

`sdkconfig.defaults` should enable at least:
```
CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y
CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192
```

`main/config.h`:
```c
#pragma once
#define LOCATION_NAME  "San Carlos, CA"
#define LOCATION_LAT   37.5072
#define LOCATION_LON  -122.2605

#define PRINTER_UART_NUM    UART_NUM_1
#define PRINTER_TX_PIN      21    // D6 — goes to printer RX
#define PRINTER_RX_PIN      20    // D7 — from printer TX
```

---

## Thermal printer component

The MC206H speaks a subset of ESC/POS. You send bytes over UART, it prints. No library needed — write a minimal component.

### Commands actually used

| Bytes         | Meaning                                    |
| ------------- | ------------------------------------------ |
| `1B 40`       | `ESC @` — initialize / reset               |
| `1B 37 n1 n2 n3` | `ESC 7` — set heating dots / time / interval (controls print darkness + speed) |
| `1B 61 n`     | `ESC a` — justify: 0=left, 1=center, 2=right |
| `1B 45 n`     | `ESC E` — bold: 0=off, 1=on                |
| `1D 21 n`     | `GS !` — character size: 0x00 normal, 0x10 double-height, 0x20 double-width, 0x11 double both |
| `1B 64 n`     | `ESC d` — feed n lines                     |
| `0A`          | `LF` — line feed (prints buffered line)    |
| `1B 38 n1 n2` | `ESC 8` — sleep after N seconds idle       |

Starting heating values for the MC206H, validated by the `diag/` acceptance test on this unit (pass 1 produced the cleanest output): heating dots = 11, heating time = 120 (×10µs), heating interval = 40 (×10µs). These values were originally derived from Adafruit's CSN-A2 reverse engineering and carry over cleanly to the MC206H. Retune if a future unit prints differently.

If prints come out too light, increase heating time. If too slow or the motor stutters, decrease heating dots. If lines smear vertically, increase heating interval.

### API

```c
// thermal_printer.h
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "driver/uart.h"
#include "esp_err.h"

esp_err_t thermal_printer_init(uart_port_t uart_num, int tx_pin, int rx_pin, int baud);
void thermal_printer_reset(void);
void thermal_printer_set_bold(bool on);
void thermal_printer_set_justify(char j);    // 'L', 'C', 'R'
void thermal_printer_set_size(char s);       // 'S' (normal), 'M' (dbl height), 'L' (dbl both)
void thermal_printer_print(const char *text);    // no trailing newline
void thermal_printer_println(const char *text);  // adds \n
void thermal_printer_feed(uint8_t lines);
void thermal_printer_sleep(uint8_t seconds); // 0 = stay awake
```

### UART init sketch

```c
uart_config_t cfg = {
    .baud_rate  = baud,
    .data_bits  = UART_DATA_8_BITS,
    .parity     = UART_PARITY_DISABLE,
    .stop_bits  = UART_STOP_BITS_1,
    .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
    .source_clk = UART_SCLK_DEFAULT,
};
ESP_ERROR_CHECK(uart_driver_install(uart_num, 512, 0, 0, NULL, 0));
ESP_ERROR_CHECK(uart_param_config(uart_num, &cfg));
ESP_ERROR_CHECK(uart_set_pin(uart_num, tx_pin, rx_pin,
                             UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
```

### Pacing

The MC206H has a small internal buffer and will drop characters if flooded. Between large chunks (e.g., after each `println`) insert a small delay proportional to the bytes written — roughly **1ms per byte at 9600 baud** is a reasonable heuristic, or use `uart_wait_tx_done()` to block until the TX FIFO drains. For the briefing we're printing maybe 300 bytes total, so a single `vTaskDelay(pdMS_TO_TICKS(50))` between logical sections is plenty.

### Reference

Adafruit's Arduino library (`Adafruit_Thermal.cpp` on GitHub) is an excellent reference for exact ESC/POS byte sequences — cross-reference it when implementing each function. Not using it as a dependency, just as documentation.

---

## APIs

Both are free, no API key, hobby-friendly.

### Weather — Open-Meteo

```
https://api.open-meteo.com/v1/forecast
  ?latitude=37.5072
  &longitude=-122.2605
  &current=temperature_2m,weather_code,wind_speed_10m
  &temperature_unit=fahrenheit
  &wind_speed_unit=mph
  &timezone=America/Los_Angeles
```

Response:
```json
{
  "current": {
    "temperature_2m": 62.4,
    "weather_code": 3,
    "wind_speed_10m": 8.2
  }
}
```

Weather codes are WMO codes. Include a small lookup table in `weather.c` mapping codes → short strings ("Clear", "Partly cloudy", "Light rain", "Fog", etc.). A dozen buckets is plenty; don't try to cover all 99.

### Quote — ZenQuotes

```
https://zenquotes.io/api/random
```

Response:
```json
[{ "q": "The best way out is always through.", "a": "Robert Frost" }]
```

Top-level is an array — remember to index `[0]` when parsing.

### HTTP client pattern

Use `esp_http_client` with a user-event-handler that accumulates the response body into a heap buffer. Standard IDF idiom — the `esp_http_client` example in the IDF tree is close to what's needed. Enable `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE` in sdkconfig and set `.crt_bundle_attach = esp_crt_bundle_attach` on the client config for TLS verification.

### JSON parsing

cJSON is bundled with IDF. Typical pattern:

```c
cJSON *root = cJSON_Parse(body);
cJSON *current = cJSON_GetObjectItem(root, "current");
double temp_f = cJSON_GetObjectItem(current, "temperature_2m")->valuedouble;
int code = cJSON_GetObjectItem(current, "weather_code")->valueint;
// ... copy any strings out before deleting
cJSON_Delete(root);
```

Always null-check every `GetObjectItem` — APIs change shape, and cJSON returns NULL rather than raising anything.

---

## Time handling

```c
#include "esp_netif_sntp.h"

esp_netif_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
ESP_ERROR_CHECK(esp_netif_sntp_init(&cfg));

if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(15000)) != ESP_OK) {
    ESP_LOGW(TAG, "NTP sync timeout");
}

setenv("TZ", "PST8PDT,M3.2.0,M11.1.0", 1);  // handles DST automatically
tzset();
```

After this, `time()` and `localtime_r()` return local time with DST applied. The POSIX TZ string covers second Sunday in March → first Sunday in November.

Re-sync once per day (after each briefing prints) — the ESP32 RTC drifts but slowly.

---

## Scheduling logic

A single FreeRTOS task. Poll once per 30 seconds — cheap, and avoids the complexity of RTC alarms/deep sleep for v1.

```c
static void briefing_task(void *arg) {
    int last_printed_yday = -1;
    while (1) {
        time_t now = time(NULL);
        struct tm lt;
        localtime_r(&now, &lt);

        bool should_print =
            lt.tm_hour == CONFIG_PRINT_HOUR &&
            lt.tm_min  == CONFIG_PRINT_MINUTE &&
            lt.tm_yday != last_printed_yday;

        if (should_print) {
            briefing_run();                 // fetches + prints
            last_printed_yday = lt.tm_yday;
            time_sync_refresh();            // re-sync NTP once/day
        }
        vTaskDelay(pdMS_TO_TICKS(30 * 1000));
    }
}
```

`tm_yday` as the "last printed" key handles the date rollover cleanly (year boundary covered by the combination of yday changing and the hour/minute check).

### Boot sequence

In `app_main`:
1. `nvs_flash_init()`
2. `esp_netif_init()` + default event loop
3. Wi-Fi connect (block until got-IP event, with retry/backoff)
4. SNTP sync (block up to 15s)
5. `thermal_printer_init(...)`
6. Print a one-liner: `"booted HH:MM — next briefing at 09:00"` — confirms the thing is alive
7. `xTaskCreate(briefing_task, ...)`

Deep sleep until 9am is possible but adds RTC math, Wi-Fi reconnect, date-boundary handling. The printer needs a 2A supply anyway, so powering the C3 continuously is free. Skip deep sleep for v1.

---

## Print layout

58mm paper is ~32 characters wide at the default (small) font. Target:

```
================================
   WEDNESDAY, APRIL 22, 2026
================================

  San Carlos, CA
  62°F · Partly cloudy
  Wind: 8 mph

--------------------------------

  "The best way out is always
   through."
       — Robert Frost

================================
```

Rough call sequence:

```c
thermal_printer_reset();
thermal_printer_set_justify('C');
thermal_printer_println("================================");
thermal_printer_set_size('M');
thermal_printer_println("WEDNESDAY, APRIL 22, 2026");
thermal_printer_set_size('S');
thermal_printer_println("================================");
thermal_printer_feed(1);

thermal_printer_set_justify('L');
thermal_printer_println("  San Carlos, CA");
thermal_printer_println("  62°F · Partly cloudy");
thermal_printer_println("  Wind: 8 mph");
thermal_printer_feed(1);

thermal_printer_set_justify('C');
thermal_printer_println("--------------------------------");
thermal_printer_feed(1);

// wrap_text helper emits each line via thermal_printer_println
wrap_text(quote_body, 28, &thermal_printer_println);
thermal_printer_println("");
thermal_printer_print("       — ");
thermal_printer_println(quote_author);

thermal_printer_feed(1);
thermal_printer_println("================================");
thermal_printer_feed(3);
thermal_printer_sleep(60);
```

Write a tiny `wrap_text(const char *in, size_t width, void (*emit)(const char *line))` that word-wraps greedily. Don't rely on the printer's internal wrapping — it'll break mid-word.

Note: the MC206H's default character set is CP437 (or similar). The `°` and `—` and curly quotes in the mockup above may not render correctly. Test this early — if they come out as garbage, either swap for ASCII equivalents (`deg`, `--`, `"`) or issue the character code table command to pick a set that has them. For v1, ASCII-safe is the easy path.

---

## Error handling

- **Wi-Fi fails to connect:** retry 3× with exponential backoff. If still failing at 9am, print "No wifi — skipped briefing" so you know the thing tried.
- **Weather API fails:** print the quote anyway, with a short "weather unavailable" line.
- **Quote API fails:** print weather anyway, skip quote section.
- **Both fail:** print date + "nothing to report today" so the paper still confirms the schedule ran.
- Use `ESP_LOGI/W/E` liberally — serial logs are invaluable when the printer misbehaves.
- Never let one failure block the whole print.

---

## Testing approach

Build up in layers. Don't wire everything at once.

1. **Printer alone.** Minimal IDF app: init UART, send `ESC @` then `"hello world\n"` every 10 seconds in a task. Confirm baud, wiring, and that the power supply isn't browning out under load.
2. **Wi-Fi + NTP.** Add connect + sync. `ESP_LOGI` the local time every 30 seconds, verify DST behavior.
3. **APIs.** Add weather + quote fetchers. Log raw JSON responses, then parsed values.
4. **Layout.** Wire the full briefing function. Trigger via debug command (below).
5. **Scheduler.** Add the time-based trigger last.

### Debug trigger

Register a console command via `esp_console` (or just poll `stdin` with a short `fgetc` loop in a low-priority task). Typing `p\n` on the USB-CDC serial runs `briefing_run()` immediately. Iterating on layout without waiting until 9am is a huge quality-of-life win.

### USB-CDC vs printer UART

The XIAO C3 uses native USB-CDC for stdio — console output, `ESP_LOG*`, and stdin all go through the USB port, independent of any hardware UART. The printer lives on a separate UART (UART1 on GPIOs 21/20 per config). Don't conflate them: `printf` goes to USB, `uart_write_bytes(UART_NUM_1, ...)` goes to the printer.

Confirm this by checking `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` (or `CONFIG_ESP_CONSOLE_USB_CDC=y`) in sdkconfig — should be the default for this board.

---

## Future extensions (out of scope for v1)

> **Update:** The "ad-hoc print this messages" and "printer status feedback" items have shipped — see [README.md](./README.md), `docs/superpowers/specs/2026-04-27-hourly-message-poll-design.md`, and `docs/superpowers/specs/2026-04-27-printer-status-feedback-design.md`. The message queue uses HTTPS polling over a Fly.io-hosted Go service rather than MQTT, but the same architectural intent. The status path uses ESC/POS `DLE EOT 4` over the printer-TX → C3-GPIO20 line that's been wired since v1. The remaining items below are still genuinely future work.

- **Physical button** on a spare GPIO for on-demand affirmations / quotes
- **Deep sleep + RTC alarm** for battery operation (requires handling Wi-Fi reconnect on wake, not hard but adds code)
- **OTA updates** via `esp_https_ota` — nice once this lives somewhere inconvenient to flash by cable
- Bluesky mentions digest, xkcd of the day, SpaceTraders contract status, weather alerts, etc.

Architecturally, the cleanest path to multi-source remains: move "what should we print" decisions off the C3 entirely. The Fly.io message-queue service is a small step in that direction; future content sources can use the same shape.
