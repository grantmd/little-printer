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

static esp_err_t do_request(esp_http_client_method_t method,
                            const char *url,
                            const char *header_name,
                            const char *header_value,
                            const char *content_type,
                            const char *body_str,
                            char **out) {
    if (out) *out = NULL;

    body_t body = { 0 };

    esp_http_client_config_t cfg = {
        .url = url,
        .event_handler = event_handler,
        .user_data = &body,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10 * 1000,
        .method = method,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        free(body.buf);
        return ESP_FAIL;
    }

    if (header_name && header_value) {
        esp_http_client_set_header(client, header_name, header_value);
    }
    if (content_type) {
        esp_http_client_set_header(client, "Content-Type", content_type);
    }
    if (body_str) {
        esp_http_client_set_post_field(client, body_str, strlen(body_str));
    }

    esp_err_t err = esp_http_client_perform(client);
    int status = err == ESP_OK ? esp_http_client_get_status_code(client) : -1;
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status < 200 || status >= 300) {
        ESP_LOGW(TAG, "%s failed: err=%s status=%d url=%s",
                 method == HTTP_METHOD_POST ? "POST" : "GET",
                 esp_err_to_name(err), status, url);
        free(body.buf);
        return ESP_FAIL;
    }

    if (out) {
        if (!body.buf) {
            body.buf = calloc(1, 1);
            if (!body.buf) return ESP_ERR_NO_MEM;
        }
        *out = body.buf;
    } else {
        free(body.buf);
    }
    return ESP_OK;
}

esp_err_t http_fetch(const char *url, char **out) {
    return do_request(HTTP_METHOD_GET, url, NULL, NULL, NULL, NULL, out);
}

esp_err_t http_fetch_with_header(const char *url,
                                 const char *header_name,
                                 const char *header_value,
                                 char **out) {
    return do_request(HTTP_METHOD_GET, url, header_name, header_value, NULL, NULL, out);
}

esp_err_t http_post_json(const char *url,
                         const char *header_name,
                         const char *header_value,
                         const char *body,
                         char **out) {
    return do_request(HTTP_METHOD_POST, url, header_name, header_value,
                      "application/json", body, out);
}
