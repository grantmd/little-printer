# Hourly Message Poll — Design

## Background

v1.1 (the public message queue, implemented earlier today) had the C3 fetch and print queued messages once per day, as a trailing block of the 09:00 daily briefing. After running it for an evening, the user changed their mind: they want messages printed **soon** after they arrive (within the hour), with the broader briefing-handles-weather-and-quote vs. messages-handles-themselves split made explicit.

This redesign replaces the v1.1 in-briefing message block with an independent hourly poll task on the C3.

## Goals

- Print public-form messages within an hour of submission, during waking hours.
- Keep the existing daily 09:00 briefing (weather + quote) entirely independent of messages.
- Limit submissions to a queue of 3 in flight (down from 5).
- Don't print between 22:00 and 08:00 — the printer is in the user's office and shouldn't make noise overnight.
- Let messages submitted overnight accumulate in the Fly DB and print as a batch when the morning poll fires (subject to the 3-cap on the form side).

## Non-goals

- No per-message immediate push — the hourly cadence is intentional. Faster than that would risk feeling like a chat app the user has to manage.
- No quiet-hours catch-up logic (e.g., "at the start of waking hours, drain everything sent overnight"). The behavior of "morning poll picks up whatever's queued" is sufficient and emerges from the design without extra code.
- No per-message confirmation, ack, or read-receipt back to the sender.
- No changes to weather / quote / briefing / GPIO / printer wiring.

## Architecture

```
┌─────────────────┐
│  Fly.io Go app  │  (unchanged except: queue cap 5 → 3, form footer copy)
└────────▲────────┘
         │ HTTPS poll
         │ /pending + /confirm
         │
┌────────┴────────────────────────────┐
│            XIAO ESP32-C3            │
│                                     │
│  ┌─────────┐    ┌──────────────┐   │
│  │briefing │    │ messages_task│   │
│  │_task    │    │ NEW          │   │
│  │daily    │    │ tm_min == 0  │   │
│  │09:00    │    │ in 08–22 PT  │   │
│  └────┬────┘    └──────┬───────┘   │
│       │                │           │
│       │ s_print_mutex  │           │
│       └────────┬───────┘           │
│                ▼                   │
│        thermal_printer             │
└────────────────────────────────────┘
```

The two scheduling tasks (briefing_task, messages_task) are independent. Each holds its own dedup state. They share a printer mutex to avoid stepping on each other if both fire in the same minute.

## Components & changes

### Fly.io app (`fly-message-queue/`)

Only two small edits:

- **`validate.go`:** `maxQueueSize` 5 → 3.
- **`templates/index.html`:** add a one-line schedule note near the queue indicator: `"prints on the hour, 08:00–22:00 PT"`. Place it above the form, just under the existing `queue: N / M` indicator. The user's existing attribution footer at the bottom of the page stays untouched.

No endpoint changes, no schema changes, no DB migration. The service that's already deployed at `little-printer.fly.dev` keeps working unchanged until the next `fly deploy`.

### Firmware (`main/`)

**`briefing.c` — strip out the messages block.**

The v1.1 code added a "MESSAGES" block at the end of `briefing_run()` plus an `#include "messages.h"`, an `#include <stdlib.h>` (used for `free`), and `messages_fetch_pending` / `messages_confirm` calls. All of this comes out. Briefing returns to v1's behavior: weather + quote + footer + done.

**`main.c` — add `messages_task`:**

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
        bool should_fire = in_window && lt.tm_min == 0 &&
                           (lt.tm_hour != last_fired_hour ||
                            lt.tm_yday != last_fired_yday);

        if (should_fire) {
            ESP_LOGI(TAG, "scheduled message poll");
            messages_print_pending();
            last_fired_hour = lt.tm_hour;
            last_fired_yday = lt.tm_yday;
        }

        vTaskDelay(pdMS_TO_TICKS(30 * 1000));
    }
}
```

Spawned alongside the existing tasks: `xTaskCreate(messages_task, "messages", 8192, NULL, 4, NULL);`. 8 KB stack matches `briefing_task` for the same TLS-handshake reasons documented in v1.1.

The dedup uses both `tm_hour` and `tm_yday` (rather than just `tm_hour`) to handle day-rollovers: at midnight, `tm_yday` changes and the previous day's "last_fired_hour=22" doesn't block tomorrow's 08:00 fire.

**`main.c` — add `m` console trigger** alongside the existing `p`:

```c
if (c == 'm' || c == 'M') {
    ESP_LOGI(TAG, "console: manual message poll");
    messages_print_pending();
}
```

Useful for testing without waiting for the top of the hour.

**`messages.c` — add `messages_print_pending()`** (in addition to the existing `messages_fetch_pending` and `messages_confirm`):

```c
void messages_print_pending(void) {
    message_t *msgs = NULL;
    size_t n = 0;
    if (messages_fetch_pending(&msgs, &n) != ESP_OK || n == 0) {
        free(msgs);
        return;
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

    messages_confirm(ids, to_confirm);
    free(msgs);
}
```

`s_print_mutex` is a new `SemaphoreHandle_t` declared in a small new shared module (see "Concurrency" below). `println_indented` already exists in `briefing.c` as a static — duplicate the 4-line helper into `messages.c` rather than promoting it (the helper is trivial; sharing it would require a header dance that doesn't earn its keep).

Header (`messages.h`) gets one new declaration:
```c
void messages_print_pending(void);
```

**Kconfig (`main/Kconfig.projbuild`) — two new entries:**

```kconfig
config MESSAGES_START_HOUR
    int "Start of messages quiet-hours window (inclusive, local time)"
    default 8
    range 0 23

config MESSAGES_END_HOUR
    int "End of messages quiet-hours window (exclusive, local time)"
    default 22
    range 1 24
```

## Concurrency

Both `briefing_task` and `messages_task` poll on a 30-second cadence. With `briefing_task` defaulting to 09:00 and `messages_task` always firing at the top of the hour, they will both fire on the same 30-second tick at 09:00 every day (and similarly any time the user picks an HH:00 briefing time). Without coordination they'd race on `thermal_printer_*` calls — those write directly to UART1 and have no internal locking.

**Solution:** a single FreeRTOS binary semaphore (`s_print_mutex`) acquired by each high-level print operation:

- `briefing_run()` takes it before printing the briefing, releases at end.
- `messages_print_pending()` takes it before printing messages, releases at end.

Whichever wins the race executes first; the other blocks until released. Both call sites already finish in well under a minute, so the loser fires shortly after.

The mutex lives in a tiny new file `main/printer_lock.c` + `main/printer_lock.h` exposing:

```c
extern SemaphoreHandle_t s_print_mutex;
void printer_lock_init(void);
```

`app_main` calls `printer_lock_init()` once during boot. Both `briefing_run()` and `messages_print_pending()` use `xSemaphoreTake(s_print_mutex, portMAX_DELAY)` / `xSemaphoreGive`.

(Alternative: thread the mutex through function arguments. Rejected — every print site would need it, and the mutex is conceptually a singleton.)

## Failure modes

Same shape as v1.1 — everything degrades gracefully:

- **`/pending` fetch fails (network, 5xx):** that hour silently skipped, queue untouched in Fly, retry next hour.
- **`/confirm` fails:** messages reprint next hour. Duplicate print but no data loss.
- **C3 offline at top of hour:** that hour is silently skipped; next hour tries again. If offline for the full waking window, accumulated messages print at the next available top-of-hour after coming back online.
- **C3 powered off overnight:** at 08:00, the morning poll finds whatever accumulated (subject to the form's 3-cap).
- **Top-of-hour fires while another briefing/poll is in progress:** the printer mutex serializes. The waiting task fires after the current one completes (typically within seconds).

## Repo / commit shape

A single PR-equivalent series of commits, ordered roughly:

1. Fly app: queue cap 5 → 3, form schedule note (one commit, deploys via `fly deploy` after merge).
2. Firmware: strip messages block from `briefing.c`.
3. Firmware: new `printer_lock.c/h` + init in main.
4. Firmware: `briefing_run` takes the mutex.
5. Firmware: `messages_print_pending()` added to `messages.c/h`.
6. Firmware: `main.c` gets `messages_task` + `m` console trigger.
7. Firmware: Kconfig entries for the waking-hours window.
8. Hardware verification.

The Fly app change can deploy independently before or after the firmware change — they're loosely coupled. The firmware will keep working with the old 5-cap until the deploy lands; the form will reject 6+ submissions instead of 4+ during the window in between.

## Out of scope

- Anything beyond the bullet list above. In particular: per-IP rate limiting, profanity filter, OAuth, DST hardening for the waking-hours window (POSIX TZ already handles this — `tm_hour` is local time post-`tzset()`), notification-style mid-hour printing for "urgent" messages.
- Removing the `messages_fetch_pending` / `messages_confirm` integration tests in `messages.c` — they still apply to the redesigned path.

## Success criteria

1. Submitting a message at 14:23 results in a print within `[15:00, 15:01]` if the C3 is online and within waking hours.
2. Submitting a message at 23:00 stays queued; prints at the next 08:00 along with anything else that came in overnight.
3. The daily briefing at 09:00 contains weather + quote with no messages block, regardless of queue state.
4. Submitting a 4th message when 3 are already queued returns 429 from `/submit`.
5. The form at `https://little-printer-msgs.fly.dev/` shows `queue: N / 3` and a small note about the polling schedule.
6. The C3 firmware survives both `briefing_task` and `messages_task` firing at 09:00 simultaneously without garbled output.
