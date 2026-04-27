#include "printer_lock.h"

#include "esp_log.h"

static const char *TAG = "printer_lock";

SemaphoreHandle_t s_print_mutex = NULL;

void printer_lock_init(void) {
    s_print_mutex = xSemaphoreCreateMutex();
    if (!s_print_mutex) {
        ESP_LOGE(TAG, "failed to create printer mutex");
    }
}
