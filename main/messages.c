#include "messages.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "cJSON.h"

#include "config.h"
#include "http_fetch.h"
#include "printer_lock.h"
#include "text_wrap.h"
#include "thermal_printer.h"

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

/* Local copy of briefing.c's helper — kept simple to avoid sharing
 * via a header, since it's only 4 lines. */
static void println_indented(const char *line) {
    char buf[80];
    snprintf(buf, sizeof(buf), "  %s", line);
    thermal_printer_println(buf);
}

esp_err_t messages_print_pending(void) {
    /* Pre-flight: is the printer alive and ready? */
    xSemaphoreTake(s_print_mutex, portMAX_DELAY);
    thermal_printer_status_t pstatus;
    esp_err_t qerr = thermal_printer_query_status(&pstatus);
    xSemaphoreGive(s_print_mutex);

    if (qerr != ESP_OK) {
        ESP_LOGW(TAG, "printer not responding; deferring messages");
        return ESP_FAIL;
    }
    if (pstatus.paper_end) {
        ESP_LOGW(TAG, "printer out of paper; deferring messages");
        return ESP_FAIL;
    }
    if (pstatus.paper_near_end) {
        ESP_LOGW(TAG, "printer paper near end — printing anyway");
    }

    /* Fetch + print + confirm flow. */
    message_t *msgs = NULL;
    size_t n = 0;
    if (messages_fetch_pending(&msgs, &n) != ESP_OK) {
        free(msgs);
        return ESP_FAIL;
    }
    if (n == 0) {
        free(msgs);
        return ESP_OK;
    }

    /* Take the printer mutex for the whole print so we don't interleave
     * with briefing_run if both fire in the same minute. */
    xSemaphoreTake(s_print_mutex, portMAX_DELAY);

    int ids[8];
    size_t to_confirm = n > 8 ? 8 : n;
    for (size_t i = 0; i < to_confirm; i++) {
        thermal_printer_set_justify('L');

        /* Split on \n so user-entered paragraph breaks are preserved.
         * text_wrap collapses internal whitespace within each paragraph;
         * splitting here is what makes multi-line messages render as
         * multi-line prints. \r before \n (browsers send CRLF) is stripped. */
        const char *p = msgs[i].message;
        while (*p) {
            const char *eol = p;
            while (*eol && *eol != '\n') eol++;

            size_t plen = (size_t)(eol - p);
            if (plen > 0 && p[plen - 1] == '\r') plen--;

            char para[320];
            if (plen >= sizeof(para)) plen = sizeof(para) - 1;
            memcpy(para, p, plen);
            para[plen] = '\0';

            if (para[0] == '\0') {
                thermal_printer_println("");
            } else {
                text_wrap(para, PRINT_LINE_WIDTH - 4, &println_indented);
            }

            p = (*eol == '\n') ? eol + 1 : eol;
        }

        char attribution[48];
        snprintf(attribution, sizeof(attribution), "       -- %s", msgs[i].sender);
        thermal_printer_println(attribution);
        thermal_printer_feed(1);
        ids[i] = msgs[i].id;
    }
    thermal_printer_feed(3);
    thermal_printer_sleep(60);

    xSemaphoreGive(s_print_mutex);

    esp_err_t err = messages_confirm(ids, to_confirm);
    free(msgs);
    return err;
}
