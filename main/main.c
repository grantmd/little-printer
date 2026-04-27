#include <stdio.h>

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
#include "weather.h"
#include "quote.h"

static const char *TAG = "main";

void app_main(void) {
    ESP_LOGI(TAG, "little-printer booting");

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Init the printer early so GPIO21 isn't floating during Wi-Fi connect. */
    ESP_ERROR_CHECK(thermal_printer_init(PRINTER_UART_NUM,
                                         PRINTER_TX_PIN,
                                         PRINTER_RX_PIN,
                                         CONFIG_PRINTER_BAUD));

    if (wifi_connect() != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi connect failed; halting");
        while (1) vTaskDelay(pdMS_TO_TICKS(60 * 1000));
    }
    time_sync_init();

    /* Fetch + log both APIs once. */
    weather_t w;
    if (weather_fetch(&w) == ESP_OK) {
        ESP_LOGI(TAG, "WEATHER: %dF, %s, wind %dmph",
                 w.temp_f, w.description, w.wind_mph);
    } else {
        ESP_LOGW(TAG, "WEATHER: fetch failed");
    }

    quote_t q;
    if (quote_fetch(&q) == ESP_OK) {
        ESP_LOGI(TAG, "QUOTE: \"%s\" — %s", q.body, q.author);
    } else {
        ESP_LOGW(TAG, "QUOTE: fetch failed");
    }

    ESP_LOGI(TAG, "API test complete; idling");
    while (1) vTaskDelay(pdMS_TO_TICKS(60 * 1000));
}
