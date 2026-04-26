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
    ESP_LOGI(TAG, "Pass %d: %s (n1=%d n2=%d n3=%d)",
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
    ESP_LOGI(TAG, "MC206H acceptance firmware booting");

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
