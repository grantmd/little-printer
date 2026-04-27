#pragma once

#include "esp_err.h"

/*
 * Connect to Wi-Fi using credentials from Kconfig (CONFIG_WIFI_SSID /
 * CONFIG_WIFI_PASSWORD). Blocks until either connected (returns ESP_OK)
 * or all retry attempts have been exhausted (returns ESP_FAIL).
 *
 * Caller must have already run nvs_flash_init() and esp_netif_init() and
 * created the default event loop.
 */
esp_err_t wifi_connect(void);

/*
 * True iff a connection has been established and we currently have an IP.
 */
bool wifi_is_connected(void);
