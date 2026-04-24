# CSN-A2 Diagnostic Firmware

## Purpose

Minimal ESP-IDF firmware for XIAO ESP32-C3 that sends aggressive print commands to a CSN-A2 thermal printer to determine whether any heating elements in the print head are still functional after suspected over-voltage damage.

**This is throwaway diagnostic code**, not the start of the main project. Do not pull in weather APIs, Wi-Fi, scheduling, or any of the scope from `SPEC.md`. Target: smallest possible firmware that exercises the printer with maximum heat.

## Success criteria

- Firmware builds with `idf.py build` and flashes via `idf.py flash`.
- On boot, printer attempts to print a sequence of test patterns with progressively more aggressive heating parameters.
- User can observe whether any marks appear on paper.

## Hardware

- **XIAO ESP32-C3** powered via USB-C
- **CSN-A2 thermal printer** powered by its own 9V 3A supply
- Wiring:
  - C3 **GPIO21** → printer RX (data)
  - C3 **GPIO20** ← printer TX (data, unused but wire it)
  - C3 **GND** → printer data GND (which is internally tied to power GND — do NOT also run a wire to the 9V supply ground)
- Printer baud rate: **try 9600 first**. If garbage / nothing, rebuild with 19200 and try again. The baud is set at `uart_config_t.baud_rate` — easy to change.

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

- **All three passes fully blank:** head is dead across all elements. Replace the printer.
- **Pass 3 shows partial/faint marks that 1 and 2 don't:** some elements survive but are damaged. Printer is technically working but won't produce legible output — replace.
- **All three passes print clean text:** head is fine, the earlier issues were something else (paper, voltage, cabling). Unexpected given history, but possible.
- **Nothing prints and `ESP_LOGI` also shows nothing on serial:** firmware isn't running or USB-CDC console isn't set up. Check `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` in sdkconfig.
- **Log messages appear but no print activity (no paper motion, no sound):** UART isn't reaching the printer. Check TX wiring and that you're on UART1, not UART0 (UART0 is the USB console on the C3).

## Notes

- Do NOT send `ESC 7` with values higher than listed — `n2=255` and `n3=2` are already at the edge of what the firmware accepts and are intended as a last-ditch diagnostic.
- Do not let this firmware loop and repeat the aggressive passes. One cycle and halt. Repeated max-heat firing on an already-damaged head is pointless and potentially worsens failure modes.
- Once diagnosis is complete, this project should be deleted. The real project uses `SPEC.md`.
