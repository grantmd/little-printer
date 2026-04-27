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

/*
 * Like http_fetch but also sends a single extra request header (e.g.
 * "Authorization: Bearer ..."). Pass NULL for header_name/header_value
 * to behave identically to http_fetch.
 */
esp_err_t http_fetch_with_header(const char *url,
                                 const char *header_name,
                                 const char *header_value,
                                 char **out);

/*
 * POST `body` to `url` with Content-Type: application/json and an optional
 * extra header. `out` may be NULL if the caller doesn't need the response
 * body. Returns ESP_OK on a 2xx response.
 */
esp_err_t http_post_json(const char *url,
                         const char *header_name,
                         const char *header_value,
                         const char *body,
                         char **out);
