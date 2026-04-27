#include "time_sync.h"

#include <time.h>

#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"

static const char *TAG = "time_sync";

esp_err_t time_sync_init(void) {
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_err_t err = esp_netif_sntp_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "sntp_init: %s", esp_err_to_name(err));
        return err;
    }

    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(15 * 1000)) != ESP_OK) {
        ESP_LOGW(TAG, "first NTP sync timed out — continuing");
    }

    /* America/Los_Angeles with DST: starts 2nd Sun of March, ends 1st Sun of November. */
    setenv("TZ", "PST8PDT,M3.2.0,M11.1.0", 1);
    tzset();

    time_t now = time(NULL);
    struct tm lt;
    localtime_r(&now, &lt);
    ESP_LOGI(TAG, "local time: %04d-%02d-%02d %02d:%02d:%02d",
             lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday,
             lt.tm_hour, lt.tm_min, lt.tm_sec);

    return ESP_OK;
}

void time_sync_refresh(void) {
    /* Non-blocking re-sync. */
    esp_sntp_restart();
}
