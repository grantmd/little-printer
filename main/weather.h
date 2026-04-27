#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    int   temp_f;        /* whole-degree */
    int   wind_mph;
    int   weather_code;  /* WMO code, raw — used for sprite lookup */
    char  description[32]; /* e.g., "Partly cloudy" */
} weather_t;

/*
 * Fetch current weather for the configured location. On ESP_OK, *out is
 * fully populated. On failure, *out is left untouched.
 */
esp_err_t weather_fetch(weather_t *out);

/*
 * Map a WMO weather code to a short human-readable string. Returns a
 * static string that lives for the lifetime of the program — do not free.
 */
const char *weather_code_to_string(int code);

/*
 * Return a 24x24 monochrome sprite (3 bytes/row × 24 rows = 72 bytes) for
 * the given WMO weather code, or NULL if no sprite is associated with it.
 * The returned pointer is to static program data — do not free.
 *
 * Sprites bucket the WMO codes into four families: clear, cloudy (incl.
 * partly cloudy + overcast + fog), precipitation/rain (incl. drizzle,
 * freezing rain, thunderstorm), and snow (incl. snow grains/showers).
 */
const uint8_t *weather_sprite_for_code(int code);
