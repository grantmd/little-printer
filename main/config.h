#pragma once
#include "driver/uart.h"

#define LOCATION_NAME  "San Carlos, CA"
#define LOCATION_LAT   37.5072
#define LOCATION_LON  -122.2605

#define PRINTER_UART_NUM    UART_NUM_1
#define PRINTER_TX_PIN      21    /* D6 — goes to printer RX */
#define PRINTER_RX_PIN      20    /* D7 — from printer TX */

#define PRINT_LINE_WIDTH    32    /* 58mm paper @ small font */
