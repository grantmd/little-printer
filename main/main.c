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
#include "briefing.h"
#include "messages.h"
#include "printer_lock.h"

static const char *TAG = "main";

static void console_task(void *arg) {
    while (1) {
        int c = fgetc(stdin);
        if (c == 'p' || c == 'P') {
            ESP_LOGI(TAG, "console: manual briefing trigger");
            briefing_run();
        } else if (c == 'm' || c == 'M') {
            ESP_LOGI(TAG, "console: manual message poll");
            messages_print_pending();
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void briefing_task(void *arg) {
    int last_printed_yday = -1;
    while (1) {
        time_t now = time(NULL);
        struct tm lt;
        localtime_r(&now, &lt);

        bool should_print =
            lt.tm_hour == CONFIG_PRINT_HOUR &&
            lt.tm_min  == CONFIG_PRINT_MINUTE &&
            lt.tm_yday != last_printed_yday;

        if (should_print) {
            ESP_LOGI(TAG, "scheduled briefing trigger");
            briefing_run();
            last_printed_yday = lt.tm_yday;
            time_sync_refresh();
        }

        vTaskDelay(pdMS_TO_TICKS(30 * 1000));
    }
}

static void messages_task(void *arg) {
    int last_fired_hour = -1;
    int last_fired_yday = -1;
    while (1) {
        time_t now = time(NULL);
        struct tm lt;
        localtime_r(&now, &lt);

        bool in_window = (lt.tm_hour >= CONFIG_MESSAGES_START_HOUR &&
                          lt.tm_hour <  CONFIG_MESSAGES_END_HOUR);
        bool fresh_slot = (lt.tm_hour != last_fired_hour ||
                           lt.tm_yday != last_fired_yday);

        if (in_window && lt.tm_min == 0 && fresh_slot) {
            ESP_LOGI(TAG, "scheduled message poll");
            messages_print_pending();
            last_fired_hour = lt.tm_hour;
            last_fired_yday = lt.tm_yday;
        }

        vTaskDelay(pdMS_TO_TICKS(30 * 1000));
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "little-printer booting");

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    printer_lock_init();

    /* Init the printer early so GPIO21 isn't floating during Wi-Fi connect. */
    ESP_ERROR_CHECK(thermal_printer_init(PRINTER_UART_NUM,
                                         PRINTER_TX_PIN,
                                         PRINTER_RX_PIN,
                                         CONFIG_PRINTER_BAUD));

    if (wifi_connect() != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi connect failed; continuing offline");
    } else {
        time_sync_init();
    }

    xTaskCreate(console_task,  "console",  8192, NULL, 5, NULL);
    xTaskCreate(briefing_task, "briefing", 8192, NULL, 4, NULL);
    xTaskCreate(messages_task, "messages", 8192, NULL, 4, NULL);

    ESP_LOGI(TAG, "ready — briefing %02d:%02d, messages hourly %02d:00–%02d:00",
             CONFIG_PRINT_HOUR, CONFIG_PRINT_MINUTE,
             CONFIG_MESSAGES_START_HOUR, CONFIG_MESSAGES_END_HOUR);
    while (1) vTaskDelay(pdMS_TO_TICKS(60 * 1000));
}
