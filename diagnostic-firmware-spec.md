# MC206H Acceptance Test Firmware

## Purpose

Minimal ESP-IDF firmware for XIAO ESP32-C3 that exercises a newly-acquired MC206H thermal printer with standard ESC/POS commands at three progressively more aggressive heating-parameter presets, to verify the printer is wired correctly, speaks the expected protocol, and produces legible output before building the full briefing firmware on top.

This is throwaway acceptance-test code, not the start of the main project. Do not pull in weather APIs, Wi-Fi, scheduling, or any of the scope from `SPEC.md`. Target: smallest possible firmware that exercises the printer.

## Success criteria

- Firmware builds with `idf.py build` and flashes via `idf.py flash`.
- On boot, printer attempts to print a sequence of test patterns with progressively more aggressive heating parameters.
- User can observe whether any marks appear on paper.

## Hardware

- **XIAO ESP32-C3** powered via USB-C
- **MC206H thermal printer** powered by its own external supply (verify voltage/current spec on the unit's label or datasheet — typical for this class is 5–9V at 2A peak)
- Wiring:
  - C3 **GPIO21** → printer RX (data)
  - C3 **GPIO20** ← printer TX (data, unused but wire it)
  - C3 **GND** → printer data GND (which is internally tied to power GND — do NOT also run a wire to the 9V supply ground)
- Printer baud rate: **9600** (confirmed via the MC206H self-test on this unit). If you swap the printer, re-run the self-test and update `uart_config_t.baud_rate` in `main/main.c` if it differs.

## Pre-flight

Before connecting any wires to the C3, verify the printer is in a state the firmware expects:

1. With the printer's data leads disconnected, hold the **FEED** button while connecting power. The printer will print a configuration page.
2. Confirm the page reports:
   - **Command mode:** `EPSON(ESC/POS)` — anything else means the firmware's command bytes won't work.
   - **Interface:** includes `TTL` — RS-232 mode would damage the C3's GPIOs.
   - **Baud:** `9600, 8N1` — anything else means update `uart_config_t.baud_rate` in `main/main.c` and rebuild before flashing.
3. If any of those don't match, do not proceed to wiring until you've reconfigured the printer (DIP switch / button combo / different baud) or updated the firmware to match.

This particular MC206H was verified against these criteria on 2026-04-26.

## Project structure

Standard minimal IDF layout:

```
diag/
├── CMakeLists.txt
├── sdkconfig.defaults
└── main/
    ├── CMakeLists.txt
    └── main.c
```

`sdkconfig.defaults` only needs:
```
CONFIG_ESP_MAIN_TASK_STACK_SIZE=4096
```

## Firmware behavior

### `app_main()`

1. Initialize UART1 at 9600 baud, 8N1, no flow control, TX=GPIO21, RX=GPIO20.
2. Wait 2 seconds (let printer settle after USB reset).
3. Send `ESC @` (`0x1B 0x40`) to reset printer state.
4. Wait 100ms.
5. Run three test passes with increasingly aggressive heating parameters (see below).
6. Between passes, wait 3 seconds to let the user visually inspect output.
7. After all passes, blink nothing / just halt (`while(1) vTaskDelay(...)`).

### Test passes

Each pass sends `ESC 7 n1 n2 n3` (`0x1B 0x37 n1 n2 n3`) to set heating parameters, then prints a recognizable test string followed by `\n` line feeds.

| Pass | n1 (max dots) | n2 (heating time, ×10µs) | n3 (heating interval, ×10µs) | Label |
|------|---------------|--------------------------|------------------------------|-------|
| 1    | 7             | 120                      | 40                           | "PASS 1: DEFAULT" |
| 2    | 11            | 200                      | 20                           | "PASS 2: MEDIUM"  |
| 3    | 15            | 255                      | 2                            | "PASS 3: MAXIMUM" |

After each parameter set, print:

```
=== PASS N: LABEL ===
ABCDEFGHIJKLMNOP
0123456789!@#$%^
||||||||||||||||
################
```

Then `\n\n\n` to feed the paper enough that the output is visible past the tear bar.

The pipe and hash lines are useful because they exercise nearly every heating element at once — any working element will leave a visible mark.

### UART write helper

```c
static void tx(const uint8_t *bytes, size_t len) {
    uart_write_bytes(UART_NUM_1, (const char *)bytes, len);
    uart_wait_tx_done(UART_NUM_1, pdMS_TO_TICKS(1000));
}

static void tx_str(const char *s) {
    tx((const uint8_t *)s, strlen(s));
}
```

Use `tx()` for command byte sequences, `tx_str()` for the text lines. Add a small `vTaskDelay(pdMS_TO_TICKS(50))` between logical chunks to avoid flooding the printer's input buffer.

### Logging

Use `ESP_LOGI(TAG, ...)` liberally — log each pass before sending it, so if nothing prints, the serial console will at least confirm the firmware is running and reaching each step.

## Flashing and running

```bash
idf.py set-target esp32c3
idf.py build
idf.py -p /dev/cu.usbmodem* flash monitor
```

If the printer is on the correct supply and wired correctly, within ~30 seconds after flash completes you should see three test blocks print (or not).

## Interpretation

Expected outcomes for a healthy MC206H:

- **All three passes print legibly:** acceptance complete. Note which pass produced the cleanest, sharpest output — those `n1/n2/n3` heating values become the recommended defaults for `SPEC.md`'s `thermal_printer_init` and the briefing firmware.
- **Pass 1 too light, passes 2/3 better:** unit needs hotter defaults than typical. Use pass 2 or 3's values.
- **Marks scattered or missing on every pass:** likely a wiring or supply problem, not a printer fault. Re-check TX/RX swap, common ground, supply current capacity. Re-run after correcting.
- **Paper advances but no marks at all on any pass:** unexpected given the pre-flight passed. Most likely a damaged head or seriously wrong wiring. Investigate before drawing protocol conclusions.
- **Paper doesn't advance and `ESP_LOGI` shows nothing on serial:** firmware isn't running or USB-CDC console isn't set up. Check `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` in sdkconfig.
- **Log messages appear but no print activity (no paper motion, no sound):** UART isn't reaching the printer. Check TX wiring and that you're on UART1 (UART0 is the USB console on the C3).

## Notes

- Do NOT send `ESC 7` with values higher than listed — `n2=255` and `n3=2` are already at the edge of what the firmware accepts and are intended as a last-ditch diagnostic.
- Do not let this firmware loop and repeat the aggressive passes. One cycle and halt. Repeated max-heat firing on an already-damaged head is pointless and potentially worsens failure modes.
- Once acceptance is complete, this firmware can stay in the repo as a smoke test for future hardware swaps, or be deleted. The real project uses `SPEC.md`.
