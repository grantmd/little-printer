# Printer Status Feedback — Design

## Background

The C3 firmware drives the MC206H thermal printer purely one-way: send UART bytes, hope they land. If the printer is unpowered, disconnected, or out of paper, the firmware sends the bytes anyway and proceeds as if the print succeeded — including, in the messages flow, deleting the queued messages from the Fly.io DB. Result: silent data loss. We hit this in practice on 2026-04-27 when the printer's power leads came undone during a manual `m` trigger; messages were drained from the queue but never printed.

The wiring for two-way comms has been in place since v1: the printer's TX line is connected to the C3's GPIO20 (UART1 RX). The firmware just hasn't been reading from it. ESC/POS supports `DLE EOT n` real-time status queries — the printer responds with a single status byte over its TX line. Adding the read path closes the data-loss gap.

Pending hardware caveat: the printer's TX could be 3.3V (safe, plug-and-play) or 5V (would have been stressing the C3's not-5V-tolerant GPIO20 for several days). The user's multimeter is dead and a search didn't yield a definitive MC206H-specific spec. We're proceeding with firmware on the working assumption that GPIO20 is intact; a side-effect of this work is that we'll learn empirically whether the pin still functions (consistent timeouts on `DLE EOT 4` against a known-up printer = pin is dead and a level shifter + possibly a new C3 are needed).

## Goals

- Detect "printer offline" (unpowered, disconnected, or comms broken) before any high-level print operation.
- Detect "printer out of paper" before any print.
- On either failure: skip the print AND skip any side effects that assume the print happened (notably, do not delete messages from the Fly queue).
- Give the user a manual "is the printer alive" diagnostic via the serial console.
- Log printer status on boot so the user sees health early without needing to trigger a print.

## Non-goals

- Mid-print checks (e.g., paper-out detected after we've already started printing). Recovery from mid-print failures is hard and the failure mode — partial print — is visible to the user. YAGNI.
- Recovery from "near-end" paper. We log a warning and proceed; using the last few cm of a roll is fine.
- Distinguishing "GPIO20 dead" from "printer unpowered" in firmware. Both manifest as "no response within timeout." User-driven empirical test (manually `s`-trigger while printer is up) is the diagnostic.
- A printer-online dashboard or web exposure of status. Logs only.
- Buzzer / LED indication of status — purely software/log for v1.

## Architecture

```
                      ┌────────────────────────┐
                      │  thermal_printer.c     │
                      │  + query_status()      │
                      │    DLE EOT 4 → 1 byte  │
                      └────────────┬───────────┘
                                   │ called by
      ┌────────────────────────────┼─────────────────────────┐
      │                            │                         │
      ▼                            ▼                         ▼
┌────────────┐              ┌──────────────┐           ┌──────────────┐
│ briefing.c │              │  messages.c  │           │ main.c       │
│ pre-flight │              │  pre-flight  │           │ 's' key +    │
│ inside lock│              │  inside lock │           │ boot smoke-  │
│            │              │              │           │ test         │
└────────────┘              └──────────────┘           └──────────────┘
```

Single new function in `thermal_printer.c`. Three call sites that all act on the result the same way: bail on offline-or-no-paper, log, return. Messages stay queued; the briefing skips a day.

## Components & changes

### `thermal_printer.h`

Add a new struct and one new function declaration:

```c
typedef struct {
    bool paper_end;       /* hard out: stop, don't print */
    bool paper_near_end;  /* warning only: still print, but log it */
} thermal_printer_status_t;

/*
 * Query the printer for paper-sensor status via ESC/POS DLE EOT 4.
 * Returns ESP_OK if the printer responded (regardless of paper state) —
 * inspect *out for details. Returns ESP_FAIL on timeout (printer
 * offline, unpowered, disconnected, or GPIO20 not receiving).
 *
 * Internally flushes the UART RX buffer before the query so stale bytes
 * from earlier prints don't get mistaken for a status reply.
 */
esp_err_t thermal_printer_query_status(thermal_printer_status_t *out);
```

### `thermal_printer.c`

Add the implementation:

```c
esp_err_t thermal_printer_query_status(thermal_printer_status_t *out) {
    if (!out) return ESP_ERR_INVALID_ARG;

    /* Drop any leftover bytes in the RX buffer so we don't read a stale
     * status byte from a prior call. */
    uart_flush_input(s_uart);

    /* DLE EOT 4 — transmit paper-sensor status. */
    const uint8_t cmd[] = { 0x10, 0x04, 0x04 };
    tx(cmd, sizeof(cmd));

    uint8_t resp = 0;
    int n = uart_read_bytes(s_uart, &resp, 1, pdMS_TO_TICKS(100));
    if (n != 1) {
        ESP_LOGW(TAG, "status query timed out");
        return ESP_FAIL;
    }

    /* Per ESC/POS spec, DLE EOT 4 reply byte:
     *   bit 2/3 — paper near end sensor (1 = near end)
     *   bit 5/6 — paper end sensor      (1 = end)
     * Bits 2 and 3 mirror each other; same for 5 and 6. We OR them. */
    out->paper_near_end = (resp & 0x0C) != 0;
    out->paper_end      = (resp & 0x60) != 0;

    ESP_LOGI(TAG, "status: 0x%02X (paper_end=%d near_end=%d)",
             resp, out->paper_end, out->paper_near_end);
    return ESP_OK;
}
```

UART driver setup already configures GPIO20 as UART1 RX in `thermal_printer_init` — no changes needed there. The driver was installed with `uart_driver_install(uart_num, 512, 0, 0, NULL, 0)` which gives a 512-byte RX buffer; ample for 1-byte status replies.

### `briefing.c` — pre-flight inside the mutex

Wrap the existing print body with a status check immediately after taking the mutex:

```c
xSemaphoreTake(s_print_mutex, portMAX_DELAY);

thermal_printer_status_t status;
if (thermal_printer_query_status(&status) != ESP_OK) {
    ESP_LOGW(TAG, "printer not responding; skipping briefing");
    xSemaphoreGive(s_print_mutex);
    return;
}
if (status.paper_end) {
    ESP_LOGW(TAG, "printer out of paper; skipping briefing");
    xSemaphoreGive(s_print_mutex);
    return;
}
if (status.paper_near_end) {
    ESP_LOGW(TAG, "printer paper near end — printing anyway");
}

// ... existing print code (thermal_printer_reset(), header, weather, quote, footer)
xSemaphoreGive(s_print_mutex);
```

### `messages.c` — pre-flight check before fetching

The HTTPS fetch is expensive; no point doing it if we can't print. So the order is: take mutex → status check → release mutex → fetch (over HTTPS, no mutex needed) → re-take mutex → print → release mutex → confirm.

```c
esp_err_t messages_print_pending(void) {
    /* Pre-flight: is the printer alive and ready? */
    xSemaphoreTake(s_print_mutex, portMAX_DELAY);
    thermal_printer_status_t status;
    esp_err_t qerr = thermal_printer_query_status(&status);
    xSemaphoreGive(s_print_mutex);

    if (qerr != ESP_OK) {
        ESP_LOGW(TAG, "printer not responding; deferring messages");
        return ESP_FAIL;
    }
    if (status.paper_end) {
        ESP_LOGW(TAG, "printer out of paper; deferring messages");
        return ESP_FAIL;
    }
    if (status.paper_near_end) {
        ESP_LOGW(TAG, "printer paper near end — printing anyway");
    }

    /* Fetch + print + confirm flow continues unchanged from v1.2. */
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

    xSemaphoreTake(s_print_mutex, portMAX_DELAY);
    /* ... existing per-message print loop ... */
    xSemaphoreGive(s_print_mutex);

    esp_err_t err = messages_confirm(ids, to_confirm);
    free(msgs);
    return err;
}
```

The mutex is released between the status check and the fetch because (a) the fetch is HTTPS and doesn't touch the printer, (b) holding the mutex across a multi-second HTTP call would unnecessarily block briefing_run if it fires.

A potential race: between status-check (printer OK) and the actual print (mutex re-taken), the printer could go offline (someone trips the power cable). The print would silently fail and `messages_confirm` would still run, dropping the messages. Acceptable for v1.2-status — the window is small and the overall failure rate is dominated by the prior "printer was offline the whole time" case which we now catch. If we want to close the race we'd add a post-print status check; deferred.

### `main.c` — boot-time smoke test + `s` console key

In `app_main`, after `thermal_printer_init()` and after Wi-Fi/NTP have settled (so logs go out cleanly), log a one-shot status query. This lets the user see "printer alive?" in the boot log without typing anything:

```c
thermal_printer_status_t boot_status;
if (thermal_printer_query_status(&boot_status) == ESP_OK) {
    ESP_LOGI(TAG, "printer reachable: paper_end=%d near_end=%d",
             boot_status.paper_end, boot_status.paper_near_end);
} else {
    ESP_LOGW(TAG, "printer not responding on boot — check power, cable, or GPIO20");
}
```

In `console_task`, add the `s`/`S` branch alongside the existing `p` and `m`:

```c
} else if (c == 's' || c == 'S') {
    thermal_printer_status_t status;
    xSemaphoreTake(s_print_mutex, portMAX_DELAY);
    esp_err_t err = thermal_printer_query_status(&status);
    xSemaphoreGive(s_print_mutex);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "console status: paper_end=%d near_end=%d",
                 status.paper_end, status.paper_near_end);
    } else {
        ESP_LOGW(TAG, "console status: no response (printer offline or GPIO20 dead)");
    }
}
```

## Failure modes

- **Printer unpowered:** query times out → both `briefing_run` and `messages_print_pending` bail. Briefing skipped today (next try is tomorrow's 09:00). Messages stay queued; hourly retry.
- **Paper out:** query returns success with `paper_end=true` → both bail. Same retry semantics.
- **GPIO20 fried:** query consistently times out, regardless of printer state. **Empirical diagnostic:** type `s` while the printer is powered on, idle, and known-good (just power-cycled, fed paper visible). If `s` consistently logs "no response" across multiple attempts and reboots, GPIO20 is suspect — order a level shifter and possibly a new C3.
- **Stale bytes in RX buffer:** `uart_flush_input` runs before the query.
- **Printer responds with garbled byte:** struct fields read whatever they read. Worst case is a false near-end warning. Printer is at least responsive, so we proceed.
- **Race between status-check-OK and print-start (printer goes offline in between):** print fails silently, messages get confirmed and dropped. Acceptable v1.2-status risk; closing this race is deferred.

## Out of scope for this design

- Distinguishing GPIO20-dead from printer-unpowered in firmware (relies on user diagnostic via `s`).
- Mid-print or post-print status checks.
- Acting on `paper_near_end` — purely a log warning.
- Status query for any sensor other than paper (DLE EOT 1 / 2 / 3 — error status, off-line status — could be added later if needed).
- Hardware (level shifter, divider) decisions — those happen out-of-band based on user observation.

## Success criteria

1. With the printer unplugged from data or power, typing `s` in the console logs "no response."
2. With the printer plugged in and paper loaded, typing `s` logs `paper_end=0 near_end=0`.
3. With the printer plugged in but no paper, typing `s` logs `paper_end=1`.
4. With the printer unplugged, typing `m` (or waiting for the top of the hour) does NOT drain the message queue — pending messages remain in the Fly DB.
5. With the printer unplugged at 09:00, the briefing is silently skipped (no garbled prints, no errors flooding the log beyond the one warning).
6. Boot log shows printer status as a normal `I (xxx) main:` line so the user sees health on every flash.

## Repo / commit shape

A small linear series:

1. `thermal_printer`: add `query_status` (header + impl).
2. `briefing.c`: pre-flight check inside mutex.
3. `messages.c`: pre-flight check before fetch + re-take mutex for print.
4. `main.c`: boot smoke test + `s` console key.
5. Hardware verification (user).

Likely 4 commits + 1 verification step.
