#pragma once

#include "esp_err.h"

/*
 * Sync time via NTP (pool.ntp.org), set the timezone to America/Los_Angeles
 * with DST handling, and block up to 15s waiting for the first sync.
 *
 * Caller must have established Wi-Fi connectivity first.
 */
esp_err_t time_sync_init(void);

/*
 * Re-trigger an NTP sync. Non-blocking. Use after each daily briefing to
 * keep the RTC drift bounded.
 */
void time_sync_refresh(void);
