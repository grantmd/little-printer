#include "quote.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "cJSON.h"

#include "http_fetch.h"

static const char *TAG = "quote";

/* The quote API can occasionally connect-timeout from the device, so
 * quote_fetch retries a few times before giving up. See quote_fetch() below. */
#define QUOTE_FETCH_ATTEMPTS  3
#define QUOTE_RETRY_DELAY_MS  2000

static esp_err_t quote_fetch_once(quote_t *out) {
    char *body = NULL;
    if (http_fetch("https://dummyjson.com/quotes/random", &body) != ESP_OK) {
        return ESP_FAIL;
    }

    esp_err_t result = ESP_FAIL;
    cJSON *root = cJSON_Parse(body);
    if (!root || !cJSON_IsObject(root)) {
        ESP_LOGW(TAG, "expected top-level JSON object");
        goto done;
    }

    cJSON *q = cJSON_GetObjectItem(root, "quote");
    cJSON *a = cJSON_GetObjectItem(root, "author");
    if (!cJSON_IsString(q) || !cJSON_IsString(a)) {
        ESP_LOGW(TAG, "missing 'quote' or 'author'");
        goto done;
    }

    strncpy(out->body, q->valuestring, sizeof(out->body) - 1);
    out->body[sizeof(out->body) - 1] = '\0';
    strncpy(out->author, a->valuestring, sizeof(out->author) - 1);
    out->author[sizeof(out->author) - 1] = '\0';

    ESP_LOGI(TAG, "fetched quote by %s", out->author);
    result = ESP_OK;

done:
    cJSON_Delete(root);
    free(body);
    return result;
}

esp_err_t quote_fetch(quote_t *out) {
    for (int attempt = 1; attempt <= QUOTE_FETCH_ATTEMPTS; attempt++) {
        if (quote_fetch_once(out) == ESP_OK) {
            return ESP_OK;
        }
        ESP_LOGW(TAG, "quote fetch attempt %d/%d failed", attempt, QUOTE_FETCH_ATTEMPTS);
        if (attempt < QUOTE_FETCH_ATTEMPTS) {
            vTaskDelay(pdMS_TO_TICKS(QUOTE_RETRY_DELAY_MS));
        }
    }
    return ESP_FAIL;
}
