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
