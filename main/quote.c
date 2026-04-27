#include "quote.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "cJSON.h"

#include "http_fetch.h"

static const char *TAG = "quote";

esp_err_t quote_fetch(quote_t *out) {
    char *body = NULL;
    if (http_fetch("https://zenquotes.io/api/random", &body) != ESP_OK) {
        return ESP_FAIL;
    }

    esp_err_t result = ESP_FAIL;
    cJSON *root = cJSON_Parse(body);
    if (!root || !cJSON_IsArray(root)) {
        ESP_LOGW(TAG, "expected top-level JSON array");
        goto done;
    }

    cJSON *first = cJSON_GetArrayItem(root, 0);
    if (!first) {
        ESP_LOGW(TAG, "empty array");
        goto done;
    }

    cJSON *q = cJSON_GetObjectItem(first, "q");
    cJSON *a = cJSON_GetObjectItem(first, "a");
    if (!q || !a || !cJSON_IsString(q) || !cJSON_IsString(a)) {
        ESP_LOGW(TAG, "missing 'q' or 'a'");
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
