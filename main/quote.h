#pragma once

#include "esp_err.h"

typedef struct {
    char body[256];    /* the quote text */
    char author[64];
} quote_t;

/*
 * Fetch a random inspirational quote from ZenQuotes. On ESP_OK, *out is
 * fully populated. On failure, *out is left untouched.
 */
esp_err_t quote_fetch(quote_t *out);
