#pragma once

#include <stddef.h>
#include "esp_err.h"

typedef struct {
    int  id;
    char sender[32];   /* 24-char cap on the server side, plus headroom */
    char message[320]; /* 280-char cap on the server side, plus headroom */
} message_t;

/*
 * Fetch all pending messages from the Fly.io service. On ESP_OK, *out is
 * a heap-allocated array of `*count` messages — caller must free(*out).
 * On failure, *out=NULL and *count=0.
 */
esp_err_t messages_fetch_pending(message_t **out, size_t *count);

/*
 * POST the given IDs as confirmed (printed). Returns ESP_OK on success.
 * No-op if `n == 0`.
 */
esp_err_t messages_confirm(const int *ids, size_t n);
