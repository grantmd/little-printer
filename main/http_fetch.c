#include "http_fetch.h"

#include <stdlib.h>
#include <string.h>

#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"

static const char *TAG = "http_fetch";

#define MAX_RESPONSE_BYTES (16 * 1024)

typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
} body_t;

static esp_err_t event_handler(esp_http_client_event_t *evt) {
    body_t *body = (body_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        if (body->len + evt->data_len + 1 > body->cap) {
            size_t new_cap = body->cap ? body->cap * 2 : 1024;
            while (new_cap < body->len + evt->data_len + 1) new_cap *= 2;
            if (new_cap > MAX_RESPONSE_BYTES) {
                ESP_LOGE(TAG, "response exceeded %d bytes; aborting", MAX_RESPONSE_BYTES);
                return ESP_FAIL;
            }
            char *grown = realloc(body->buf, new_cap);
            if (!grown) return ESP_ERR_NO_MEM;
            body->buf = grown;
            body->cap = new_cap;
        }
        memcpy(body->buf + body->len, evt->data, evt->data_len);
        body->len += evt->data_len;
        body->buf[body->len] = '\0';
    }
    return ESP_OK;
}

esp_err_t http_fetch(const char *url, char **out) {
    *out = NULL;

    body_t body = { 0 };

    esp_http_client_config_t cfg = {
        .url = url,
        .event_handler = event_handler,
        .user_data = &body,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10 * 1000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        free(body.buf);
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_perform(client);
    int status = err == ESP_OK ? esp_http_client_get_status_code(client) : -1;
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status < 200 || status >= 300) {
        ESP_LOGW(TAG, "fetch failed: err=%s status=%d url=%s",
                 esp_err_to_name(err), status, url);
        free(body.buf);
        return ESP_FAIL;
    }

    if (!body.buf) {
        /* 2xx with empty body — unusual but treat as success with empty string. */
        body.buf = calloc(1, 1);
        if (!body.buf) return ESP_ERR_NO_MEM;
    }

    *out = body.buf;
    return ESP_OK;
}
