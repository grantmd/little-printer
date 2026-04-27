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

    out->temp_f   = (int)round(temp_node->valuedouble);
    out->wind_mph = (int)round(wind_node->valuedouble);
    const char *desc = weather_code_to_string(code_node->valueint);
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
