#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"

static const char *TAG = "main";

void app_main(void) {
    ESP_LOGI(TAG, "little-printer booting");

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_LOGI(TAG, "boot complete; idling");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(60 * 1000));
    }
}
