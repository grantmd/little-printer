#include "messages.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "cJSON.h"

#include "http_fetch.h"

static const char *TAG = "messages";

static const char *AUTH_HEADER = "Authorization";

static void make_bearer(char *buf, size_t buf_size) {
    snprintf(buf, buf_size, "Bearer %s", CONFIG_MESSAGES_TOKEN);
}

esp_err_t messages_fetch_pending(message_t **out, size_t *count) {
    *out = NULL;
    *count = 0;

    if (CONFIG_MESSAGES_TOKEN[0] == '\0') {
        ESP_LOGW(TAG, "MESSAGES_TOKEN is empty; skipping fetch");
        return ESP_FAIL;
    }

    char url[256];
    snprintf(url, sizeof(url), "%s/pending", CONFIG_MESSAGES_BASE_URL);

    char auth[128];
    make_bearer(auth, sizeof(auth));

    char *body = NULL;
    if (http_fetch_with_header(url, AUTH_HEADER, auth, &body) != ESP_OK) {
        return ESP_FAIL;
    }

    esp_err_t result = ESP_FAIL;
    cJSON *root = cJSON_Parse(body);
    if (!root || !cJSON_IsArray(root)) {
        ESP_LOGW(TAG, "expected top-level JSON array");
        goto done;
    }

    int n = cJSON_GetArraySize(root);
    if (n <= 0) {
        result = ESP_OK;  /* empty queue is success */
        goto done;
    }

    message_t *msgs = calloc((size_t)n, sizeof(message_t));
    if (!msgs) {
        ESP_LOGE(TAG, "calloc failed for %d messages", n);
        goto done;
    }

    for (int i = 0; i < n; i++) {
        cJSON *item = cJSON_GetArrayItem(root, i);
        if (!item) continue;
        cJSON *id      = cJSON_GetObjectItem(item, "id");
        cJSON *sender  = cJSON_GetObjectItem(item, "sender");
        cJSON *message = cJSON_GetObjectItem(item, "message");
        if (!id || !sender || !message) continue;

        msgs[i].id = id->valueint;
        if (cJSON_IsString(sender)) {
            strncpy(msgs[i].sender, sender->valuestring, sizeof(msgs[i].sender) - 1);
        }
        if (cJSON_IsString(message)) {
            strncpy(msgs[i].message, message->valuestring, sizeof(msgs[i].message) - 1);
        }
    }

    *out = msgs;
    *count = (size_t)n;
    result = ESP_OK;
    ESP_LOGI(TAG, "fetched %d pending message(s)", n);

done:
    cJSON_Delete(root);
    free(body);
    return result;
}

esp_err_t messages_confirm(const int *ids, size_t n) {
    if (n == 0) return ESP_OK;

    char url[256];
    snprintf(url, sizeof(url), "%s/confirm", CONFIG_MESSAGES_BASE_URL);

    char auth[128];
    make_bearer(auth, sizeof(auth));

    /* Build the JSON body: {"ids":[1,2,3]} */
    char body[256];
    int off = snprintf(body, sizeof(body), "{\"ids\":[");
    for (size_t i = 0; i < n; i++) {
        if (off >= (int)sizeof(body)) break;
        off += snprintf(body + off, sizeof(body) - off, "%s%d",
                        i == 0 ? "" : ",", ids[i]);
    }
    if (off < (int)sizeof(body)) {
        off += snprintf(body + off, sizeof(body) - off, "]}");
    }

    if (http_post_json(url, AUTH_HEADER, auth, body, NULL) != ESP_OK) {
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "confirmed %u message(s)", (unsigned)n);
    return ESP_OK;
}
