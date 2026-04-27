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

/*
 * Fetch any pending messages and print them as standalone receipts (no
 * "MESSAGES" header — just message bodies + sender attributions). On
 * success, POSTs /confirm to drop the printed IDs from the queue.
 *
 * Acquires the printer mutex (printer_lock.h) for the duration of the
 * print. Safe to call concurrently with briefing_run().
 *
 * No-op if there are no pending messages. Returns ESP_FAIL on a fetch
 * or confirm error; the queue is unchanged in that case.
 */
esp_err_t messages_print_pending(void);
