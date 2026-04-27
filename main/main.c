#include <stdio.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"

#include "config.h"
#include "thermal_printer.h"
#include "wifi.h"
#include "time_sync.h"

static const char *TAG = "main";

void app_main(void) {
    ESP_LOGI(TAG, "little-printer booting");

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    if (wifi_connect() != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi connect failed; continuing without time sync");
    } else {
        time_sync_init();
    }

    ESP_ERROR_CHECK(thermal_printer_init(PRINTER_UART_NUM,
                                         PRINTER_TX_PIN,
                                         PRINTER_RX_PIN,
                                         CONFIG_PRINTER_BAUD));

    /* Boot one-liner so the paper confirms the device is alive. */
    char line[64];
    time_t now = time(NULL);
    struct tm lt;
    localtime_r(&now, &lt);
    snprintf(line, sizeof(line), "booted %02d:%02d - briefing at %02d:%02d",
             lt.tm_hour, lt.tm_min, CONFIG_PRINT_HOUR, CONFIG_PRINT_MINUTE);
    thermal_printer_set_justify('C');
    thermal_printer_println(line);
    thermal_printer_feed(3);

    /* Log local time every 30s so we can confirm DST behavior over time. */
    while (1) {
        time_t t = time(NULL);
        struct tm tt;
        localtime_r(&t, &tt);
        ESP_LOGI(TAG, "local: %04d-%02d-%02d %02d:%02d:%02d",
                 tt.tm_year + 1900, tt.tm_mon + 1, tt.tm_mday,
                 tt.tm_hour, tt.tm_min, tt.tm_sec);
        vTaskDelay(pdMS_TO_TICKS(30 * 1000));
    }
}
