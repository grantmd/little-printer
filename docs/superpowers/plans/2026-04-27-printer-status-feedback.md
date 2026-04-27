# Printer Status Feedback Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the data-loss gap when the printer is unpowered, disconnected, or out of paper, by adding an ESC/POS `DLE EOT 4` status query to the firmware and gating every high-level print operation on its result.

**Architecture:** One new function `thermal_printer_query_status()` issues `DLE EOT 4`, reads one byte over UART1 RX (GPIO20) with a 100ms timeout, parses the paper-sensor bits into a struct. Three call sites — `briefing_run`, `messages_print_pending`, and an `app_main` boot-time smoke test — invoke it and bail on offline-or-no-paper. A new `s` console key surfaces the same query for manual diagnostics (notably useful for distinguishing "printer offline" from "C3 GPIO20 dead").

**Tech Stack:** ESP-IDF v5.x, `driver/uart` (`uart_read_bytes`, `uart_flush_input`), FreeRTOS mutex (existing `s_print_mutex` from `printer_lock.h`).

**Reference docs:**
- Spec: `docs/superpowers/specs/2026-04-27-printer-status-feedback-design.md`
- Prior firmware: `docs/superpowers/plans/2026-04-27-hourly-message-poll.md`

---

## File structure (after the plan completes)

```
little-printer/main/
├── thermal_printer.h    (MODIFY — add status struct + query_status decl)
├── thermal_printer.c    (MODIFY — add query_status impl)
├── briefing.c           (MODIFY — pre-flight inside the existing mutex region)
├── messages.c           (MODIFY — pre-flight + restructured mutex use)
└── main.c               (MODIFY — boot smoke test + `s` console key)
```

(No new files; no CMakeLists.txt change. This is purely additive to existing modules.)

---

### Task 1: Add `thermal_printer_query_status` (header + impl)

**Files:**
- Modify: `/Users/myles/dev/little-printer/components/thermal_printer/include/thermal_printer.h`
- Modify: `/Users/myles/dev/little-printer/components/thermal_printer/thermal_printer.c`

The header just gets a new struct and one function declaration. The implementation flushes the RX buffer (so stale bytes from a prior print don't get mistaken for the status reply), sends three bytes (`0x10 0x04 0x04`), reads one byte with a 100ms timeout, parses paper-sensor bits.

- [ ] **Step 1: Append the struct + declaration to `thermal_printer.h`**

Read the current `/Users/myles/dev/little-printer/components/thermal_printer/include/thermal_printer.h` to confirm its content. The file currently ends with declarations for `thermal_printer_feed`, `thermal_printer_sleep`, and `thermal_printer_print_bitmap`. Append the new content at the end.

Add this exact content to the bottom of the file (before any closing `#endif` if there were one — but the file uses `#pragma once`, so just append at the end):

```c

/*
 * Paper-sensor status reported by the printer in response to a
 * thermal_printer_query_status() call. Both bits are FALSE when paper
 * is loaded normally.
 */
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

- [ ] **Step 2: Append the implementation to `thermal_printer.c`**

Append this exact content to the end of `/Users/myles/dev/little-printer/components/thermal_printer/thermal_printer.c`:

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

(The existing `tx()` static helper, `s_uart` static, and `TAG` static at the top of the file all remain in place and are reused.)

- [ ] **Step 3: Commit**

```bash
cd /Users/myles/dev/little-printer
git add components/thermal_printer/include/thermal_printer.h components/thermal_printer/thermal_printer.c
git commit -m "thermal_printer: add query_status (DLE EOT 4 paper sensor)"
```

---

### Task 2: Pre-flight check in `briefing.c`

**Files:**
- Modify: `/Users/myles/dev/little-printer/main/briefing.c`

`briefing_run()` already takes the printer mutex around its body (added in v1.2). Insert the status query immediately after taking the mutex, bail (releasing the mutex) on failure.

- [ ] **Step 1: Insert the pre-flight block**

In `/Users/myles/dev/little-printer/main/briefing.c`, find:

```c
    /* Serialise printer access — messages_task may also fire this minute. */
    xSemaphoreTake(s_print_mutex, portMAX_DELAY);

    thermal_printer_reset();
```

Replace with:

```c
    /* Serialise printer access — messages_task may also fire this minute. */
    xSemaphoreTake(s_print_mutex, portMAX_DELAY);

    /* Pre-flight: if the printer is offline or out of paper, skip the
     * print so we don't quietly drop output to a void. */
    thermal_printer_status_t pstatus;
    if (thermal_printer_query_status(&pstatus) != ESP_OK) {
        ESP_LOGW(TAG, "printer not responding; skipping briefing");
        xSemaphoreGive(s_print_mutex);
        return;
    }
    if (pstatus.paper_end) {
        ESP_LOGW(TAG, "printer out of paper; skipping briefing");
        xSemaphoreGive(s_print_mutex);
        return;
    }
    if (pstatus.paper_near_end) {
        ESP_LOGW(TAG, "printer paper near end — printing anyway");
    }

    thermal_printer_reset();
```

- [ ] **Step 2: Commit**

```bash
cd /Users/myles/dev/little-printer
git add main/briefing.c
git commit -m "briefing: pre-flight printer status, skip if offline or out of paper"
```

---

### Task 3: Pre-flight check + restructured mutex use in `messages.c`

**Files:**
- Modify: `/Users/myles/dev/little-printer/main/messages.c`

The current `messages_print_pending()` (from v1.2) does: fetch (no mutex) → take mutex → print → release mutex → confirm. We need to insert a status check BEFORE the fetch, taking the mutex briefly for just the status query and releasing it (so we don't hold the mutex during the multi-second HTTPS fetch).

The structure becomes: take mutex → status query → release mutex → bail-if-bad → fetch → take mutex → print → release mutex → confirm.

- [ ] **Step 1: Read the current state of `messages_print_pending`**

Read `/Users/myles/dev/little-printer/main/messages.c` and confirm the existing `messages_print_pending` function looks like:

```c
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

- [ ] **Step 2: Replace the function with the pre-flight version**

Replace the entire function (the block shown in Step 1) with:

```c
esp_err_t messages_print_pending(void) {
    /* Pre-flight: is the printer alive and ready? */
    xSemaphoreTake(s_print_mutex, portMAX_DELAY);
    thermal_printer_status_t pstatus;
    esp_err_t qerr = thermal_printer_query_status(&pstatus);
    xSemaphoreGive(s_print_mutex);

    if (qerr != ESP_OK) {
        ESP_LOGW(TAG, "printer not responding; deferring messages");
        return ESP_FAIL;
    }
    if (pstatus.paper_end) {
        ESP_LOGW(TAG, "printer out of paper; deferring messages");
        return ESP_FAIL;
    }
    if (pstatus.paper_near_end) {
        ESP_LOGW(TAG, "printer paper near end — printing anyway");
    }

    /* Fetch + print + confirm flow. */
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

(Net change: a new pre-flight block at the top — take mutex, query, release mutex, bail on bad status, log near-end. Everything from `/* Fetch + print + confirm flow. */` onward is unchanged from the existing implementation.)

- [ ] **Step 3: Commit**

```bash
cd /Users/myles/dev/little-printer
git add main/messages.c
git commit -m "messages: pre-flight printer status before fetch + print"
```

---

### Task 4: Boot smoke test + `s` console key in `main.c`

**Files:**
- Modify: `/Users/myles/dev/little-printer/main/main.c`

Add a one-shot status query at boot (logs "printer reachable" or "not responding"), and an `s`/`S` branch in `console_task` for manual diagnostics.

- [ ] **Step 1: Add boot-time smoke test**

In `/Users/myles/dev/little-printer/main/main.c`, find:

```c
    if (wifi_connect() != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi connect failed; continuing offline");
    } else {
        time_sync_init();
    }

    xTaskCreate(console_task,  "console",  8192, NULL, 5, NULL);
```

Replace with:

```c
    if (wifi_connect() != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi connect failed; continuing offline");
    } else {
        time_sync_init();
    }

    /* One-shot smoke test so the boot log surfaces printer health early. */
    thermal_printer_status_t boot_status;
    if (thermal_printer_query_status(&boot_status) == ESP_OK) {
        ESP_LOGI(TAG, "printer reachable: paper_end=%d near_end=%d",
                 boot_status.paper_end, boot_status.paper_near_end);
    } else {
        ESP_LOGW(TAG, "printer not responding on boot — check power, cable, or GPIO20");
    }

    xTaskCreate(console_task,  "console",  8192, NULL, 5, NULL);
```

- [ ] **Step 2: Add `s` console key in `console_task`**

Find:

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
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
```

- [ ] **Step 3: Commit**

```bash
cd /Users/myles/dev/little-printer
git add main/main.c
git commit -m "main: boot-time printer smoke test + `s` console diagnostic key"
```

---

### Task 5: Hardware verification

**Files:** none changed; user runs the device.

This is the empirical proof that the read path actually works. Most importantly: it doubles as the diagnostic for whether GPIO20 is alive after potentially having seen 5V from the printer's TX for several days.

- [ ] **Step 1: Build and flash**

```bash
cd /Users/myles/dev/little-printer
idf.py build flash monitor
```

(No `menuconfig` needed.)

- [ ] **Step 2: Verify boot smoke test reports the printer as reachable**

With the printer powered on and paper loaded, the boot log should include a line like:

```
I (xxx) main: printer reachable: paper_end=0 near_end=0
```

If instead you see:

```
W (xxx) main: printer not responding on boot — check power, cable, or GPIO20
```

…that's the signal. **Try power-cycling the printer** (unplug power, count to 3, replug) and reflashing. If the message still says "not responding" reliably across multiple flashes with a known-good printer, GPIO20 is the suspect — order the level shifter and consider a new C3.

- [ ] **Step 3: Verify the `s` console key works**

In the IDF monitor, type `s` + Enter. With the printer up:

```
I (xxx) main: console status: paper_end=0 near_end=0
```

With the printer powered off (unplug its power adapter, leave the data leads connected), type `s` again:

```
W (xxx) main: console status: no response (printer offline or GPIO20 dead)
```

Plug the printer back in, type `s` again — should return to "paper_end=0 near_end=0".

- [ ] **Step 4: Verify no-paper detection (optional)**

If you can spare a few seconds of paper roll: open the printer's lid (which exposes a paper-out sensor on most ESC/POS printers), type `s` while the lid is open or paper unloaded:

```
I (xxx) main: console status: paper_end=1 near_end=0
```

Close the lid / re-load paper, type `s` again, confirm `paper_end=0`. (This step is optional — if the printer doesn't have a separate "no paper loaded" sensor and only fires `paper_end` at actual end-of-roll, you may not be able to easily reproduce it.)

- [ ] **Step 5: Verify the messages-flow no-loss behaviour**

The reproduction of the original incident:

1. Submit a test message via the form: `https://<your-fly-app>.fly.dev/`.
2. Confirm the form shows `queue: 1 / 3`.
3. **Unplug the printer's power adapter.** Leave the data leads connected.
4. Type `m` in the IDF monitor.
5. Expected log:
   ```
   I (xxx) main: console: manual message poll
   W (xxx) messages: printer not responding; deferring messages
   ```
6. Refresh the form. Should still show `queue: 1 / 3` — the message is still queued.
7. Plug the printer back in. Wait a moment for it to be ready. Type `m` again.
8. Expected: the message prints, the log shows the normal fetch + confirm sequence, and the form refreshes to `queue: 0 / 3`.

This confirms the data-loss fix works end to end. Step 5 is the most important verification — it's literally the bug that motivated this work.

- [ ] **Step 6: Verify the briefing-flow analog (optional)**

Same pattern as Step 5 but for the briefing path. With the printer unplugged, type `p`:

```
I (xxx) main: console: manual briefing trigger
I (xxx) briefing: briefing_run starting
... (weather + quote fetches still happen) ...
W (xxx) briefing: printer not responding; skipping briefing
```

No paper output. Plug the printer back in, type `p` again — full briefing prints.

- [ ] **Step 7: No commit**

This task is verification only.

---

## Self-review notes

**Spec coverage:**

| Spec section | Covered by |
|---|---|
| Goals (detect offline, detect paper-out, no side effects on failure, manual diagnostic, boot health) | Task 1 (function), Task 2 (briefing pre-flight), Task 3 (messages pre-flight), Task 4 (boot smoke test + `s` key) |
| Non-goals (no mid-print, no near-end action, no GPIO-vs-offline disambiguation, no DLE EOT 1/2/3) | Honoured throughout |
| Architecture (one new function, three call sites) | Tasks 1–4 |
| API (struct + esp_err_t function) | Task 1 |
| `briefing.c` integration | Task 2 |
| `messages.c` integration with restructured mutex use | Task 3 |
| `main.c` boot test + `s` key | Task 4 |
| Failure modes (offline, paper out, GPIO dead, stale RX, garbled response, race) | Inherited from Task 1 (flush + timeout + bit parse) and Tasks 2/3 (bail-on-error semantics) |
| Success criteria (six concrete checks) | Task 5 (hardware verification) covers all six |

**Placeholder scan:** No "TBD", "TODO", "appropriate", or vague language. Every step contains literal code, exact paths, or precise commands. The `<your-fly-app>` placeholder in Task 5 Step 5 is a user-substituted runtime value.

**Type / identifier consistency:**
- `thermal_printer_status_t`, `paper_end`, `paper_near_end` referenced consistently across Tasks 1, 2, 3, 4.
- `thermal_printer_query_status` signature `(thermal_printer_status_t *out)` returning `esp_err_t` matches between header (Task 1.1) and impl (Task 1.2) and call sites (Tasks 2, 3, 4).
- `s_print_mutex`, `xSemaphoreTake`, `xSemaphoreGive`, `portMAX_DELAY` referenced consistently — same primitives used in v1.2's `printer_lock.h`.

**Open assumptions:**
- The MC206H supports `DLE EOT 4` (standard ESC/POS — virtually all ESC/POS printers do, but the only authoritative way to confirm is to try it). Task 5 Step 2 is the empirical confirmation.
- The C3's GPIO20 is not damaged from prior 5V exposure (if any). Task 5 Step 2 is also the diagnostic for this.
- No new IDF dependency: `uart_read_bytes` and `uart_flush_input` are part of `driver/uart` which is already in the `thermal_printer` component's REQUIRES.
