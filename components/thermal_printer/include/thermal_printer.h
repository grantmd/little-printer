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
