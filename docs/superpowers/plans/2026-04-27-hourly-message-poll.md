# Hourly Message Poll Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace v1.1's daily-briefing message block with an independent hourly-during-waking-hours poll on the C3, drop the queue cap to 3, and serialise concurrent printer access between the briefing and message tasks.

**Architecture:** Two FreeRTOS scheduling tasks (`briefing_task`, `messages_task`) poll independently and share a binary mutex (`s_print_mutex`) around any high-level print operation. The Fly.io service stays mostly unchanged — only the queue-cap constant moves. The C3 firmware loses the v1.1 messages block in `briefing.c`, gains a new `messages_print_pending()` helper in the existing `messages` component, a new `messages_task` in `main.c` with a `m` console trigger, two Kconfig entries for the waking-hours window, and a tiny `printer_lock` shared module.

**Tech Stack:** Go stdlib (`net/http`, `database/sql`); ESP-IDF v5.x (`driver/uart`, FreeRTOS, `esp_http_client`, cJSON).

**Reference docs:**
- Spec: `docs/superpowers/specs/2026-04-27-hourly-message-poll-design.md`
- v1.1 baseline: `docs/superpowers/plans/2026-04-27-public-message-queue.md`

---

## File structure (after the plan completes)

```
little-printer/
├── fly-message-queue/
│   ├── validate.go                              (MODIFY — maxQueueSize 5 → 3)
│   ├── handlers_test.go                         (MODIFY — replace hardcoded 5 with maxQueueSize)
│   └── ...
├── main/
│   ├── Kconfig.projbuild                        (MODIFY — add 2 hour entries)
│   ├── printer_lock.h                           (NEW)
│   ├── printer_lock.c                           (NEW)
│   ├── briefing.c                               (MODIFY — strip messages block, take mutex)
│   ├── messages.h                               (MODIFY — add messages_print_pending)
│   ├── messages.c                               (MODIFY — add messages_print_pending impl)
│   ├── main.c                                   (MODIFY — add messages_task, m trigger, init mutex)
│   └── CMakeLists.txt                           (MODIFY — add printer_lock.c)
└── ...
```

---

## Phase A — Fly.io app

### Task A1: Drop queue cap from 5 to 3

**Files:**
- Modify: `/Users/myles/dev/little-printer/fly-message-queue/validate.go`
- Modify: `/Users/myles/dev/little-printer/fly-message-queue/handlers_test.go`

The existing test `TestSubmitRejectsWhenQueueFull` hard-codes a `for i := 0; i < 5; i++` loop and a "6th submission" comment. With `maxQueueSize = 3`, the test would still PASS (the 6th submission still gets 429 because the queue saturates after 3) but the comment would be misleading. Refactor the test to use the constant so it stays accurate as the cap evolves.

- [ ] **Step 1: Update the test to use `maxQueueSize`**

In `/Users/myles/dev/little-printer/fly-message-queue/handlers_test.go`, find:

```go
func TestSubmitRejectsWhenQueueFull(t *testing.T) {
	srv := newTestServer(t)
	for i := 0; i < 5; i++ {
		form := strings.NewReader("sender=alice&message=hello")
		resp, _ := http.Post(srv.URL+"/submit", "application/x-www-form-urlencoded", form)
		resp.Body.Close()
	}
	// 6th submission should be rejected.
	form := strings.NewReader("sender=alice&message=hello")
	resp, err := http.Post(srv.URL+"/submit", "application/x-www-form-urlencoded", form)
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != 429 {
		t.Errorf("want 429, got %d", resp.StatusCode)
	}
}
```

Replace with:

```go
func TestSubmitRejectsWhenQueueFull(t *testing.T) {
	srv := newTestServer(t)
	for i := 0; i < maxQueueSize; i++ {
		form := strings.NewReader("sender=alice&message=hello")
		resp, _ := http.Post(srv.URL+"/submit", "application/x-www-form-urlencoded", form)
		resp.Body.Close()
	}
	// One past the cap should be rejected.
	form := strings.NewReader("sender=alice&message=hello")
	resp, err := http.Post(srv.URL+"/submit", "application/x-www-form-urlencoded", form)
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != 429 {
		t.Errorf("want 429, got %d", resp.StatusCode)
	}
}
```

- [ ] **Step 2: Run tests, expect PASS (the test still passes against `maxQueueSize=5` — that's fine, the change is purely cosmetic)**

```bash
cd /Users/myles/dev/little-printer/fly-message-queue
go test ./...
```

Expected: `ok ... PASS`. All 14 tests still pass.

- [ ] **Step 3: Drop the cap to 3 in `validate.go`**

In `/Users/myles/dev/little-printer/fly-message-queue/validate.go`, find:

```go
const (
	maxSenderLen  = 24
	maxMessageLen = 280
	maxQueueSize  = 5
)
```

Replace with:

```go
const (
	maxSenderLen  = 24
	maxMessageLen = 280
	maxQueueSize  = 3
)
```

- [ ] **Step 4: Run tests, expect PASS**

```bash
go test ./...
```

Expected: still all PASS — the test loops `maxQueueSize` times and verifies the next submission is 429. Whether the cap is 3 or 5, the assertion holds.

- [ ] **Step 5: Commit**

```bash
cd /Users/myles/dev/little-printer
git add fly-message-queue/validate.go fly-message-queue/handlers_test.go
git commit -m "fly-message-queue: drop queue cap from 5 to 3"
```

- [ ] **Step 6: Deploy — DEFERRED to user**

```bash
cd /Users/myles/dev/little-printer/fly-message-queue
fly deploy
```

(Optional but recommended: redeploy now so the cap is in effect before the firmware changes land. Either order works — they're loosely coupled. The firmware will keep working with the old 5-cap until this deploy lands.)

---

## Phase B — Firmware

### Task B1: Add Kconfig entries for waking hours

**Files:**
- Modify: `/Users/myles/dev/little-printer/main/Kconfig.projbuild`

- [ ] **Step 1: Insert two new entries before `endmenu`**

Read `/Users/myles/dev/little-printer/main/Kconfig.projbuild` to confirm its content. The file ends with `endmenu`.

Use `Edit` to replace:

```
endmenu
```

With:

```
config MESSAGES_START_HOUR
    int "Start of messages waking-hours window (inclusive, local time)"
    default 8
    range 0 23
    help
      The hourly message poll only fires when the local hour is in the
      half-open range [START_HOUR, END_HOUR).

config MESSAGES_END_HOUR
    int "End of messages waking-hours window (exclusive, local time)"
    default 22
    range 1 24

endmenu
```

- [ ] **Step 2: Commit**

```bash
cd /Users/myles/dev/little-printer
git add main/Kconfig.projbuild
git commit -m "kconfig: add MESSAGES_START_HOUR and MESSAGES_END_HOUR"
```

---

### Task B2: New `printer_lock` shared module

**Files:**
- Create: `/Users/myles/dev/little-printer/main/printer_lock.h`
- Create: `/Users/myles/dev/little-printer/main/printer_lock.c`
- Modify: `/Users/myles/dev/little-printer/main/CMakeLists.txt`

The mutex serialises high-level print operations (`briefing_run` and `messages_print_pending`) so they don't interleave UART writes when both fire in the same minute. FreeRTOS mutex (not binary semaphore) is the right primitive: it starts in the "available" state and provides priority inheritance.

- [ ] **Step 1: Header**

Write this exact content to `/Users/myles/dev/little-printer/main/printer_lock.h`:

```c
#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/*
 * A single FreeRTOS mutex serialising access to the thermal printer.
 * High-level print operations (briefing_run, messages_print_pending)
 * acquire this around any sequence of thermal_printer_* calls. Without
 * it, two scheduled tasks firing in the same minute could interleave
 * UART bytes and produce garbled output.
 *
 * printer_lock_init() must be called once during boot, before any task
 * that prints starts.
 */

extern SemaphoreHandle_t s_print_mutex;

void printer_lock_init(void);
```

- [ ] **Step 2: Implementation**

Write this exact content to `/Users/myles/dev/little-printer/main/printer_lock.c`:

```c
#include "printer_lock.h"

#include "esp_log.h"

static const char *TAG = "printer_lock";

SemaphoreHandle_t s_print_mutex = NULL;

void printer_lock_init(void) {
    s_print_mutex = xSemaphoreCreateMutex();
    if (!s_print_mutex) {
        ESP_LOGE(TAG, "failed to create printer mutex");
    }
}
```

- [ ] **Step 3: Update main/CMakeLists.txt to include printer_lock.c**

Read `/Users/myles/dev/little-printer/main/CMakeLists.txt`. The `SRCS` list currently ends with `messages.c`. Use `Edit` to replace:

```
        "messages.c"
    INCLUDE_DIRS "."
```

With:

```
        "messages.c"
        "printer_lock.c"
    INCLUDE_DIRS "."
```

- [ ] **Step 4: Commit**

```bash
cd /Users/myles/dev/little-printer
git add main/printer_lock.c main/printer_lock.h main/CMakeLists.txt
git commit -m "main: add printer_lock shared mutex module"
```

---

### Task B3: Strip messages block from briefing.c, take mutex around briefing_run body

**Files:**
- Modify: `/Users/myles/dev/little-printer/main/briefing.c`

The current `briefing_run` (post-v1.1) ends with a conditional MESSAGES block that fetches pending messages, prints them, and confirms. We strip all of that and instead acquire `s_print_mutex` around the briefing body. After this task, `briefing.c` returns to v1's behavior plus mutex locking.

- [ ] **Step 1: Remove the messages-related includes**

In `/Users/myles/dev/little-printer/main/briefing.c`, find:

```c
#include "text_wrap.h"
#include "messages.h"
```

Replace with:

```c
#include "text_wrap.h"

#include "printer_lock.h"
```

(The `messages.h` include goes away; `printer_lock.h` is added because the mutex is used below.)

- [ ] **Step 2: Drop the unused `<stdlib.h>` include**

After the messages block is gone, `free()` is no longer called in this file. Find:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
```

Replace with:

```c
#include <stdio.h>
#include <string.h>
```

- [ ] **Step 3: Replace the function body to lock the mutex and remove the messages block**

Find the entirety of `briefing_run` (from the line `void briefing_run(void) {` to the closing `}` after `ESP_LOGI(TAG, "briefing_run done");`):

```c
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

    /* Weather sprite, centered above the weather text. */
    if (have_weather) {
        const uint8_t *sprite = weather_sprite_for_code(w.weather_code);
        if (sprite) {
            thermal_printer_set_justify('C');
            thermal_printer_print_bitmap(3, 24, sprite);
            thermal_printer_feed(1);
        }
    }

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

    /* Optional messages block — only if the queue has anything pending. */
    message_t *msgs = NULL;
    size_t n = 0;
    if (messages_fetch_pending(&msgs, &n) == ESP_OK && n > 0) {
        thermal_printer_feed(2);
        thermal_printer_set_justify('C');
        thermal_printer_println("----- MESSAGES -----");
        thermal_printer_feed(1);

        thermal_printer_set_justify('L');
        int printed_ids[8];
        size_t to_confirm = n > 8 ? 8 : n;
        for (size_t i = 0; i < to_confirm; i++) {
            text_wrap(msgs[i].message, PRINT_LINE_WIDTH - 4, &println_indented);
            char attribution[48];
            snprintf(attribution, sizeof(attribution), "       -- %s", msgs[i].sender);
            thermal_printer_println(attribution);
            thermal_printer_feed(1);
            printed_ids[i] = msgs[i].id;
        }

        thermal_printer_set_justify('C');
        thermal_printer_println("================================");

        messages_confirm(printed_ids, to_confirm);
    }
    free(msgs);

    thermal_printer_feed(3);
    thermal_printer_sleep(60);

    ESP_LOGI(TAG, "briefing_run done");
}
```

Replace with:

```c
void briefing_run(void) {
    ESP_LOGI(TAG, "briefing_run starting");

    weather_t w;
    bool have_weather = (weather_fetch(&w) == ESP_OK);

    quote_t q;
    bool have_quote = (quote_fetch(&q) == ESP_OK);

    char date_line[48];
    format_date(date_line, sizeof(date_line));

    /* Serialise printer access — messages_task may also fire this minute. */
    xSemaphoreTake(s_print_mutex, portMAX_DELAY);

    thermal_printer_reset();
    thermal_printer_set_justify('C');
    thermal_printer_println("================================");
    thermal_printer_set_size('M');
    thermal_printer_println(date_line);
    thermal_printer_set_size('S');
    thermal_printer_println("================================");
    thermal_printer_feed(1);

    /* Weather sprite, centered above the weather text. */
    if (have_weather) {
        const uint8_t *sprite = weather_sprite_for_code(w.weather_code);
        if (sprite) {
            thermal_printer_set_justify('C');
            thermal_printer_print_bitmap(3, 24, sprite);
            thermal_printer_feed(1);
        }
    }

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

    xSemaphoreGive(s_print_mutex);

    ESP_LOGI(TAG, "briefing_run done");
}
```

(Net change vs. v1.1: the entire MESSAGES block is gone, plus mutex `take` near the top and `give` near the bottom.)

- [ ] **Step 4: Commit**

```bash
cd /Users/myles/dev/little-printer
git add main/briefing.c
git commit -m "briefing: drop messages block, take printer mutex"
```

---

### Task B4: Add `messages_print_pending()` to messages.c/h

**Files:**
- Modify: `/Users/myles/dev/little-printer/main/messages.h`
- Modify: `/Users/myles/dev/little-printer/main/messages.c`

`messages_print_pending` is the unit of work the new `messages_task` invokes. It fetches pending messages, takes the printer mutex, prints them under text-wrap, releases the mutex, and POSTs `/confirm` with the printed IDs.

- [ ] **Step 1: Append the declaration to messages.h**

Read `/Users/myles/dev/little-printer/main/messages.h`. After the existing `messages_confirm` declaration, append:

```c

/*
 * Fetch any pending messages and print them as standalone receipts (no
 * "MESSAGES" header — just message bodies + sender attributions). On
 * success, POSTs /confirm to drop the printed IDs from the queue.
 *
 * Acquires the printer mutex (printer_lock.h) for the duration of the
 * print. Safe to call concurrently with briefing_run().
 *
 * No-op if there are no pending messages. Returns ESP_FAIL on a fetch
 * or confirm error; the queue is unchanged in that case.
 */
esp_err_t messages_print_pending(void);
```

- [ ] **Step 2: Append the implementation to messages.c**

In `/Users/myles/dev/little-printer/main/messages.c`, the existing includes are:

```c
#include "messages.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "cJSON.h"

#include "http_fetch.h"
```

Replace with:

```c
#include "messages.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "cJSON.h"

#include "config.h"
#include "http_fetch.h"
#include "printer_lock.h"
#include "text_wrap.h"
#include "thermal_printer.h"
```

Then APPEND the following at the end of `messages.c`:

```c

/* Local copy of briefing.c's helper — kept simple to avoid sharing
 * via a header, since it's only 4 lines. */
static void println_indented(const char *line) {
    char buf[80];
    snprintf(buf, sizeof(buf), "  %s", line);
    thermal_printer_println(buf);
}

esp_err_t messages_print_pending(void) {
    message_t *msgs = NULL;
    size_t n = 0;
    if (messages_fetch_pending(&msgs, &n) != ESP_OK) {
        free(msgs);
        return ESP_FAIL;
    }
    if (n == 0) {
        free(msgs);
        return ESP_OK;
    }

    /* Take the printer mutex for the whole print so we don't interleave
     * with briefing_run if both fire in the same minute. */
    xSemaphoreTake(s_print_mutex, portMAX_DELAY);

    int ids[8];
    size_t to_confirm = n > 8 ? 8 : n;
    for (size_t i = 0; i < to_confirm; i++) {
        thermal_printer_set_justify('L');
        text_wrap(msgs[i].message, PRINT_LINE_WIDTH - 4, &println_indented);
        char attribution[48];
        snprintf(attribution, sizeof(attribution), "       -- %s", msgs[i].sender);
        thermal_printer_println(attribution);
        thermal_printer_feed(1);
        ids[i] = msgs[i].id;
    }
    thermal_printer_feed(3);
    thermal_printer_sleep(60);

    xSemaphoreGive(s_print_mutex);

    esp_err_t err = messages_confirm(ids, to_confirm);
    free(msgs);
    return err;
}
```

- [ ] **Step 3: Commit**

```bash
cd /Users/myles/dev/little-printer
git add main/messages.c main/messages.h
git commit -m "messages: add messages_print_pending (mutex-locked print + confirm)"
```

---

### Task B5: Wire messages_task and `m` console trigger into main.c

**Files:**
- Modify: `/Users/myles/dev/little-printer/main/main.c`

Adds:
- `printer_lock_init()` call early in `app_main`
- `messages_task` static function (poll every 30s, fire at top of hour during waking hours)
- `m` / `M` keystroke handling in `console_task`
- New `xTaskCreate(messages_task, ...)` alongside the existing two

- [ ] **Step 1: Add the new include**

In `/Users/myles/dev/little-printer/main/main.c`, find:

```c
#include "config.h"
#include "thermal_printer.h"
#include "wifi.h"
#include "time_sync.h"
#include "briefing.h"
```

Replace with:

```c
#include "config.h"
#include "thermal_printer.h"
#include "wifi.h"
#include "time_sync.h"
#include "briefing.h"
#include "messages.h"
#include "printer_lock.h"
```

- [ ] **Step 2: Add `m` keystroke handling to console_task**

Find:

```c
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
```

Replace with:

```c
static void console_task(void *arg) {
    while (1) {
        int c = fgetc(stdin);
        if (c == 'p' || c == 'P') {
            ESP_LOGI(TAG, "console: manual briefing trigger");
            briefing_run();
        } else if (c == 'm' || c == 'M') {
            ESP_LOGI(TAG, "console: manual message poll");
            messages_print_pending();
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
```

- [ ] **Step 3: Add the new messages_task function**

Find the existing `briefing_task`:

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
            ESP_LOGI(TAG, "scheduled briefing trigger");
            briefing_run();
            last_printed_yday = lt.tm_yday;
            time_sync_refresh();
        }

        vTaskDelay(pdMS_TO_TICKS(30 * 1000));
    }
}
```

Insert the following NEW function immediately after `briefing_task` (before `app_main`):

```c
static void messages_task(void *arg) {
    int last_fired_hour = -1;
    int last_fired_yday = -1;
    while (1) {
        time_t now = time(NULL);
        struct tm lt;
        localtime_r(&now, &lt);

        bool in_window = (lt.tm_hour >= CONFIG_MESSAGES_START_HOUR &&
                          lt.tm_hour <  CONFIG_MESSAGES_END_HOUR);
        bool fresh_slot = (lt.tm_hour != last_fired_hour ||
                           lt.tm_yday != last_fired_yday);

        if (in_window && lt.tm_min == 0 && fresh_slot) {
            ESP_LOGI(TAG, "scheduled message poll");
            messages_print_pending();
            last_fired_hour = lt.tm_hour;
            last_fired_yday = lt.tm_yday;
        }

        vTaskDelay(pdMS_TO_TICKS(30 * 1000));
    }
}
```

- [ ] **Step 4: Init the mutex and spawn the new task in app_main**

Find:

```c
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Init the printer early so GPIO21 isn't floating during Wi-Fi connect. */
    ESP_ERROR_CHECK(thermal_printer_init(PRINTER_UART_NUM,
                                         PRINTER_TX_PIN,
                                         PRINTER_RX_PIN,
                                         CONFIG_PRINTER_BAUD));
```

Replace with:

```c
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    printer_lock_init();

    /* Init the printer early so GPIO21 isn't floating during Wi-Fi connect. */
    ESP_ERROR_CHECK(thermal_printer_init(PRINTER_UART_NUM,
                                         PRINTER_TX_PIN,
                                         PRINTER_RX_PIN,
                                         CONFIG_PRINTER_BAUD));
```

Then find:

```c
    xTaskCreate(console_task,  "console",  8192, NULL, 5, NULL);
    xTaskCreate(briefing_task, "briefing", 8192, NULL, 4, NULL);
```

Replace with:

```c
    xTaskCreate(console_task,  "console",  8192, NULL, 5, NULL);
    xTaskCreate(briefing_task, "briefing", 8192, NULL, 4, NULL);
    xTaskCreate(messages_task, "messages", 8192, NULL, 4, NULL);
```

- [ ] **Step 5: Update the final ready log**

Find:

```c
    ESP_LOGI(TAG, "ready — briefing scheduled for %02d:%02d",
             CONFIG_PRINT_HOUR, CONFIG_PRINT_MINUTE);
```

Replace with:

```c
    ESP_LOGI(TAG, "ready — briefing %02d:%02d, messages hourly %02d:00–%02d:00",
             CONFIG_PRINT_HOUR, CONFIG_PRINT_MINUTE,
             CONFIG_MESSAGES_START_HOUR, CONFIG_MESSAGES_END_HOUR);
```

- [ ] **Step 6: Commit**

```bash
cd /Users/myles/dev/little-printer
git add main/main.c
git commit -m "main: add messages_task, m console trigger, mutex init"
```

---

### Task B6: Hardware verification

**Files:** none changed; user runs the device.

- [ ] **Step 1: Build and flash**

```bash
cd /Users/myles/dev/little-printer
idf.py build flash monitor
```

(No `menuconfig` is required — the new `MESSAGES_START_HOUR=8` and `MESSAGES_END_HOUR=22` defaults from Kconfig are fine for the live unit. Run menuconfig only if you want to change them.)

Expected log on boot:
```
I (xxx) main: little-printer booting
I (xxx) wifi: got IP a.b.c.d
I (xxx) time_sync: local time: ...
I (xxx) thermal_printer: initialised on UART1 @ 9600 baud
I (xxx) main: ready — briefing 09:00, messages hourly 08:00–22:00
```

- [ ] **Step 2: Verify daily briefing no longer prints messages**

The most reliable check: queue a message via the form, then trigger the briefing manually with `p`. The briefing should print weather + quote + footer separator and stop — no MESSAGES block, no extra paper for the queued message.

Cleanup the queued message before continuing:
```bash
URL=https://little-printer-msgs.fly.dev   # or your actual URL
TOKEN=<your token>
curl -s -H "Authorization: Bearer $TOKEN" "$URL/pending"
# note the IDs in the response
curl -X POST -H "Authorization: Bearer $TOKEN" -H 'Content-Type: application/json' \
    -d '{"ids":[<id>]}' "$URL/confirm"
```

- [ ] **Step 3: Verify the `m` console trigger works**

Submit two messages via the form. In the IDF monitor, type `m` and Enter. Expected log:
```
I (xxx) main: console: manual message poll
I (xxx) messages: fetched 2 pending message(s)
I (xxx) messages: confirmed 2 message(s)
```

The printer should print both messages back-to-back (each with `       -- <sender>` underneath), no header, paper advances 3 lines at the end. The form should subsequently show `queue: 0 / 3`.

- [ ] **Step 4: Verify the queue cap is now 3**

Submit messages via the form until you see the "queue full" rejection. The 4th submission should fail with HTTP 429 (the form will display the error message inline).

(After this the queue is full of test messages — clear them via `m` or wait for the next top-of-hour.)

- [ ] **Step 5: Verify the hourly poll fires (optional, requires waiting up to an hour)**

Wait for the next top of the hour (e.g., if the time is 14:23, wait until 14:59:30 or so and watch the monitor).

At HH:00 the log should show:
```
I (xxx) main: scheduled message poll
```

If there were any pending messages they print and confirm. If not, the call is a no-op.

If you don't want to wait, type `m` instead and confirm equivalent behaviour.

- [ ] **Step 6: Verify quiet-hours suppression (optional, requires waiting until 22:00 or temporarily reconfiguring)**

For an immediate check: `idf.py menuconfig` → set `MESSAGES_END_HOUR` to (current_hour) → save → `idf.py build flash monitor`. The next top-of-hour should NOT fire (the log should be quiet at HH:00). Restore the value to 22 afterwards.

This step is optional — the quiet-hours logic is a single arithmetic comparison, not high-risk.

---

## Self-review notes

**Spec coverage:**

| Spec section | Covered by |
|---|---|
| Goals (hourly print, briefing-independent, cap 3, no overnight prints) | Tasks A1, B1, B5 |
| Non-goals (no per-message push, no overnight catch-up, no DST hardening) | Honoured throughout |
| Architecture (briefing_task + messages_task share s_print_mutex) | B2 (mutex), B3 (briefing locks), B4 (messages locks), B5 (messages_task spawn) |
| Fly app changes | A1 |
| `briefing.c` strip + lock | B3 |
| `main.c` messages_task + `m` trigger + mutex init | B5 |
| `messages.c` messages_print_pending | B4 |
| Kconfig waking-hours window | B1 |
| Concurrency (printer mutex) | B2 + B3 + B4 |
| Failure modes (graceful degradation matches v1.1) | Inherited via existing messages_fetch_pending / messages_confirm |
| Repo / commit shape (8 commits ordered) | A1, B1, B2, B3, B4, B5 = 6 commits + Fly redeploy + B6 verification = 8 |
| Success criteria | All six covered by B6 verification steps |

**Placeholder scan:** No "TBD", "TODO", or vague language. Every step contains literal code, exact paths, or precise commands. The `<id>` and `<your token>` placeholders in B6 are user-substituted runtime values, not code-quality placeholders.

**Type / identifier consistency:**
- `s_print_mutex`, `printer_lock_init`, `xSemaphoreTake/Give` referenced consistently across B2, B3, B4.
- `messages_print_pending` declared in B4.1, defined in B4.2, called in B5.2/3.
- `CONFIG_MESSAGES_START_HOUR` / `CONFIG_MESSAGES_END_HOUR` defined in B1, used in B5 (messages_task body and the boot log line).
- `maxQueueSize` referenced in A1's both edits.
- `println_indented` is duplicated between briefing.c (left in place by B3) and messages.c (added by B4) — intentional per spec.

**Open assumptions:**
- The user's existing fly.io app token is valid (the C3 still uses it from v1.1's Kconfig). No new secret rotation in this redesign.
- The printer mutex initialisation happens before either task that uses it spawns — `printer_lock_init()` runs early in `app_main`, before `xTaskCreate(briefing_task, ...)` etc. This is correct in B5.
- `idf.py` is not on the controller's PATH; user runs builds and flashing themselves (same as v1.1).
