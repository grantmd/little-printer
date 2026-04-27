#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"

#include "config.h"
#include "thermal_printer.h"

static const char *TAG = "main";

void app_main(void) {
    ESP_LOGI(TAG, "little-printer booting");

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(thermal_printer_init(PRINTER_UART_NUM,
                                         PRINTER_TX_PIN,
                                         PRINTER_RX_PIN,
                                         CONFIG_PRINTER_BAUD));

    thermal_printer_set_justify('C');
    thermal_printer_println("hello, world");
    thermal_printer_feed(3);

    ESP_LOGI(TAG, "boot complete; idling");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(60 * 1000));
    }
}
