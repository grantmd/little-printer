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

/*
 * Print a raster bitmap via ESC/POS GS v 0. `width_bytes` is the row
 * stride in bytes — image width in pixels divided by 8, rounded up.
 * `height` is the number of rows. Data is row-major, MSB-first
 * horizontally (bit 7 of byte 0 is the leftmost pixel of row 0). 1 = ink.
 */
void thermal_printer_print_bitmap(uint16_t width_bytes,
                                  uint16_t height,
                                  const uint8_t *data);

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
