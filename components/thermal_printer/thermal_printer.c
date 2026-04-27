#include "thermal_printer.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "thermal_printer";

static uart_port_t s_uart = UART_NUM_1;

static void tx(const uint8_t *bytes, size_t len) {
    uart_write_bytes(s_uart, (const char *)bytes, len);
    uart_wait_tx_done(s_uart, pdMS_TO_TICKS(1000));
}

esp_err_t thermal_printer_init(uart_port_t uart_num,
                               int tx_pin,
                               int rx_pin,
                               int baud) {
    s_uart = uart_num;

    /*
     * Idle the TX line HIGH before configuring the UART. Otherwise the pin
     * floats during boot ROM execution; the printer reads the line noise
     * as ESC/POS bytes and prints garbage on cold start. (Soft reset is
     * unaffected because the pin keeps its UART config across reset.)
     */
    gpio_reset_pin(tx_pin);
    gpio_set_direction(tx_pin, GPIO_MODE_OUTPUT);
    gpio_set_level(tx_pin, 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    const uart_config_t cfg = {
        .baud_rate  = baud,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_driver_install(uart_num, 512, 0, 0, NULL, 0);
    if (err != ESP_OK) return err;
    err = uart_param_config(uart_num, &cfg);
    if (err != ESP_OK) return err;
    err = uart_set_pin(uart_num, tx_pin, rx_pin,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) return err;

    /* Let the printer settle after USB reset. */
    vTaskDelay(pdMS_TO_TICKS(2000));

    thermal_printer_reset();

    /* Heating defaults validated by diag/ acceptance on this MC206H. */
    const uint8_t esc7[] = { 0x1B, 0x37, 11, 120, 40 };
    tx(esc7, sizeof(esc7));
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_LOGI(TAG, "initialised on UART%d @ %d baud", uart_num, baud);
    return ESP_OK;
}

void thermal_printer_reset(void) {
    const uint8_t esc_at[] = { 0x1B, 0x40 };
    tx(esc_at, sizeof(esc_at));
    vTaskDelay(pdMS_TO_TICKS(100));
}

void thermal_printer_set_bold(bool on) {
    const uint8_t b[] = { 0x1B, 0x45, on ? (uint8_t)1 : (uint8_t)0 };
    tx(b, sizeof(b));
}

void thermal_printer_set_justify(char j) {
    uint8_t n;
    switch (j) {
        case 'C': case 'c': n = 1; break;
        case 'R': case 'r': n = 2; break;
        case 'L': case 'l':
        default:            n = 0; break;
    }
    const uint8_t b[] = { 0x1B, 0x61, n };
    tx(b, sizeof(b));
}

void thermal_printer_set_size(char s) {
    uint8_t n;
    switch (s) {
        case 'M': case 'm': n = 0x10; break;  /* double height */
        case 'L': case 'l': n = 0x11; break;  /* double both */
        case 'S': case 's':
        default:            n = 0x00; break;  /* normal */
    }
    const uint8_t b[] = { 0x1D, 0x21, n };
    tx(b, sizeof(b));
}

void thermal_printer_print(const char *text) {
    if (!text) return;
    tx((const uint8_t *)text, strlen(text));
}

void thermal_printer_println(const char *text) {
    if (text) {
        tx((const uint8_t *)text, strlen(text));
    }
    const uint8_t lf = 0x0A;
    tx(&lf, 1);
    /* Light pacing — at 9600 baud, 32 chars take ~33ms to clear. */
    vTaskDelay(pdMS_TO_TICKS(20));
}

void thermal_printer_feed(uint8_t lines) {
    const uint8_t b[] = { 0x1B, 0x64, lines };
    tx(b, sizeof(b));
    vTaskDelay(pdMS_TO_TICKS(50 * (lines ? lines : 1)));
}

void thermal_printer_sleep(uint16_t seconds) {
    const uint8_t b[] = { 0x1B, 0x38,
                          (uint8_t)(seconds & 0xFF),
                          (uint8_t)((seconds >> 8) & 0xFF) };
    tx(b, sizeof(b));
}

void thermal_printer_print_bitmap(uint16_t width_bytes,
                                  uint16_t height,
                                  const uint8_t *data) {
    if (!data || width_bytes == 0 || height == 0) return;

    /* GS v 0 m xL xH yL yH ... — m=0 (normal density). */
    const uint8_t header[] = {
        0x1D, 0x76, 0x30, 0x00,
        (uint8_t)(width_bytes & 0xFF), (uint8_t)((width_bytes >> 8) & 0xFF),
        (uint8_t)(height & 0xFF),       (uint8_t)((height >> 8) & 0xFF),
    };
    tx(header, sizeof(header));

    /* Send the pixel data in chunks so we don't overrun the printer's
     * input buffer at 9600 baud. */
    const size_t total = (size_t)width_bytes * height;
    const size_t chunk = 64;
    for (size_t off = 0; off < total; off += chunk) {
        size_t n = (total - off > chunk) ? chunk : (total - off);
        tx(data + off, n);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    vTaskDelay(pdMS_TO_TICKS(80));
}
