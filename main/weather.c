#include "weather.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "esp_log.h"
#include "cJSON.h"

#include "config.h"
#include "http_fetch.h"

static const char *TAG = "weather";

/*
 * 24x24 monochrome sprites — 3 bytes per row, 24 rows = 72 bytes each.
 * Bit 7 of byte 0 is the leftmost pixel of each row; 1 = ink. Drawn so
 * each sprite reads cleanly on 58mm thermal paper at the printer's
 * native resolution.
 *
 * SUN: filled disc with four cardinal rays.
 * CLOUD: cumulus silhouette with a small bump on top.
 * RAIN: cumulus silhouette + three teardrops below.
 * SNOW: cumulus silhouette + three plus-sign snowflakes below.
 */

static const uint8_t SPRITE_SUN[72] = {
    /* row  0 */ 0x00, 0x00, 0x00,
    /* row  1 */ 0x00, 0x18, 0x00,  /* top ray (cols 11-12) */
    /* row  2 */ 0x00, 0x18, 0x00,
    /* row  3 */ 0x00, 0x18, 0x00,
    /* row  4 */ 0x00, 0x00, 0x00,
    /* row  5 */ 0x00, 0x00, 0x00,
    /* row  6 */ 0x00, 0x08, 0x00,  /* circle apex */
    /* row  7 */ 0x00, 0x7F, 0x00,
    /* row  8 */ 0x00, 0xFF, 0x80,
    /* row  9 */ 0x01, 0xFF, 0xC0,
    /* row 10 */ 0x01, 0xFF, 0xC0,
    /* row 11 */ 0x71, 0xFF, 0xCE,  /* + left ray (cols 1-3), right ray (20-22) */
    /* row 12 */ 0x73, 0xFF, 0xEE,  /* widest row + same rays */
    /* row 13 */ 0x01, 0xFF, 0xC0,
    /* row 14 */ 0x01, 0xFF, 0xC0,
    /* row 15 */ 0x01, 0xFF, 0xC0,
    /* row 16 */ 0x00, 0xFF, 0x80,
    /* row 17 */ 0x00, 0x7F, 0x00,
    /* row 18 */ 0x00, 0x08, 0x00,
    /* row 19 */ 0x00, 0x00, 0x00,
    /* row 20 */ 0x00, 0x18, 0x00,  /* bottom ray */
    /* row 21 */ 0x00, 0x18, 0x00,
    /* row 22 */ 0x00, 0x18, 0x00,
    /* row 23 */ 0x00, 0x00, 0x00,
};

static const uint8_t SPRITE_CLOUD[72] = {
    /* row  0 */ 0x00, 0x00, 0x00,
    /* row  1 */ 0x00, 0x00, 0x00,
    /* row  2 */ 0x00, 0x00, 0x00,
    /* row  3 */ 0x00, 0x7C, 0x00,  /* small bump on top */
    /* row  4 */ 0x00, 0xFF, 0x80,
    /* row  5 */ 0x00, 0xFF, 0xE0,
    /* row  6 */ 0x07, 0xFF, 0xE0,  /* widening to the left */
    /* row  7 */ 0x1F, 0xFF, 0xF8,
    /* row  8 */ 0x3F, 0xFF, 0xFC,  /* full-width body */
    /* row  9 */ 0x3F, 0xFF, 0xFC,
    /* row 10 */ 0x3F, 0xFF, 0xFC,
    /* row 11 */ 0x3F, 0xFF, 0xFC,
    /* row 12 */ 0x3F, 0xFF, 0xFC,
    /* row 13 */ 0x3F, 0xFF, 0xFC,
    /* row 14 */ 0x1F, 0xFF, 0xF8,  /* taper to bottom */
    /* row 15 */ 0x0F, 0xFF, 0xF0,
    /* row 16 */ 0x00, 0x00, 0x00,
    /* row 17 */ 0x00, 0x00, 0x00,
    /* row 18 */ 0x00, 0x00, 0x00,
    /* row 19 */ 0x00, 0x00, 0x00,
    /* row 20 */ 0x00, 0x00, 0x00,
    /* row 21 */ 0x00, 0x00, 0x00,
    /* row 22 */ 0x00, 0x00, 0x00,
    /* row 23 */ 0x00, 0x00, 0x00,
};

static const uint8_t SPRITE_RAIN[72] = {
    /* compressed cloud, rows 0-11 */
    /* row  0 */ 0x00, 0x7C, 0x00,
    /* row  1 */ 0x00, 0xFF, 0x80,
    /* row  2 */ 0x00, 0xFF, 0xE0,
    /* row  3 */ 0x07, 0xFF, 0xE0,
    /* row  4 */ 0x1F, 0xFF, 0xF8,
    /* row  5 */ 0x3F, 0xFF, 0xFC,
    /* row  6 */ 0x3F, 0xFF, 0xFC,
    /* row  7 */ 0x3F, 0xFF, 0xFC,
    /* row  8 */ 0x3F, 0xFF, 0xFC,
    /* row  9 */ 0x3F, 0xFF, 0xFC,
    /* row 10 */ 0x1F, 0xFF, 0xF8,
    /* row 11 */ 0x0F, 0xFF, 0xF0,
    /* drops, rows 12-23 — three 2-wide teardrops, middle one offset */
    /* row 12 */ 0x00, 0x00, 0x00,
    /* row 13 */ 0x03, 0x00, 0xC0,  /* drops 1 + 3 start (cols 6-7, 16-17) */
    /* row 14 */ 0x03, 0x18, 0xC0,  /* drop 2 starts (cols 11-12) */
    /* row 15 */ 0x03, 0x18, 0xC0,
    /* row 16 */ 0x03, 0x18, 0xC0,
    /* row 17 */ 0x03, 0x18, 0xC0,
    /* row 18 */ 0x00, 0x18, 0x00,  /* drop 2 trailing */
    /* row 19 */ 0x00, 0x00, 0x00,
    /* row 20 */ 0x00, 0x00, 0x00,
    /* row 21 */ 0x00, 0x00, 0x00,
    /* row 22 */ 0x00, 0x00, 0x00,
    /* row 23 */ 0x00, 0x00, 0x00,
};

static const uint8_t SPRITE_SNOW[72] = {
    /* same compressed cloud, rows 0-11 */
    /* row  0 */ 0x00, 0x7C, 0x00,
    /* row  1 */ 0x00, 0xFF, 0x80,
    /* row  2 */ 0x00, 0xFF, 0xE0,
    /* row  3 */ 0x07, 0xFF, 0xE0,
    /* row  4 */ 0x1F, 0xFF, 0xF8,
    /* row  5 */ 0x3F, 0xFF, 0xFC,
    /* row  6 */ 0x3F, 0xFF, 0xFC,
    /* row  7 */ 0x3F, 0xFF, 0xFC,
    /* row  8 */ 0x3F, 0xFF, 0xFC,
    /* row  9 */ 0x3F, 0xFF, 0xFC,
    /* row 10 */ 0x1F, 0xFF, 0xF8,
    /* row 11 */ 0x0F, 0xFF, 0xF0,
    /* three plus-sign snowflakes at cols 5-7, 10-12, 15-17 */
    /* row 12 */ 0x00, 0x00, 0x00,
    /* row 13 */ 0x00, 0x00, 0x00,
    /* row 14 */ 0x02, 0x10, 0x80,  /* centre dots */
    /* row 15 */ 0x07, 0x39, 0xC0,  /* horizontal arms */
    /* row 16 */ 0x02, 0x10, 0x80,  /* centre dots */
    /* row 17 */ 0x00, 0x00, 0x00,
    /* row 18 */ 0x00, 0x00, 0x00,
    /* row 19 */ 0x00, 0x00, 0x00,
    /* row 20 */ 0x00, 0x00, 0x00,
    /* row 21 */ 0x00, 0x00, 0x00,
    /* row 22 */ 0x00, 0x00, 0x00,
    /* row 23 */ 0x00, 0x00, 0x00,
};

const uint8_t *weather_sprite_for_code(int code) {
    switch (code) {
        case 0:                                      return SPRITE_SUN;
        case 1: case 2: case 3:                      return SPRITE_CLOUD;
        case 45: case 48:                            return SPRITE_CLOUD;
        case 51: case 53: case 55:                   return SPRITE_RAIN;
        case 61: case 63: case 65:                   return SPRITE_RAIN;
        case 66: case 67:                            return SPRITE_RAIN;
        case 80: case 81: case 82:                   return SPRITE_RAIN;
        case 95: case 96: case 99:                   return SPRITE_RAIN;
        case 71: case 73: case 75: case 77:          return SPRITE_SNOW;
        case 85: case 86:                            return SPRITE_SNOW;
        default:                                     return NULL;
    }
}

const char *weather_code_to_string(int code) {
    switch (code) {
        case 0:                      return "Clear";
        case 1: case 2:              return "Partly cloudy";
        case 3:                      return "Overcast";
        case 45: case 48:            return "Fog";
        case 51: case 53: case 55:   return "Drizzle";
        case 61: case 63: case 65:   return "Rain";
        case 66: case 67:            return "Freezing rain";
        case 71: case 73: case 75:   return "Snow";
        case 77:                     return "Snow grains";
        case 80: case 81: case 82:   return "Rain showers";
        case 85: case 86:            return "Snow showers";
        case 95:                     return "Thunderstorm";
        case 96: case 99:            return "Thunderstorm w/ hail";
        default:                     return "Unknown";
    }
}

esp_err_t weather_fetch(weather_t *out) {
    char url[256];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast"
             "?latitude=%.4f&longitude=%.4f"
             "&current=temperature_2m,weather_code,wind_speed_10m"
             "&temperature_unit=fahrenheit&wind_speed_unit=mph"
             "&timezone=America/Los_Angeles",
             LOCATION_LAT, LOCATION_LON);

    char *body = NULL;
    if (http_fetch(url, &body) != ESP_OK) {
        return ESP_FAIL;
    }

    esp_err_t result = ESP_FAIL;
    cJSON *root = cJSON_Parse(body);
    if (!root) {
        ESP_LOGW(TAG, "JSON parse failed");
        goto done;
    }

    cJSON *current = cJSON_GetObjectItem(root, "current");
    if (!current) {
        ESP_LOGW(TAG, "no 'current' in response");
        goto done;
    }

    cJSON *temp_node = cJSON_GetObjectItem(current, "temperature_2m");
    cJSON *code_node = cJSON_GetObjectItem(current, "weather_code");
    cJSON *wind_node = cJSON_GetObjectItem(current, "wind_speed_10m");
    if (!temp_node || !code_node || !wind_node) {
        ESP_LOGW(TAG, "missing fields in 'current'");
        goto done;
    }

    out->temp_f       = (int)round(temp_node->valuedouble);
    out->wind_mph     = (int)round(wind_node->valuedouble);
    out->weather_code = code_node->valueint;
    const char *desc = weather_code_to_string(out->weather_code);
    strncpy(out->description, desc, sizeof(out->description) - 1);
    out->description[sizeof(out->description) - 1] = '\0';

    ESP_LOGI(TAG, "%dF %s wind %dmph",
             out->temp_f, out->description, out->wind_mph);
    result = ESP_OK;

done:
    cJSON_Delete(root);
    free(body);
    return result;
}
