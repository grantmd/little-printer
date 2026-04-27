#pragma once

#include <stddef.h>
#include "esp_err.h"

/*
 * GET `url` over HTTPS and return the response body as a heap-allocated
 * NUL-terminated C string. Caller must free() the result on success.
 *
 * Returns ESP_OK on a 2xx response, ESP_FAIL otherwise. *out is set to
 * NULL on failure.
 */
esp_err_t http_fetch(const char *url, char **out);
