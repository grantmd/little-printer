# CSN-A2 Diagnostic Firmware Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A throwaway ESP-IDF firmware for XIAO ESP32-C3 that runs three progressively-more-aggressive thermal-head test passes on a CSN-A2 printer on boot, then halts, to determine whether any heating elements survived over-voltage damage.

**Architecture:** Single-file ESP-IDF project at `diag/` off the repo root. One UART1 @ 9600/8N1 on GPIO21 TX / GPIO20 RX; no Wi-Fi, NVS, SNTP, HTTP, or scheduling. Boot → 2s settle → `ESC @` reset → three passes (each: `ESC 7` heat params, then a banner + four 16-char stress rows + feed) → infinite halt. Logs each step via `ESP_LOGI` on USB-Serial-JTAG so a blank paper vs. a dead MCU can be told apart.

**Tech Stack:** ESP-IDF v5.x (`driver/uart`, FreeRTOS, `esp_log`), target `esp32c3`. No external components.

**Testing note:** This is firmware that drives a physical printer. There is no off-device unit test worth writing for "emit the right ESC/POS bytes" — the real verification is the build succeeding (`idf.py build`) and, separately, observing what lands on paper when flashed. TDD is not applicable; the plan substitutes a build-gate and a manual hardware-test checklist.

**Scope guard:** Do not pull anything from `SPEC.md` (Wi-Fi, NTP, APIs, `thermal_printer` component, Kconfig menu). Anything that isn't strictly needed to emit the three passes and halt is out of scope.

---

## File structure

All paths are relative to repo root `/Users/myles/dev/little-printer/`.

- **Create: `diag/CMakeLists.txt`** — ESP-IDF top-level project file. Three lines: `cmake_minimum_required`, `include project.cmake`, `project(diag)`.
- **Create: `diag/sdkconfig.defaults`** — Committed ESP-IDF config defaults: main task stack size + explicit USB-Serial-JTAG console (so `ESP_LOGI` reaches the user).
- **Create: `diag/main/CMakeLists.txt`** — Main component registration: one source, one include dir, explicit `REQUIRES driver` so UART APIs link on both older and newer IDF 5.x layouts.
- **Create: `diag/main/main.c`** — All firmware logic: UART init, `tx()`/`tx_str()` helpers, `run_pass()` helper, `app_main()` with three passes then halt.

No existing files are modified. `diag/` is a self-contained ESP-IDF project, independent of the yet-unwritten main project that will eventually live at the repo root per `SPEC.md`.

---

## Preflight

### Task 0: Confirm ESP-IDF is available

**Why:** The whole plan assumes `idf.py` is on PATH. If it isn't, stop and ask the user to source the export script before continuing — do not attempt to install IDF.

- [ ] **Step 0.1: Check `idf.py` is on PATH**

Run:
```bash
idf.py --version
```
Expected: a version string like `ESP-IDF v5.x.x`. If the command is not found, stop and ask the user to run `. $HOME/esp/esp-idf/export.sh` (or their equivalent) and re-invoke the plan. Do not proceed.

---

## Task 1: Create the ESP-IDF project scaffolding

**Files:**
- Create: `diag/CMakeLists.txt`
- Create: `diag/sdkconfig.defaults`
- Create: `diag/main/CMakeLists.txt`

These three files give us a buildable (but empty-ish) ESP-IDF project. We create all three in one task because none of them is useful in isolation — the first meaningful build-gate lives in Task 3 after `main.c` exists.

- [ ] **Step 1.1: Write `diag/CMakeLists.txt`**

Contents (exactly):
```cmake
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(diag)
```

- [ ] **Step 1.2: Write `diag/sdkconfig.defaults`**

Contents (exactly):
```
CONFIG_ESP_MAIN_TASK_STACK_SIZE=4096
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y
```

The `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` line is in addition to what the spec lists. The XIAO ESP32-C3 exposes its USB port as the Serial-JTAG device, and the spec's own Interpretation section explicitly calls out checking this sdkconfig when logs don't appear. Setting it here makes that failure mode impossible.

- [ ] **Step 1.3: Write `diag/main/CMakeLists.txt`**

Contents (exactly):
```cmake
idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver)
```

`REQUIRES driver` is defensive: it links the legacy `driver` umbrella component which re-exports UART on ESP-IDF 5.x even after the 5.2 split to `esp_driver_uart`. If a build error complaining about `driver/uart.h` appears, swap `driver` for `esp_driver_uart`.

---

## Task 2: Implement `main.c`

**Files:**
- Create: `diag/main/main.c`

This is the entire firmware. One file, ~100 lines. Follow the spec exactly — no embellishment.

- [ ] **Step 2.1: Write `diag/main/main.c`**

Contents (exactly):
```c
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"

static const char *TAG = "diag";

#define PRINTER_UART    UART_NUM_1
#define PRINTER_TX_PIN  21     // C3 -> printer RX
#define PRINTER_RX_PIN  20     // C3 <- printer TX (unused)
#define PRINTER_BAUD    9600   // if garbage: rebuild with 19200
#define UART_BUF_SIZE   512

static void tx(const uint8_t *bytes, size_t len) {
    uart_write_bytes(PRINTER_UART, (const char *)bytes, len);
    uart_wait_tx_done(PRINTER_UART, pdMS_TO_TICKS(1000));
}

static void tx_str(const char *s) {
    tx((const uint8_t *)s, strlen(s));
}

static void run_pass(int num, const char *label,
                     uint8_t n1, uint8_t n2, uint8_t n3) {
    ESP_LOGI(TAG, "Pass %d: %s (n1=%u n2=%u n3=%u)",
             num, label, n1, n2, n3);

    // ESC 7 n1 n2 n3 — set heating parameters
    uint8_t esc7[] = { 0x1B, 0x37, n1, n2, n3 };
    tx(esc7, sizeof(esc7));
    vTaskDelay(pdMS_TO_TICKS(50));

    char banner[48];
    snprintf(banner, sizeof(banner), "=== PASS %d: %s ===\n", num, label);
    tx_str(banner);
    vTaskDelay(pdMS_TO_TICKS(50));

    tx_str("ABCDEFGHIJKLMNOP\n");
    vTaskDelay(pdMS_TO_TICKS(50));
    tx_str("0123456789!@#$%^\n");
    vTaskDelay(pdMS_TO_TICKS(50));
    tx_str("||||||||||||||||\n");
    vTaskDelay(pdMS_TO_TICKS(50));
    tx_str("################\n");
    vTaskDelay(pdMS_TO_TICKS(50));

    // Feed so output clears the tear bar
    tx_str("\n\n\n");
    vTaskDelay(pdMS_TO_TICKS(50));
}

void app_main(void) {
    ESP_LOGI(TAG, "CSN-A2 diagnostic firmware booting");

    const uart_config_t cfg = {
        .baud_rate  = PRINTER_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(PRINTER_UART, UART_BUF_SIZE,
                                        0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(PRINTER_UART, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(PRINTER_UART,
                                 PRINTER_TX_PIN, PRINTER_RX_PIN,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    ESP_LOGI(TAG, "UART1 up @ %d baud (TX=GPIO%d, RX=GPIO%d)",
             PRINTER_BAUD, PRINTER_TX_PIN, PRINTER_RX_PIN);

    // Let the printer settle after USB reset
    vTaskDelay(pdMS_TO_TICKS(2000));

    // ESC @ — reset printer state
    ESP_LOGI(TAG, "Sending ESC @ (reset)");
    const uint8_t esc_reset[] = { 0x1B, 0x40 };
    tx(esc_reset, sizeof(esc_reset));
    vTaskDelay(pdMS_TO_TICKS(100));

    run_pass(1, "DEFAULT",  7, 120, 40);
    vTaskDelay(pdMS_TO_TICKS(3000));
    run_pass(2, "MEDIUM",  11, 200, 20);
    vTaskDelay(pdMS_TO_TICKS(3000));
    run_pass(3, "MAXIMUM", 15, 255,  2);

    ESP_LOGI(TAG, "All passes complete; halting");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(60 * 1000));
    }
}
```

Key choices worth naming:
- `run_pass()` takes the label as a `const char *` and the three `ESC 7` operands as `uint8_t`. Spec values (7/120/40, 11/200/20, 15/255/2) all fit.
- Banner is built with `snprintf` into a 48-byte buffer — the longest banner ("=== PASS 3: MAXIMUM ===\n") is 25 bytes; 48 is comfortable.
- No loop around the three passes. Spec Note: "Do not let this firmware loop and repeat the aggressive passes."
- The final halt is `vTaskDelay(60000)` in a `while(1)` — does nothing but keeps the task alive so the idle task can run. Matches spec's "halt" language.

---

## Task 3: Build the firmware

**Files:** none modified — this task exists to gate on a clean compile before trying hardware.

- [ ] **Step 3.1: Set target to `esp32c3`**

Run:
```bash
cd /Users/myles/dev/little-printer/diag && idf.py set-target esp32c3
```
Expected: `set-target` regenerates `build/` and a fresh `sdkconfig` from `sdkconfig.defaults`. No errors. You'll see output ending with something like `-- Configuring done` and `-- Build files have been written to: .../build`.

- [ ] **Step 3.2: Build**

Run:
```bash
cd /Users/myles/dev/little-printer/diag && idf.py build
```
Expected: ends with `Project build complete. To flash, run this command:` and a line showing `esptool.py ... write_flash ...`. Artifact `build/diag.bin` exists.

If the build fails with a `driver/uart.h: No such file` or a link error about `uart_driver_install`, edit `diag/main/CMakeLists.txt` and change `REQUIRES driver` to `REQUIRES esp_driver_uart`, then re-run `idf.py build`.

- [ ] **Step 3.3: Sanity-check the binary size**

Run:
```bash
cd /Users/myles/dev/little-printer/diag && idf.py size | head -40
```
Expected: non-zero `.text` / `.rodata` sizes, total app image well under the default 1 MB app partition. Anything over ~300 KB of `.text` for this firmware means something unintended got linked in — stop and investigate.

---

## Task 4: Commit

**Files:** the four files created in Tasks 1–2, plus this plan and (implicitly) the `diagnostic-firmware-spec.md` and `SPEC.md` that were already untracked.

- [ ] **Step 4.1: Review what's about to be committed**

Run:
```bash
cd /Users/myles/dev/little-printer && git status && git diff --stat
```
Expected: untracked `diag/CMakeLists.txt`, `diag/sdkconfig.defaults`, `diag/main/CMakeLists.txt`, `diag/main/main.c`, `docs/superpowers/plans/2026-04-23-diagnostic-firmware.md`, plus the pre-existing untracked `diagnostic-firmware-spec.md` and `SPEC.md`. No untracked `build/`, `sdkconfig`, or `dependencies.lock` (those should be gitignored below).

- [ ] **Step 4.2: Add a `.gitignore` entry for the IDF build artifacts (if one doesn't exist)**

Run:
```bash
cd /Users/myles/dev/little-printer && ls -la .gitignore 2>/dev/null
```

If `.gitignore` does not exist, create it at repo root with:
```
# ESP-IDF build artifacts
diag/build/
diag/sdkconfig
diag/sdkconfig.old
diag/dependencies.lock
diag/managed_components/
```

If `.gitignore` exists, append the same block to it (check first; don't duplicate lines).

- [ ] **Step 4.3: Stage the intended files explicitly**

Run (pick only the four firmware files + plan + spec; explicitly skip anything in `diag/build/`):
```bash
cd /Users/myles/dev/little-printer && \
  git add .gitignore \
    diagnostic-firmware-spec.md \
    docs/superpowers/plans/2026-04-23-diagnostic-firmware.md \
    diag/CMakeLists.txt diag/sdkconfig.defaults \
    diag/main/CMakeLists.txt diag/main/main.c
```

Do **not** `git add -A` — that would pull in `diag/build/` and `SPEC.md`. `SPEC.md` is separate work; leave it untracked for now unless the user says otherwise.

- [ ] **Step 4.4: Commit**

Run:
```bash
cd /Users/myles/dev/little-printer && git commit -m "$(cat <<'EOF'
add CSN-A2 diagnostic firmware (diag/)

Standalone throwaway ESP-IDF project targeting XIAO ESP32-C3 that
fires three progressively-aggressive thermal-head test passes over
UART1 at 9600 baud, then halts. Used to determine post-over-voltage
whether any print head elements still heat.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```
Expected: commit succeeds, no pre-commit hooks in this repo yet.

- [ ] **Step 4.5: Confirm clean status**

Run:
```bash
cd /Users/myles/dev/little-printer && git status
```
Expected: working tree clean except for `SPEC.md` (still intentionally untracked) and possibly `diag/build/`, `diag/sdkconfig`, etc. (ignored).

---

## Task 5: Manual hardware test (user-driven — do not automate)

**Files:** none. This task is instructions for the user to run with hardware in hand; the agent should not attempt to flash or power the printer.

- [ ] **Step 5.1: Wire per spec**

- C3 GPIO21 → printer RX (green lead)
- C3 GPIO20 ← printer TX (yellow lead, wire it even though unused)
- C3 GND → printer data GND (black thin lead)
- Printer +V/GND (thick leads) → 9V 3A supply. **Do not** jumper the supply's GND to the C3 separately — the printer's data-GND pin is already tied to power GND internally.
- C3 powered via USB-C from the host.

- [ ] **Step 5.2: Flash and monitor**

Run:
```bash
cd /Users/myles/dev/little-printer/diag && idf.py -p /dev/cu.usbmodem* flash monitor
```
Expected: flash succeeds, monitor opens, and within ~2.5 seconds of boot you see log lines `CSN-A2 diagnostic firmware booting`, `UART1 up @ 9600 baud ...`, `Sending ESC @ (reset)`, then `Pass 1: DEFAULT ...`, `Pass 2: MEDIUM ...`, `Pass 3: MAXIMUM ...`, then `All passes complete; halting`. `Ctrl+]` exits the monitor.

- [ ] **Step 5.3: Interpret the paper output against the spec's Interpretation section**

See `diagnostic-firmware-spec.md` §Interpretation. Summary of decision points:

- All three passes blank → head fully dead → replace printer.
- Pass 3 faint where 1+2 are blank → partial damage → replace printer.
- All passes print cleanly → head OK; the earlier problem was elsewhere.
- Logs appear but no paper motion/sound at all → UART not reaching printer; check wiring and that `PRINTER_UART == UART_NUM_1`.
- Logs never appear → console mis-configured; verify `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` survived into `diag/sdkconfig`.

- [ ] **Step 5.4: (Conditional) Retry at 19200 baud**

Only if Pass 1 prints visible but corrupted/garbage characters (implying a baud mismatch, not a head failure):

Edit `diag/main/main.c` and change `#define PRINTER_BAUD 9600` to `#define PRINTER_BAUD 19200`, then rerun Task 3 (build) and Step 5.2 (flash/monitor). If output then prints cleanly, 19200 is the printer's configured rate. If still garbage at both, the issue is wiring or voltage, not baud.

---

## Self-review (done at plan-write time, not at execution)

**Spec coverage:**

- §Purpose / §Success criteria — Task 3 builds, Task 5 flashes, Task 5.3 observes marks. ✓
- §Hardware (wiring, baud) — Step 2.1 pins match (21 TX, 20 RX); Step 5.4 documents the 19200 fallback. ✓
- §Project structure — Task 1 creates the exact `diag/` + `main/` layout the spec calls for. ✓
- §Firmware behavior §`app_main()` — Step 2.1: UART init, 2s wait, ESC @, 100ms, three passes with 3s gaps, infinite halt. ✓
- §Test passes table — Step 2.1: `run_pass(1, "DEFAULT", 7, 120, 40)`, `(2, "MEDIUM", 11, 200, 20)`, `(3, "MAXIMUM", 15, 255, 2)`. Labels and operands match the spec table exactly. ✓
- §Test passes banner + stress rows + `\n\n\n` feed — Step 2.1: banner via `snprintf`, the four 16-char lines verbatim, three trailing newlines. ✓
- §UART write helper — Step 2.1 uses `tx()` / `tx_str()` verbatim. ✓
- §Logging — `ESP_LOGI` at boot, after UART up, before ESC @, before each pass, at halt. ✓
- §Flashing and running — Task 3 (`set-target` + `build`), Step 5.2 (`flash monitor`). ✓
- §Interpretation — Step 5.3 references the spec section directly so the diagnostic table stays single-source. ✓
- §Notes §"Do not loop" — Step 2.1: single fall-through then `while(1) vTaskDelay(60000)`; run_pass called exactly three times. ✓
- §Notes §"delete when done" — not in the plan's remit; user will remove `diag/` after diagnosis.

**Placeholder scan:** No "TBD", no "implement later", no "handle edge cases", no "similar to Task N". Every code block is complete. ✓

**Type / name consistency:** `run_pass(int, const char *, uint8_t, uint8_t, uint8_t)` is defined and called consistently (three call sites, all with matching arg types). `PRINTER_BAUD`, `PRINTER_UART`, `PRINTER_TX_PIN`, `PRINTER_RX_PIN`, `UART_BUF_SIZE` used consistently throughout. `tx()` signature matches spec verbatim. ✓

**Out-of-scope confirmation:** No Wi-Fi, no NVS, no SNTP, no HTTP client, no cJSON, no `thermal_printer` component, no Kconfig menu. ✓
