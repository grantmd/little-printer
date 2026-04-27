# Daily Briefing Firmware Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the ESP-IDF firmware described in `SPEC.md` — a XIAO ESP32-C3 driving an MC206H thermal printer that prints a daily briefing (weather + inspirational quote) at 09:00 America/Los_Angeles.

**Architecture:** Layered, additive build per SPEC.md's testing approach: skeleton + printer first, then networking, then APIs, then layout, then scheduler. Each layer ends in a hardware verification step before moving on. Public APIs are kept narrow; each `.c/.h` pair owns one responsibility (Wi-Fi, time sync, HTTP, weather, quote, briefing, etc.).

**Tech Stack:** ESP-IDF v5.x; first-party IDF components only (`esp_wifi`, `esp_netif`, `esp_event`, `nvs_flash`, `esp_http_client` + `esp_crt_bundle`, `esp_netif_sntp`, `driver/uart`, `cJSON`, FreeRTOS). No Arduino, no third-party dependencies.

**Reference docs:**
- `SPEC.md` — full project specification (read first)
- `docs/superpowers/specs/2026-04-26-mc206h-printer-migration-design.md` — context on the printer migration
- `diagnostic-firmware-spec.md` — context on what the `diag/` firmware does

**Testing approach:** Most code touches hardware (UART, Wi-Fi, network) and is verified by flashing + observing on-device. Pure logic (text wrapping, weather code lookup) is unit-tested via tiny host-side programs in `host_tests/` compiled with plain `cc`. No `unity` or full IDF unit-test framework — overkill for a hobby firmware of this size.

**Build environment note:** `idf.py` is not on PATH in the controller's shell. The user runs builds and flashing in a separate shell with the IDF export script sourced (or this controller can ask the user to invoke specific commands). Plan steps that say "build" or "flash" assume the user does this.

---

## File structure

```
little-printer/
├── CMakeLists.txt                    (NEW — top-level IDF project)
├── sdkconfig.defaults                (NEW — committed defaults)
├── .gitignore                        (MODIFY — add /build, /sdkconfig, /managed_components)
├── main/
│   ├── CMakeLists.txt                (NEW)
│   ├── Kconfig.projbuild             (NEW — Wi-Fi creds, print time, baud)
│   ├── config.h                      (NEW — pins + location constants)
│   ├── main.c                        (NEW — app_main + console trigger)
│   ├── wifi.h / wifi.c               (NEW)
│   ├── time_sync.h / time_sync.c     (NEW)
│   ├── http_fetch.h / http_fetch.c   (NEW — shared HTTPS GET → string buffer)
│   ├── weather.h / weather.c         (NEW)
│   ├── quote.h / quote.c             (NEW)
│   ├── text_wrap.h / text_wrap.c     (NEW — host-testable)
│   └── briefing.h / briefing.c       (NEW — orchestrator)
├── components/thermal_printer/
│   ├── CMakeLists.txt                (NEW)
│   ├── include/thermal_printer.h     (NEW)
│   └── thermal_printer.c             (NEW)
├── host_tests/
│   ├── README.md                     (NEW — how to run)
│   ├── test_text_wrap.c              (NEW)
│   └── test_weather_code.c           (NEW)
├── diag/                             (UNCHANGED — acceptance test)
├── SPEC.md                           (UNCHANGED)
└── ...
```

---

## Phase 1 — Skeleton + Thermal Printer

### Task 1: Project skeleton

**Files:**
- Create: `/Users/myles/dev/little-printer/CMakeLists.txt`
- Create: `/Users/myles/dev/little-printer/sdkconfig.defaults`
- Create: `/Users/myles/dev/little-printer/main/CMakeLists.txt`
- Create: `/Users/myles/dev/little-printer/main/Kconfig.projbuild`
- Create: `/Users/myles/dev/little-printer/main/config.h`
- Create: `/Users/myles/dev/little-printer/main/main.c`
- Modify: `/Users/myles/dev/little-printer/.gitignore`

- [ ] **Step 1: Top-level `CMakeLists.txt`**

Write this exact content to `/Users/myles/dev/little-printer/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(little-printer)
```

- [ ] **Step 2: `sdkconfig.defaults`**

Write this exact content to `/Users/myles/dev/little-printer/sdkconfig.defaults`:

```
CONFIG_IDF_TARGET="esp32c3"
CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y
CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y
CONFIG_FREERTOS_HZ=100
```

- [ ] **Step 3: `main/CMakeLists.txt`**

Write this exact content to `/Users/myles/dev/little-printer/main/CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS
        "main.c"
        "wifi.c"
        "time_sync.c"
        "http_fetch.c"
        "weather.c"
        "quote.c"
        "text_wrap.c"
        "briefing.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_wifi
        esp_netif
        esp_event
        nvs_flash
        esp_http_client
        esp-tls
        json
        driver
)
```

(Note: `esp_netif_sntp` lives in the `esp_netif` component in IDF v5.x; `cJSON` is in `json`. `thermal_printer` will be added to REQUIRES in Task 3, after the component itself exists.)

This file references C files we haven't created yet — that's fine; we'll create them in subsequent tasks. The build won't succeed until they exist.

- [ ] **Step 4: `main/Kconfig.projbuild`**

Write this exact content to `/Users/myles/dev/little-printer/main/Kconfig.projbuild`:

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

- [ ] **Step 5: `main/config.h`**

Write this exact content to `/Users/myles/dev/little-printer/main/config.h`:

```c
#pragma once
#include "driver/uart.h"

#define LOCATION_NAME  "San Carlos, CA"
#define LOCATION_LAT   37.5072
#define LOCATION_LON  -122.2605

#define PRINTER_UART_NUM    UART_NUM_1
#define PRINTER_TX_PIN      21    /* D6 — goes to printer RX */
#define PRINTER_RX_PIN      20    /* D7 — from printer TX */

#define PRINT_LINE_WIDTH    32    /* 58mm paper @ small font */
```

- [ ] **Step 6: Stub `main/main.c`**

Write this exact content to `/Users/myles/dev/little-printer/main/main.c`. It's a stub with empty bodies for files that don't yet exist; we'll wire them in once each component lands.

```c
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"

static const char *TAG = "main";

void app_main(void) {
    ESP_LOGI(TAG, "little-printer booting");

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_LOGI(TAG, "boot complete; idling");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(60 * 1000));
    }
}
```

- [ ] **Step 7: Create empty placeholder source files**

The `main/CMakeLists.txt` from Step 3 lists files we'll create in later tasks. Create empty stubs now so Phase 1 builds:

```bash
cd /Users/myles/dev/little-printer/main
for f in wifi.c time_sync.c http_fetch.c weather.c quote.c text_wrap.c briefing.c; do
  echo "/* placeholder — implemented in a later task */" > "$f"
done
```

- [ ] **Step 8: Update `.gitignore`**

Read `/Users/myles/dev/little-printer/.gitignore`. Add these lines (preserve any existing content):

```
# ESP-IDF top-level build artifacts
/build/
/sdkconfig
/sdkconfig.old
/dependencies.lock
/managed_components/
```

The existing `diag/` ignores stay. The result should ignore both top-level and `diag/` build outputs.

- [ ] **Step 9: Commit**

```bash
cd /Users/myles/dev/little-printer
git add CMakeLists.txt sdkconfig.defaults main/ .gitignore
git commit -m "scaffold IDF project skeleton for briefing firmware"
```

- [ ] **Step 10: User verification — does it build?**

Ask the user to run:
```bash
cd /Users/myles/dev/little-printer
idf.py set-target esp32c3
idf.py build
```

Expected: build succeeds. Warnings about empty translation units in the placeholder `.c` files are OK.

If it fails, the most common causes are:
- IDF version mismatch (need v5.x)
- A typo in `CMakeLists.txt`
- Wrong working directory

Stop and resolve before proceeding.

---

### Task 2: thermal_printer component

**Files:**
- Create: `/Users/myles/dev/little-printer/components/thermal_printer/CMakeLists.txt`
- Create: `/Users/myles/dev/little-printer/components/thermal_printer/include/thermal_printer.h`
- Create: `/Users/myles/dev/little-printer/components/thermal_printer/thermal_printer.c`

- [ ] **Step 1: Component `CMakeLists.txt`**

Write this exact content to `/Users/myles/dev/little-printer/components/thermal_printer/CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS "thermal_printer.c"
    INCLUDE_DIRS "include"
    REQUIRES driver
)
```

- [ ] **Step 2: Header file**

Write this exact content to `/Users/myles/dev/little-printer/components/thermal_printer/include/thermal_printer.h`:

```c
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "driver/uart.h"
#include "esp_err.h"

/*
 * Minimal ESC/POS driver for the MC206H (and ESC/POS-compatible thermal
 * printers). Public API is intentionally narrow: callers issue formatting
 * directives (justify/size/bold), then call print/println, plus feed and
 * sleep. All commands go out as raw UART bytes; there is no buffering
 * beyond the IDF UART driver.
 */

esp_err_t thermal_printer_init(uart_port_t uart_num,
                               int tx_pin,
                               int rx_pin,
                               int baud);

void thermal_printer_reset(void);
void thermal_printer_set_bold(bool on);
void thermal_printer_set_justify(char j);   /* 'L', 'C', 'R' */
void thermal_printer_set_size(char s);      /* 'S' normal, 'M' double-height, 'L' double both */
void thermal_printer_print(const char *text);
void thermal_printer_println(const char *text);
void thermal_printer_feed(uint8_t lines);
void thermal_printer_sleep(uint16_t seconds);
```

- [ ] **Step 3: Implementation file**

Write this exact content to `/Users/myles/dev/little-printer/components/thermal_printer/thermal_printer.c`:

```c
#include "thermal_printer.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "thermal_printer";

static uart_port_t s_uart = UART_NUM_1;

static void tx(const uint8_t *bytes, size_t len) {
    uart_write_bytes(s_uart, (const char *)bytes, len);
    uart_wait_tx_done(s_uart, pdMS_TO_TICKS(1000));
}

esp_err_t thermal_printer_init(uart_port_t uart_num,
                               int tx_pin,
                               int rx_pin,
                               int baud) {
    s_uart = uart_num;

    const uart_config_t cfg = {
        .baud_rate  = baud,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_driver_install(uart_num, 512, 0, 0, NULL, 0);
    if (err != ESP_OK) return err;
    err = uart_param_config(uart_num, &cfg);
    if (err != ESP_OK) return err;
    err = uart_set_pin(uart_num, tx_pin, rx_pin,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) return err;

    /* Let the printer settle after USB reset. */
    vTaskDelay(pdMS_TO_TICKS(2000));

    thermal_printer_reset();

    /* Heating defaults validated by diag/ acceptance on this MC206H. */
    const uint8_t esc7[] = { 0x1B, 0x37, 11, 120, 40 };
    tx(esc7, sizeof(esc7));
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_LOGI(TAG, "initialised on UART%d @ %d baud", uart_num, baud);
    return ESP_OK;
}

void thermal_printer_reset(void) {
    const uint8_t esc_at[] = { 0x1B, 0x40 };
    tx(esc_at, sizeof(esc_at));
    vTaskDelay(pdMS_TO_TICKS(100));
}

void thermal_printer_set_bold(bool on) {
    const uint8_t b[] = { 0x1B, 0x45, on ? (uint8_t)1 : (uint8_t)0 };
    tx(b, sizeof(b));
}

void thermal_printer_set_justify(char j) {
    uint8_t n;
    switch (j) {
        case 'C': case 'c': n = 1; break;
        case 'R': case 'r': n = 2; break;
        case 'L': case 'l':
        default:            n = 0; break;
    }
    const uint8_t b[] = { 0x1B, 0x61, n };
    tx(b, sizeof(b));
}

void thermal_printer_set_size(char s) {
    uint8_t n;
    switch (s) {
        case 'M': case 'm': n = 0x10; break;  /* double height */
        case 'L': case 'l': n = 0x11; break;  /* double both */
        case 'S': case 's':
        default:            n = 0x00; break;  /* normal */
    }
    const uint8_t b[] = { 0x1D, 0x21, n };
    tx(b, sizeof(b));
}

void thermal_printer_print(const char *text) {
    if (!text) return;
    tx((const uint8_t *)text, strlen(text));
}

void thermal_printer_println(const char *text) {
    if (text) {
        tx((const uint8_t *)text, strlen(text));
    }
    const uint8_t lf = 0x0A;
    tx(&lf, 1);
    /* Light pacing — at 9600 baud, 32 chars take ~33ms to clear. */
    vTaskDelay(pdMS_TO_TICKS(20));
}

void thermal_printer_feed(uint8_t lines) {
    const uint8_t b[] = { 0x1B, 0x64, lines };
    tx(b, sizeof(b));
    vTaskDelay(pdMS_TO_TICKS(50 * (lines ? lines : 1)));
}

void thermal_printer_sleep(uint16_t seconds) {
    const uint8_t b[] = { 0x1B, 0x38,
                          (uint8_t)(seconds & 0xFF),
                          (uint8_t)((seconds >> 8) & 0xFF) };
    tx(b, sizeof(b));
}
```

- [ ] **Step 4: Commit**

```bash
cd /Users/myles/dev/little-printer
git add components/thermal_printer
git commit -m "add thermal_printer component (ESC/POS over UART)"
```

---

### Task 3: Wire thermal_printer into main; hello-world hardware test

**Files:**
- Modify: `/Users/myles/dev/little-printer/main/main.c`
- Modify: `/Users/myles/dev/little-printer/main/CMakeLists.txt`

- [ ] **Step 1: Add `thermal_printer` to main's REQUIRES**

Edit `/Users/myles/dev/little-printer/main/CMakeLists.txt` and append `thermal_printer` to the `REQUIRES` list. Result:

```cmake
idf_component_register(
    SRCS
        "main.c"
        "wifi.c"
        "time_sync.c"
        "http_fetch.c"
        "weather.c"
        "quote.c"
        "text_wrap.c"
        "briefing.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_wifi
        esp_netif
        esp_event
        nvs_flash
        esp_http_client
        esp-tls
        json
        driver
        thermal_printer
)
```

- [ ] **Step 2: Replace `main/main.c` with hello-world version**

Write this exact content to `/Users/myles/dev/little-printer/main/main.c`:

```c
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"

#include "config.h"
#include "thermal_printer.h"

static const char *TAG = "main";

void app_main(void) {
    ESP_LOGI(TAG, "little-printer booting");

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(thermal_printer_init(PRINTER_UART_NUM,
                                         PRINTER_TX_PIN,
                                         PRINTER_RX_PIN,
                                         CONFIG_PRINTER_BAUD));

    thermal_printer_set_justify('C');
    thermal_printer_println("hello, world");
    thermal_printer_feed(3);

    ESP_LOGI(TAG, "boot complete; idling");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(60 * 1000));
    }
}
```

- [ ] **Step 3: Commit**

```bash
cd /Users/myles/dev/little-printer
git add main/main.c
git commit -m "main: print hello-world via thermal_printer on boot"
```

- [ ] **Step 4: User verification**

Ask the user to:
1. Run `idf.py menuconfig` and set `Briefing Printer Config → Wi-Fi SSID` and `Wi-Fi Password` (placeholder values are fine for now; this layer doesn't connect). Save and exit.
2. Run `idf.py build flash monitor`.
3. Watch the printer.

Expected: serial log shows "little-printer booting" → "thermal_printer: initialised on UART1 @ 9600 baud" → "boot complete; idling". Printer prints `hello, world` centered, then feeds 3 lines.

If "hello, world" doesn't print but the log appears: re-check wiring. If neither: see `diagnostic-firmware-spec.md` interpretation table.

---

## Phase 2 — Networking (Wi-Fi + NTP)

### Task 4: Wi-Fi connect with retry/backoff

**Files:**
- Create: `/Users/myles/dev/little-printer/main/wifi.h`
- Modify: `/Users/myles/dev/little-printer/main/wifi.c` (currently a placeholder)

- [ ] **Step 1: Header**

Write this exact content to `/Users/myles/dev/little-printer/main/wifi.h`:

```c
#pragma once

#include "esp_err.h"

/*
 * Connect to Wi-Fi using credentials from Kconfig (CONFIG_WIFI_SSID /
 * CONFIG_WIFI_PASSWORD). Blocks until either connected (returns ESP_OK)
 * or all retry attempts have been exhausted (returns ESP_FAIL).
 *
 * Caller must have already run nvs_flash_init() and esp_netif_init() and
 * created the default event loop.
 */
esp_err_t wifi_connect(void);

/*
 * True iff a connection has been established and we currently have an IP.
 */
bool wifi_is_connected(void);
```

- [ ] **Step 2: Implementation**

Write this exact content to `/Users/myles/dev/little-printer/main/wifi.c`:

```c
#include "wifi.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"

static const char *TAG = "wifi";

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1
#define WIFI_MAX_RETRIES    3

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_count = 0;
static bool s_connected = false;

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        if (s_retry_count < WIFI_MAX_RETRIES) {
            int delay_ms = 1000 << s_retry_count;  /* 1s, 2s, 4s */
            ESP_LOGW(TAG, "disconnected, retry %d/%d in %dms",
                     s_retry_count + 1, WIFI_MAX_RETRIES, delay_ms);
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
            esp_wifi_connect();
            s_retry_count++;
        } else {
            ESP_LOGE(TAG, "gave up after %d retries", WIFI_MAX_RETRIES);
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got IP " IPSTR, IP2STR(&e->ip_info.ip));
        s_retry_count = 0;
        s_connected = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

esp_err_t wifi_connect(void) {
    s_wifi_event_group = xEventGroupCreate();

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    esp_event_handler_instance_t any_id, got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &got_ip));

    wifi_config_t wifi_cfg = { 0 };
    strncpy((char *)wifi_cfg.sta.ssid, CONFIG_WIFI_SSID, sizeof(wifi_cfg.sta.ssid));
    strncpy((char *)wifi_cfg.sta.password, CONFIG_WIFI_PASSWORD, sizeof(wifi_cfg.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "connecting to '%s'...", CONFIG_WIFI_SSID);

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(30 * 1000));

    if (bits & WIFI_CONNECTED_BIT) {
        return ESP_OK;
    }
    return ESP_FAIL;
}

bool wifi_is_connected(void) {
    return s_connected;
}
```

- [ ] **Step 3: Commit**

```bash
cd /Users/myles/dev/little-printer
git add main/wifi.c main/wifi.h
git commit -m "add Wi-Fi connect with retry/backoff"
```

---

### Task 5: NTP time sync + POSIX TZ

**Files:**
- Create: `/Users/myles/dev/little-printer/main/time_sync.h`
- Modify: `/Users/myles/dev/little-printer/main/time_sync.c` (placeholder)

- [ ] **Step 1: Header**

Write this exact content to `/Users/myles/dev/little-printer/main/time_sync.h`:

```c
#pragma once

#include "esp_err.h"

/*
 * Sync time via NTP (pool.ntp.org), set the timezone to America/Los_Angeles
 * with DST handling, and block up to 15s waiting for the first sync.
 *
 * Caller must have established Wi-Fi connectivity first.
 */
esp_err_t time_sync_init(void);

/*
 * Re-trigger an NTP sync. Non-blocking. Use after each daily briefing to
 * keep the RTC drift bounded.
 */
void time_sync_refresh(void);
```

- [ ] **Step 2: Implementation**

Write this exact content to `/Users/myles/dev/little-printer/main/time_sync.c`:

```c
#include "time_sync.h"

#include <time.h>

#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"

static const char *TAG = "time_sync";

esp_err_t time_sync_init(void) {
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_err_t err = esp_netif_sntp_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "sntp_init: %s", esp_err_to_name(err));
        return err;
    }

    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(15 * 1000)) != ESP_OK) {
        ESP_LOGW(TAG, "first NTP sync timed out — continuing");
    }

    /* America/Los_Angeles with DST: starts 2nd Sun of March, ends 1st Sun of November. */
    setenv("TZ", "PST8PDT,M3.2.0,M11.1.0", 1);
    tzset();

    time_t now = time(NULL);
    struct tm lt;
    localtime_r(&now, &lt);
    ESP_LOGI(TAG, "local time: %04d-%02d-%02d %02d:%02d:%02d",
             lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday,
             lt.tm_hour, lt.tm_min, lt.tm_sec);

    return ESP_OK;
}

void time_sync_refresh(void) {
    /* Non-blocking re-sync. */
    esp_sntp_restart();
}
```

- [ ] **Step 3: Commit**

```bash
cd /Users/myles/dev/little-printer
git add main/time_sync.c main/time_sync.h
git commit -m "add NTP time sync with America/Los_Angeles DST"
```

---

### Task 6: Wire networking into main; verify on hardware

**Files:**
- Modify: `/Users/myles/dev/little-printer/main/main.c`

- [ ] **Step 1: Update `main.c` to connect Wi-Fi and sync time**

Write this exact content to `/Users/myles/dev/little-printer/main/main.c`:

```c
#include <stdio.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"

#include "config.h"
#include "thermal_printer.h"
#include "wifi.h"
#include "time_sync.h"

static const char *TAG = "main";

void app_main(void) {
    ESP_LOGI(TAG, "little-printer booting");

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    if (wifi_connect() != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi connect failed; continuing without time sync");
    } else {
        time_sync_init();
    }

    ESP_ERROR_CHECK(thermal_printer_init(PRINTER_UART_NUM,
                                         PRINTER_TX_PIN,
                                         PRINTER_RX_PIN,
                                         CONFIG_PRINTER_BAUD));

    /* Boot one-liner so the paper confirms the device is alive. */
    char line[64];
    time_t now = time(NULL);
    struct tm lt;
    localtime_r(&now, &lt);
    snprintf(line, sizeof(line), "booted %02d:%02d - briefing at %02d:%02d",
             lt.tm_hour, lt.tm_min, CONFIG_PRINT_HOUR, CONFIG_PRINT_MINUTE);
    thermal_printer_set_justify('C');
    thermal_printer_println(line);
    thermal_printer_feed(3);

    /* Log local time every 30s so we can confirm DST behavior over time. */
    while (1) {
        time_t t = time(NULL);
        struct tm tt;
        localtime_r(&t, &tt);
        ESP_LOGI(TAG, "local: %04d-%02d-%02d %02d:%02d:%02d",
                 tt.tm_year + 1900, tt.tm_mon + 1, tt.tm_mday,
                 tt.tm_hour, tt.tm_min, tt.tm_sec);
        vTaskDelay(pdMS_TO_TICKS(30 * 1000));
    }
}
```

- [ ] **Step 2: Commit**

```bash
cd /Users/myles/dev/little-printer
git add main/main.c
git commit -m "main: connect Wi-Fi, sync time, log local time every 30s"
```

- [ ] **Step 3: User verification**

User runs:
1. `idf.py menuconfig` → enter real Wi-Fi credentials. Save and exit.
2. `idf.py build flash monitor`

Expected:
- Log shows `wifi: connecting to 'X'...` then `wifi: got IP a.b.c.d`
- Log shows `time_sync: local time: 2026-04-26 HH:MM:SS` (current local time, correctly offset for PDT)
- Printer prints something like `booted 14:23 - briefing at 09:00`
- Every 30 seconds, log shows the current local time

If Wi-Fi fails: check SSID/password in menuconfig, and that the SSID is 2.4GHz (the C3 doesn't do 5GHz).

---

## Phase 3 — APIs

### Task 7: HTTP fetch helper

**Files:**
- Create: `/Users/myles/dev/little-printer/main/http_fetch.h`
- Modify: `/Users/myles/dev/little-printer/main/http_fetch.c` (placeholder)

- [ ] **Step 1: Header**

Write this exact content to `/Users/myles/dev/little-printer/main/http_fetch.h`:

```c
#pragma once

#include <stddef.h>
#include "esp_err.h"

/*
 * GET `url` over HTTPS and return the response body as a heap-allocated
 * NUL-terminated C string. Caller must free() the result on success.
 *
 * Returns ESP_OK on a 2xx response, ESP_FAIL otherwise. *out is set to
 * NULL on failure.
 */
esp_err_t http_fetch(const char *url, char **out);
```

- [ ] **Step 2: Implementation**

Write this exact content to `/Users/myles/dev/little-printer/main/http_fetch.c`:

```c
#include "http_fetch.h"

#include <stdlib.h>
#include <string.h>

#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"

static const char *TAG = "http_fetch";

#define MAX_RESPONSE_BYTES (16 * 1024)

typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
} body_t;

static esp_err_t event_handler(esp_http_client_event_t *evt) {
    body_t *body = (body_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        if (body->len + evt->data_len + 1 > body->cap) {
            size_t new_cap = body->cap ? body->cap * 2 : 1024;
            while (new_cap < body->len + evt->data_len + 1) new_cap *= 2;
            if (new_cap > MAX_RESPONSE_BYTES) {
                ESP_LOGE(TAG, "response exceeded %d bytes; aborting", MAX_RESPONSE_BYTES);
                return ESP_FAIL;
            }
            char *grown = realloc(body->buf, new_cap);
            if (!grown) return ESP_ERR_NO_MEM;
            body->buf = grown;
            body->cap = new_cap;
        }
        memcpy(body->buf + body->len, evt->data, evt->data_len);
        body->len += evt->data_len;
        body->buf[body->len] = '\0';
    }
    return ESP_OK;
}

esp_err_t http_fetch(const char *url, char **out) {
    *out = NULL;

    body_t body = { 0 };

    esp_http_client_config_t cfg = {
        .url = url,
        .event_handler = event_handler,
        .user_data = &body,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10 * 1000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        free(body.buf);
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_perform(client);
    int status = err == ESP_OK ? esp_http_client_get_status_code(client) : -1;
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status < 200 || status >= 300) {
        ESP_LOGW(TAG, "fetch failed: err=%s status=%d url=%s",
                 esp_err_to_name(err), status, url);
        free(body.buf);
        return ESP_FAIL;
    }

    if (!body.buf) {
        /* 2xx with empty body — unusual but treat as success with empty string. */
        body.buf = calloc(1, 1);
        if (!body.buf) return ESP_ERR_NO_MEM;
    }

    *out = body.buf;
    return ESP_OK;
}
```

- [ ] **Step 3: Commit**

```bash
cd /Users/myles/dev/little-printer
git add main/http_fetch.c main/http_fetch.h
git commit -m "add http_fetch helper (HTTPS GET to heap string)"
```

---

### Task 8: Weather fetcher (Open-Meteo)

**Files:**
- Create: `/Users/myles/dev/little-printer/main/weather.h`
- Modify: `/Users/myles/dev/little-printer/main/weather.c` (placeholder)
- Create: `/Users/myles/dev/little-printer/host_tests/test_weather_code.c`

- [ ] **Step 1: Header**

Write this exact content to `/Users/myles/dev/little-printer/main/weather.h`:

```c
#pragma once

#include <stdbool.h>
#include "esp_err.h"

typedef struct {
    int   temp_f;        /* whole-degree */
    int   wind_mph;
    char  description[32]; /* e.g., "Partly cloudy" */
} weather_t;

/*
 * Fetch current weather for the configured location. On ESP_OK, *out is
 * fully populated. On failure, *out is left untouched.
 */
esp_err_t weather_fetch(weather_t *out);

/*
 * Map a WMO weather code to a short human-readable string. Returns a
 * static string that lives for the lifetime of the program — do not free.
 */
const char *weather_code_to_string(int code);
```

- [ ] **Step 2: Implementation**

Write this exact content to `/Users/myles/dev/little-printer/main/weather.c`:

```c
#include "weather.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "esp_log.h"
#include "cJSON.h"

#include "config.h"
#include "http_fetch.h"

static const char *TAG = "weather";

const char *weather_code_to_string(int code) {
    switch (code) {
        case 0:                      return "Clear";
        case 1: case 2:              return "Partly cloudy";
        case 3:                      return "Overcast";
        case 45: case 48:            return "Fog";
        case 51: case 53: case 55:   return "Drizzle";
        case 61: case 63: case 65:   return "Rain";
        case 66: case 67:            return "Freezing rain";
        case 71: case 73: case 75:   return "Snow";
        case 77:                     return "Snow grains";
        case 80: case 81: case 82:   return "Rain showers";
        case 85: case 86:            return "Snow showers";
        case 95:                     return "Thunderstorm";
        case 96: case 99:            return "Thunderstorm w/ hail";
        default:                     return "Unknown";
    }
}

esp_err_t weather_fetch(weather_t *out) {
    char url[256];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast"
             "?latitude=%.4f&longitude=%.4f"
             "&current=temperature_2m,weather_code,wind_speed_10m"
             "&temperature_unit=fahrenheit&wind_speed_unit=mph"
             "&timezone=America/Los_Angeles",
             LOCATION_LAT, LOCATION_LON);

    char *body = NULL;
    if (http_fetch(url, &body) != ESP_OK) {
        return ESP_FAIL;
    }

    esp_err_t result = ESP_FAIL;
    cJSON *root = cJSON_Parse(body);
    if (!root) {
        ESP_LOGW(TAG, "JSON parse failed");
        goto done;
    }

    cJSON *current = cJSON_GetObjectItem(root, "current");
    if (!current) {
        ESP_LOGW(TAG, "no 'current' in response");
        goto done;
    }

    cJSON *temp_node = cJSON_GetObjectItem(current, "temperature_2m");
    cJSON *code_node = cJSON_GetObjectItem(current, "weather_code");
    cJSON *wind_node = cJSON_GetObjectItem(current, "wind_speed_10m");
    if (!temp_node || !code_node || !wind_node) {
        ESP_LOGW(TAG, "missing fields in 'current'");
        goto done;
    }

    out->temp_f   = (int)round(temp_node->valuedouble);
    out->wind_mph = (int)round(wind_node->valuedouble);
    const char *desc = weather_code_to_string(code_node->valueint);
    strncpy(out->description, desc, sizeof(out->description) - 1);
    out->description[sizeof(out->description) - 1] = '\0';

    ESP_LOGI(TAG, "%dF %s wind %dmph",
             out->temp_f, out->description, out->wind_mph);
    result = ESP_OK;

done:
    cJSON_Delete(root);
    free(body);
    return result;
}
```

- [ ] **Step 3: Host-side unit test for `weather_code_to_string`**

Write this exact content to `/Users/myles/dev/little-printer/host_tests/test_weather_code.c`:

```c
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

const char *weather_code_to_string(int code);

#define EXPECT(code, expected) do {                                         \
    const char *got = weather_code_to_string(code);                         \
    if (strcmp(got, expected) != 0) {                                       \
        fprintf(stderr, "FAIL: code=%d expected='%s' got='%s'\n",           \
                code, expected, got);                                       \
        exit(1);                                                            \
    }                                                                       \
} while (0)

int main(void) {
    EXPECT(0,   "Clear");
    EXPECT(1,   "Partly cloudy");
    EXPECT(3,   "Overcast");
    EXPECT(45,  "Fog");
    EXPECT(63,  "Rain");
    EXPECT(95,  "Thunderstorm");
    EXPECT(999, "Unknown");
    printf("PASS\n");
    return 0;
}
```

- [ ] **Step 4: Run host-side test**

```bash
cd /Users/myles/dev/little-printer
mkdir -p host_tests
cc -o /tmp/test_weather_code host_tests/test_weather_code.c main/weather.c \
    -I main \
    -DESP_LOGI=printf -DESP_LOGW=printf \
    -DESP_OK=0 -DESP_FAIL=-1 \
    2>&1 | head -20 || true
```

This minimal host compile will fail because `weather.c` pulls in `cJSON.h`, `esp_log.h`, etc. **That's expected.** For the weather-code test we need to extract the function under test into a separate unit, OR compile only the lookup function. Use the simpler path: copy the `weather_code_to_string` function body inline into the test, or compile-link just that function.

Replacement step — write `host_tests/test_weather_code.c` as a self-contained test that includes the function directly:

```c
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Copy of weather_code_to_string from main/weather.c — keep in sync. */
static const char *weather_code_to_string(int code) {
    switch (code) {
        case 0:                      return "Clear";
        case 1: case 2:              return "Partly cloudy";
        case 3:                      return "Overcast";
        case 45: case 48:            return "Fog";
        case 51: case 53: case 55:   return "Drizzle";
        case 61: case 63: case 65:   return "Rain";
        case 66: case 67:            return "Freezing rain";
        case 71: case 73: case 75:   return "Snow";
        case 77:                     return "Snow grains";
        case 80: case 81: case 82:   return "Rain showers";
        case 85: case 86:            return "Snow showers";
        case 95:                     return "Thunderstorm";
        case 96: case 99:            return "Thunderstorm w/ hail";
        default:                     return "Unknown";
    }
}

#define EXPECT(code, expected) do {                                         \
    const char *got = weather_code_to_string(code);                         \
    if (strcmp(got, expected) != 0) {                                       \
        fprintf(stderr, "FAIL: code=%d expected='%s' got='%s'\n",           \
                code, expected, got);                                       \
        exit(1);                                                            \
    }                                                                       \
} while (0)

int main(void) {
    EXPECT(0,   "Clear");
    EXPECT(1,   "Partly cloudy");
    EXPECT(3,   "Overcast");
    EXPECT(45,  "Fog");
    EXPECT(63,  "Rain");
    EXPECT(95,  "Thunderstorm");
    EXPECT(999, "Unknown");
    printf("PASS\n");
    return 0;
}
```

(This duplicates the lookup table. Acceptable trade-off given the table changes infrequently and the alternative is significant build-system complexity for one helper. If the table ever changes, update both copies.)

Now build and run:

```bash
cd /Users/myles/dev/little-printer
cc -Wall -o /tmp/test_weather_code host_tests/test_weather_code.c
/tmp/test_weather_code
```

Expected output: `PASS`

- [ ] **Step 5: `host_tests/README.md`**

Write this exact content to `/Users/myles/dev/little-printer/host_tests/README.md`:

```markdown
# Host-side unit tests

Tiny standalone tests for pure-logic helpers that don't touch hardware. Compile and run with plain `cc`; no IDF required.

```bash
cc -Wall -o /tmp/test_weather_code host_tests/test_weather_code.c
/tmp/test_weather_code

cc -Wall -o /tmp/test_text_wrap host_tests/test_text_wrap.c
/tmp/test_text_wrap
```

Each test prints `PASS` on success or `FAIL: ...` and exits non-zero on failure.

These tests duplicate the function under test rather than linking to `main/*.c` — the IDF build pulls in too many dependencies for a host compile. Keep the duplicates in sync when the function changes; the test suite is small enough that drift is easy to spot.
```

- [ ] **Step 6: Commit**

```bash
cd /Users/myles/dev/little-printer
git add main/weather.c main/weather.h host_tests/
git commit -m "add weather fetcher (Open-Meteo) + host-side weather_code test"
```

---

### Task 9: Quote fetcher (ZenQuotes)

**Files:**
- Create: `/Users/myles/dev/little-printer/main/quote.h`
- Modify: `/Users/myles/dev/little-printer/main/quote.c` (placeholder)

- [ ] **Step 1: Header**

Write this exact content to `/Users/myles/dev/little-printer/main/quote.h`:

```c
#pragma once

#include "esp_err.h"

typedef struct {
    char body[256];    /* the quote text */
    char author[64];
} quote_t;

/*
 * Fetch a random inspirational quote from ZenQuotes. On ESP_OK, *out is
 * fully populated. On failure, *out is left untouched.
 */
esp_err_t quote_fetch(quote_t *out);
```

- [ ] **Step 2: Implementation**

Write this exact content to `/Users/myles/dev/little-printer/main/quote.c`:

```c
#include "quote.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "cJSON.h"

#include "http_fetch.h"

static const char *TAG = "quote";

esp_err_t quote_fetch(quote_t *out) {
    char *body = NULL;
    if (http_fetch("https://zenquotes.io/api/random", &body) != ESP_OK) {
        return ESP_FAIL;
    }

    esp_err_t result = ESP_FAIL;
    cJSON *root = cJSON_Parse(body);
    if (!root || !cJSON_IsArray(root)) {
        ESP_LOGW(TAG, "expected top-level JSON array");
        goto done;
    }

    cJSON *first = cJSON_GetArrayItem(root, 0);
    if (!first) {
        ESP_LOGW(TAG, "empty array");
        goto done;
    }

    cJSON *q = cJSON_GetObjectItem(first, "q");
    cJSON *a = cJSON_GetObjectItem(first, "a");
    if (!q || !a || !cJSON_IsString(q) || !cJSON_IsString(a)) {
        ESP_LOGW(TAG, "missing 'q' or 'a'");
        goto done;
    }

    strncpy(out->body, q->valuestring, sizeof(out->body) - 1);
    out->body[sizeof(out->body) - 1] = '\0';
    strncpy(out->author, a->valuestring, sizeof(out->author) - 1);
    out->author[sizeof(out->author) - 1] = '\0';

    ESP_LOGI(TAG, "fetched quote by %s", out->author);
    result = ESP_OK;

done:
    cJSON_Delete(root);
    free(body);
    return result;
}
```

- [ ] **Step 3: Commit**

```bash
cd /Users/myles/dev/little-printer
git add main/quote.c main/quote.h
git commit -m "add quote fetcher (ZenQuotes)"
```

---

### Task 10: Wire APIs into main; verify on hardware

**Files:**
- Modify: `/Users/myles/dev/little-printer/main/main.c`

- [ ] **Step 1: Replace `main.c` with API-test version**

Write this exact content to `/Users/myles/dev/little-printer/main/main.c`:

```c
#include <stdio.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"

#include "config.h"
#include "thermal_printer.h"
#include "wifi.h"
#include "time_sync.h"
#include "weather.h"
#include "quote.h"

static const char *TAG = "main";

void app_main(void) {
    ESP_LOGI(TAG, "little-printer booting");

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    if (wifi_connect() != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi connect failed; halting");
        while (1) vTaskDelay(pdMS_TO_TICKS(60 * 1000));
    }
    time_sync_init();

    ESP_ERROR_CHECK(thermal_printer_init(PRINTER_UART_NUM,
                                         PRINTER_TX_PIN,
                                         PRINTER_RX_PIN,
                                         CONFIG_PRINTER_BAUD));

    /* Fetch + log both APIs once. */
    weather_t w;
    if (weather_fetch(&w) == ESP_OK) {
        ESP_LOGI(TAG, "WEATHER: %dF, %s, wind %dmph",
                 w.temp_f, w.description, w.wind_mph);
    } else {
        ESP_LOGW(TAG, "WEATHER: fetch failed");
    }

    quote_t q;
    if (quote_fetch(&q) == ESP_OK) {
        ESP_LOGI(TAG, "QUOTE: \"%s\" — %s", q.body, q.author);
    } else {
        ESP_LOGW(TAG, "QUOTE: fetch failed");
    }

    ESP_LOGI(TAG, "API test complete; idling");
    while (1) vTaskDelay(pdMS_TO_TICKS(60 * 1000));
}
```

- [ ] **Step 2: Commit**

```bash
cd /Users/myles/dev/little-printer
git add main/main.c
git commit -m "main: smoke-test weather + quote APIs on boot"
```

- [ ] **Step 3: User verification**

User: `idf.py build flash monitor`

Expected log:
```
I (xxx) main: little-printer booting
I (xxx) wifi: got IP a.b.c.d
I (xxx) time_sync: local time: ...
I (xxx) thermal_printer: initialised on UART1 @ 9600 baud
I (xxx) weather: 62F Partly cloudy wind 8mph
I (xxx) main: WEATHER: 62F, Partly cloudy, wind 8mph
I (xxx) quote: fetched quote by Robert Frost
I (xxx) main: QUOTE: "The best way out is always through." — Robert Frost
```

If a fetch fails: check the URL in `weather.c` / `quote.c`, check that `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y` is in `sdkconfig.defaults` (it is, from Task 1), and that the network has DNS + outbound HTTPS.

---

## Phase 4 — Briefing layout

### Task 11: Text wrapping helper

**Files:**
- Create: `/Users/myles/dev/little-printer/main/text_wrap.h`
- Modify: `/Users/myles/dev/little-printer/main/text_wrap.c` (placeholder)
- Create: `/Users/myles/dev/little-printer/host_tests/test_text_wrap.c`

- [ ] **Step 1: Header**

Write this exact content to `/Users/myles/dev/little-printer/main/text_wrap.h`:

```c
#pragma once

#include <stddef.h>

typedef void (*text_wrap_emit_fn)(const char *line);

/*
 * Greedy word-wrap `in` to `width` columns, calling `emit` once per output
 * line. Whitespace runs are collapsed to a single space; words longer than
 * `width` are emitted on their own line (will overflow the line, by design).
 * `in` must be NUL-terminated.
 */
void text_wrap(const char *in, size_t width, text_wrap_emit_fn emit);
```

- [ ] **Step 2: Write the failing host-side test first (TDD)**

Write this exact content to `/Users/myles/dev/little-printer/host_tests/test_text_wrap.c`:

```c
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "../main/text_wrap.h"

#define MAX_LINES 32
static char captured[MAX_LINES][128];
static int captured_count;

static void capture(const char *line) {
    if (captured_count < MAX_LINES) {
        strncpy(captured[captured_count], line, sizeof(captured[0]) - 1);
        captured[captured_count][sizeof(captured[0]) - 1] = '\0';
        captured_count++;
    }
}

static void reset(void) { captured_count = 0; }

#define ASSERT_LINES(expected_count) do {                                   \
    if (captured_count != (expected_count)) {                               \
        fprintf(stderr, "FAIL %s: expected %d lines, got %d\n",             \
                __func__, (expected_count), captured_count);                \
        for (int i = 0; i < captured_count; i++) {                          \
            fprintf(stderr, "  [%d] '%s'\n", i, captured[i]);               \
        }                                                                   \
        exit(1);                                                            \
    }                                                                       \
} while (0)

#define ASSERT_LINE(idx, expected) do {                                     \
    if (strcmp(captured[idx], (expected)) != 0) {                           \
        fprintf(stderr, "FAIL %s: line %d expected '%s' got '%s'\n",        \
                __func__, (idx), (expected), captured[idx]);                \
        exit(1);                                                            \
    }                                                                       \
} while (0)

static void test_short_input(void) {
    reset();
    text_wrap("hello world", 32, capture);
    ASSERT_LINES(1);
    ASSERT_LINE(0, "hello world");
}

static void test_wraps_at_word_boundary(void) {
    reset();
    text_wrap("the quick brown fox", 10, capture);
    /* Expected greedy wrap: "the quick" (9 chars), "brown fox" (9 chars). */
    ASSERT_LINES(2);
    ASSERT_LINE(0, "the quick");
    ASSERT_LINE(1, "brown fox");
}

static void test_collapses_whitespace(void) {
    reset();
    text_wrap("hello   world", 32, capture);
    ASSERT_LINES(1);
    ASSERT_LINE(0, "hello world");
}

static void test_word_longer_than_width(void) {
    reset();
    text_wrap("supercalifragilisticexpialidocious me", 10, capture);
    /* Long word emitted on its own line, then "me" on the next. */
    ASSERT_LINES(2);
    ASSERT_LINE(0, "supercalifragilisticexpialidocious");
    ASSERT_LINE(1, "me");
}

static void test_empty_input(void) {
    reset();
    text_wrap("", 32, capture);
    ASSERT_LINES(0);
}

int main(void) {
    test_short_input();
    test_wraps_at_word_boundary();
    test_collapses_whitespace();
    test_word_longer_than_width();
    test_empty_input();
    printf("PASS\n");
    return 0;
}
```

- [ ] **Step 3: Try to build the test — confirm it fails**

```bash
cd /Users/myles/dev/little-printer
cc -Wall -o /tmp/test_text_wrap host_tests/test_text_wrap.c main/text_wrap.c
```

Expected: link error — `text_wrap` is undefined (text_wrap.c is still the placeholder).

- [ ] **Step 4: Implement `text_wrap.c`**

Write this exact content to `/Users/myles/dev/little-printer/main/text_wrap.c`:

```c
#include "text_wrap.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>

void text_wrap(const char *in, size_t width, text_wrap_emit_fn emit) {
    if (!in || !emit || width == 0) return;

    char buf[256];
    size_t buf_len = 0;

    const char *p = in;
    while (*p) {
        /* Skip whitespace. */
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;

        /* Collect a word. */
        const char *word_start = p;
        while (*p && !isspace((unsigned char)*p)) p++;
        size_t word_len = (size_t)(p - word_start);
        if (word_len >= sizeof(buf)) word_len = sizeof(buf) - 1;

        /* Decide whether to emit current line and start fresh. */
        if (buf_len == 0) {
            memcpy(buf, word_start, word_len);
            buf_len = word_len;
        } else if (buf_len + 1 + word_len <= width) {
            buf[buf_len++] = ' ';
            memcpy(buf + buf_len, word_start, word_len);
            buf_len += word_len;
        } else {
            buf[buf_len] = '\0';
            emit(buf);
            memcpy(buf, word_start, word_len);
            buf_len = word_len;
        }
    }

    if (buf_len > 0) {
        buf[buf_len] = '\0';
        emit(buf);
    }
}
```

- [ ] **Step 5: Build and run the test**

```bash
cd /Users/myles/dev/little-printer
cc -Wall -o /tmp/test_text_wrap host_tests/test_text_wrap.c main/text_wrap.c
/tmp/test_text_wrap
```

Expected output: `PASS`

If a test fails, the failure message identifies which case and what the diff was. Fix and rerun.

- [ ] **Step 6: Commit**

```bash
cd /Users/myles/dev/little-printer
git add main/text_wrap.c main/text_wrap.h host_tests/test_text_wrap.c
git commit -m "add text_wrap helper with host-side tests"
```

---

### Task 12: Briefing orchestrator

**Files:**
- Create: `/Users/myles/dev/little-printer/main/briefing.h`
- Modify: `/Users/myles/dev/little-printer/main/briefing.c` (placeholder)

- [ ] **Step 1: Header**

Write this exact content to `/Users/myles/dev/little-printer/main/briefing.h`:

```c
#pragma once

/*
 * Fetch weather + quote, format the briefing, and emit it to the thermal
 * printer. Never throws; on individual fetch failures, prints a degraded
 * version (see SPEC.md error-handling section). The thermal_printer must
 * already be initialised.
 */
void briefing_run(void);
```

- [ ] **Step 2: Implementation**

Write this exact content to `/Users/myles/dev/little-printer/main/briefing.c`:

```c
#include "briefing.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#include "esp_log.h"

#include "config.h"
#include "thermal_printer.h"
#include "weather.h"
#include "quote.h"
#include "text_wrap.h"

static const char *TAG = "briefing";

static void println_indented(const char *line) {
    char buf[80];
    snprintf(buf, sizeof(buf), "  %s", line);
    thermal_printer_println(buf);
}

static void format_date(char *out, size_t out_size) {
    time_t now = time(NULL);
    struct tm lt;
    localtime_r(&now, &lt);
    /* "WEDNESDAY, APRIL 22, 2026" — uppercase weekday + month. */
    strftime(out, out_size, "%A, %B %-d, %Y", &lt);
    for (char *p = out; *p; p++) *p = (char)toupper((unsigned char)*p);
}

void briefing_run(void) {
    ESP_LOGI(TAG, "briefing_run starting");

    weather_t w;
    bool have_weather = (weather_fetch(&w) == ESP_OK);

    quote_t q;
    bool have_quote = (quote_fetch(&q) == ESP_OK);

    char date_line[48];
    format_date(date_line, sizeof(date_line));

    thermal_printer_reset();
    thermal_printer_set_justify('C');
    thermal_printer_println("================================");
    thermal_printer_set_size('M');
    thermal_printer_println(date_line);
    thermal_printer_set_size('S');
    thermal_printer_println("================================");
    thermal_printer_feed(1);

    /* Weather block (or degraded line). */
    thermal_printer_set_justify('L');
    if (have_weather) {
        char line[64];
        println_indented(LOCATION_NAME);
        snprintf(line, sizeof(line), "%dF, %s", w.temp_f, w.description);
        println_indented(line);
        snprintf(line, sizeof(line), "Wind: %d mph", w.wind_mph);
        println_indented(line);
    } else {
        println_indented("weather unavailable");
    }
    thermal_printer_feed(1);

    thermal_printer_set_justify('C');
    thermal_printer_println("--------------------------------");
    thermal_printer_feed(1);

    /* Quote block (or degraded line, or skip if both fail). */
    thermal_printer_set_justify('L');
    if (have_quote) {
        char wrap_in[320];
        snprintf(wrap_in, sizeof(wrap_in), "\"%s\"", q.body);
        text_wrap(wrap_in, PRINT_LINE_WIDTH - 4, &println_indented);
        thermal_printer_println("");
        char attribution[80];
        snprintf(attribution, sizeof(attribution), "       -- %s", q.author);
        thermal_printer_println(attribution);
    } else if (!have_weather) {
        /* Both APIs failed — confirm the schedule still ran. */
        println_indented("nothing to report today");
    }

    thermal_printer_feed(1);
    thermal_printer_set_justify('C');
    thermal_printer_println("================================");
    thermal_printer_feed(3);
    thermal_printer_sleep(60);

    ESP_LOGI(TAG, "briefing_run done");
}
```

- [ ] **Step 3: Commit**

```bash
cd /Users/myles/dev/little-printer
git add main/briefing.c main/briefing.h
git commit -m "add briefing orchestrator (fetch + format + print)"
```

---

### Task 13: Debug console trigger; verify full briefing on hardware

**Files:**
- Modify: `/Users/myles/dev/little-printer/main/main.c`

- [ ] **Step 1: Replace `main.c` with debug-trigger version**

Write this exact content to `/Users/myles/dev/little-printer/main/main.c`:

```c
#include <stdio.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"

#include "config.h"
#include "thermal_printer.h"
#include "wifi.h"
#include "time_sync.h"
#include "briefing.h"

static const char *TAG = "main";

static void console_task(void *arg) {
    while (1) {
        int c = fgetc(stdin);
        if (c == 'p' || c == 'P') {
            ESP_LOGI(TAG, "console: manual briefing trigger");
            briefing_run();
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "little-printer booting");

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    if (wifi_connect() != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi connect failed; continuing offline");
    } else {
        time_sync_init();
    }

    ESP_ERROR_CHECK(thermal_printer_init(PRINTER_UART_NUM,
                                         PRINTER_TX_PIN,
                                         PRINTER_RX_PIN,
                                         CONFIG_PRINTER_BAUD));

    char boot_line[64];
    time_t now = time(NULL);
    struct tm lt;
    localtime_r(&now, &lt);
    snprintf(boot_line, sizeof(boot_line),
             "booted %02d:%02d - briefing at %02d:%02d",
             lt.tm_hour, lt.tm_min, CONFIG_PRINT_HOUR, CONFIG_PRINT_MINUTE);
    thermal_printer_set_justify('C');
    thermal_printer_println(boot_line);
    thermal_printer_feed(2);

    xTaskCreate(console_task, "console", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "ready — type 'p' on the serial console for a manual briefing");
    while (1) vTaskDelay(pdMS_TO_TICKS(60 * 1000));
}
```

- [ ] **Step 2: Commit**

```bash
cd /Users/myles/dev/little-printer
git add main/main.c
git commit -m "main: console 'p' trigger for manual briefing"
```

- [ ] **Step 3: User verification**

User: `idf.py build flash monitor`

After boot completes:
- Type `p` and Enter in the monitor.
- Expected: full briefing prints — date header, weather block, divider, quote block, footer.

Validate against the SPEC.md mockup:
- Date is in uppercase ("WEDNESDAY, APRIL 22, 2026" style).
- Location, temp/conditions, wind appear in the weather block.
- Quote wraps cleanly without breaking words.
- Author line is right-padded with the `-- author` style.

If text overruns the 32-char width or breaks oddly, adjust `text_wrap`'s width argument or the indentation in `briefing.c`.

If special chars (curly quotes, em-dash, °) come out as garbage, they came from ZenQuotes' response — strip or substitute them in `quote.c`. SPEC.md notes ASCII-safe is the easy path.

---

## Phase 5 — Scheduler

### Task 14: Time-based scheduler; final integration

**Files:**
- Modify: `/Users/myles/dev/little-printer/main/main.c`

- [ ] **Step 1: Add the scheduler task**

Write this exact content to `/Users/myles/dev/little-printer/main/main.c`:

```c
#include <stdio.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"

#include "config.h"
#include "thermal_printer.h"
#include "wifi.h"
#include "time_sync.h"
#include "briefing.h"

static const char *TAG = "main";

static void console_task(void *arg) {
    while (1) {
        int c = fgetc(stdin);
        if (c == 'p' || c == 'P') {
            ESP_LOGI(TAG, "console: manual briefing trigger");
            briefing_run();
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

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
            ESP_LOGI(TAG, "scheduled briefing trigger");
            briefing_run();
            last_printed_yday = lt.tm_yday;
            time_sync_refresh();
        }

        vTaskDelay(pdMS_TO_TICKS(30 * 1000));
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "little-printer booting");

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    if (wifi_connect() != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi connect failed; continuing offline");
    } else {
        time_sync_init();
    }

    ESP_ERROR_CHECK(thermal_printer_init(PRINTER_UART_NUM,
                                         PRINTER_TX_PIN,
                                         PRINTER_RX_PIN,
                                         CONFIG_PRINTER_BAUD));

    char boot_line[64];
    time_t now = time(NULL);
    struct tm lt;
    localtime_r(&now, &lt);
    snprintf(boot_line, sizeof(boot_line),
             "booted %02d:%02d - briefing at %02d:%02d",
             lt.tm_hour, lt.tm_min, CONFIG_PRINT_HOUR, CONFIG_PRINT_MINUTE);
    thermal_printer_set_justify('C');
    thermal_printer_println(boot_line);
    thermal_printer_feed(2);

    xTaskCreate(console_task, "console", 4096, NULL, 5, NULL);
    xTaskCreate(briefing_task, "briefing", 8192, NULL, 4, NULL);

    ESP_LOGI(TAG, "ready");
    while (1) vTaskDelay(pdMS_TO_TICKS(60 * 1000));
}
```

- [ ] **Step 2: Commit**

```bash
cd /Users/myles/dev/little-printer
git add main/main.c
git commit -m "main: add scheduled briefing task (polls every 30s)"
```

- [ ] **Step 3: User verification — final integration**

User: `idf.py build flash monitor`

Three checks:

1. **Manual trigger still works:** type `p` on the console; full briefing prints.
2. **Scheduled trigger works:** in `idf.py menuconfig`, set `Hour to print briefing` and `Minute to print briefing` to ~2 minutes in the future. Save, rebuild, flash. Wait. The briefing should print automatically at the configured time, with `scheduled briefing trigger` in the log.
3. **De-duplication works:** after the scheduled print, the device shouldn't reprint at the same hour/minute. Verify by leaving the monitor running for a few minutes past the print time — only one briefing per hour:minute per day.

After verification, set `PRINT_HOUR=9, PRINT_MINUTE=0` (or whatever the desired daily time is), rebuild, flash, and unplug from the dev machine. Plug into a permanent USB-C supply.

---

## Self-review notes

**Spec coverage:**

| SPEC.md section | Covered by |
|---|---|
| Hardware / Wiring | (handled by migration plan; unchanged here) |
| Software stack | Tasks 1, 4–14 use the components SPEC.md lists |
| Project structure | Task 1 sets up the layout; later tasks fill it |
| Configuration (Kconfig + config.h) | Task 1 (Steps 4–5) |
| Thermal printer component | Tasks 2, 3 |
| Weather API (Open-Meteo) | Task 8 |
| Quote API (ZenQuotes) | Task 9 |
| HTTP client pattern | Task 7 |
| JSON parsing | Tasks 8, 9 use cJSON per spec |
| Time handling (SNTP + TZ) | Task 5 |
| Scheduling logic | Task 14 |
| Boot sequence | Task 14's `app_main` matches SPEC.md's enumeration |
| Print layout | Task 12 |
| Error handling | Task 12 (degraded paths in `briefing_run`) |
| Testing approach (layered) | The 5 phases mirror SPEC.md's 5 layers |
| Debug trigger | Task 13 |
| USB-CDC vs printer UART | sdkconfig.defaults sets `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` (Task 1) |

**Placeholder scan:** none of the steps use "TBD", "TODO", "appropriate", or vague placeholder language. Each step contains literal code, exact paths, or precise commands.

**Identifier consistency:**
- `weather_t` and `quote_t` types defined in their respective headers and used as defined.
- `text_wrap_emit_fn` matches between header and test.
- Function names match across calls (`briefing_run`, `weather_fetch`, `quote_fetch`, `time_sync_refresh`).
- `PRINTER_UART_NUM`, `PRINTER_TX_PIN`, `PRINTER_RX_PIN`, `PRINT_LINE_WIDTH` all defined in `config.h` (Task 1) and used consistently.
- `CONFIG_PRINTER_BAUD`, `CONFIG_WIFI_SSID`, `CONFIG_WIFI_PASSWORD`, `CONFIG_PRINT_HOUR`, `CONFIG_PRINT_MINUTE` all match the Kconfig in Task 1 Step 4.

**Out of scope (intentional, per SPEC.md):**
- MQTT subscriber / public webhook
- Physical button trigger
- Deep sleep + RTC alarm
- OTA updates
- Bluesky / xkcd / SpaceTraders / weather alert sources
- Backwards-compat shims, feature flags, etc.

**Known small risks:**
- ZenQuotes occasionally returns curly quotes / em-dashes that may print as garbage on the MC206H's CP437 charset (SPEC.md flags this). Task 13 verification calls it out; if it's a problem, a 5-line ASCII-fold function in `quote.c` solves it.
- `briefing_task` polls every 30s; in the worst case, a briefing fires up to 30s after the configured minute boundary. Acceptable for a 09:00 daily briefing.
- Console `fgetc` in a polled task with a 100ms `vTaskDelay` uses ~1% CPU and is harmless. If stdin EOFs (e.g., when not connected to monitor), `fgetc` returns -1 immediately and we busy-loop at 100ms intervals — fine.
