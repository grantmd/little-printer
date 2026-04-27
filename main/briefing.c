#include "briefing.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#include "esp_log.h"

#include "config.h"
#include "thermal_printer.h"
#include "weather.h"
#include "quote.h"
#include "text_wrap.h"

#include "printer_lock.h"

static const char *TAG = "briefing";

static void println_indented(const char *line) {
    char buf[80];
    snprintf(buf, sizeof(buf), "  %s", line);
    thermal_printer_println(buf);
}

static void format_date(char *out, size_t out_size) {
    time_t now = time(NULL);
    struct tm lt;
    localtime_r(&now, &lt);
    /*
     * %d (zero-padded) instead of %-d (no-leading-zero) — the latter is a
     * GNU extension that newlib doesn't support, and on failure newlib
     * leaves the buffer uninitialised. Pre-zero the buffer for safety.
     */
    out[0] = '\0';
    strftime(out, out_size, "%A, %B %d", &lt);
    for (char *p = out; *p; p++) *p = (char)toupper((unsigned char)*p);
}

void briefing_run(void) {
    ESP_LOGI(TAG, "briefing_run starting");

    weather_t w;
    bool have_weather = (weather_fetch(&w) == ESP_OK);

    quote_t q;
    bool have_quote = (quote_fetch(&q) == ESP_OK);

    char date_line[48];
    format_date(date_line, sizeof(date_line));

    /* Serialise printer access — messages_task may also fire this minute. */
    xSemaphoreTake(s_print_mutex, portMAX_DELAY);

    /* Pre-flight: if the printer is offline or out of paper, skip the
     * print so we don't quietly drop output to a void. */
    thermal_printer_status_t pstatus;
    if (thermal_printer_query_status(&pstatus) != ESP_OK) {
        ESP_LOGW(TAG, "printer not responding; skipping briefing");
        xSemaphoreGive(s_print_mutex);
        return;
    }
    if (pstatus.paper_end) {
        ESP_LOGW(TAG, "printer out of paper; skipping briefing");
        xSemaphoreGive(s_print_mutex);
        return;
    }
    if (pstatus.paper_near_end) {
        ESP_LOGW(TAG, "printer paper near end — printing anyway");
    }

    thermal_printer_reset();
    thermal_printer_set_justify('C');
    thermal_printer_println("================================");
    thermal_printer_set_size('M');
    thermal_printer_println(date_line);
    thermal_printer_set_size('S');
    thermal_printer_println("================================");
    thermal_printer_feed(1);

    /* Weather sprite, centered above the weather text. */
    if (have_weather) {
        const uint8_t *sprite = weather_sprite_for_code(w.weather_code);
        if (sprite) {
            thermal_printer_set_justify('C');
            thermal_printer_print_bitmap(3, 24, sprite);
            thermal_printer_feed(1);
        }
    }

    /* Weather block (or degraded line). */
    thermal_printer_set_justify('L');
    if (have_weather) {
        char line[64];
        println_indented(LOCATION_NAME);
        snprintf(line, sizeof(line), "%dF, %s", w.temp_f, w.description);
        println_indented(line);
        snprintf(line, sizeof(line), "Wind: %d mph", w.wind_mph);
        println_indented(line);
    } else {
        println_indented("weather unavailable");
    }
    thermal_printer_feed(1);

    thermal_printer_set_justify('C');
    thermal_printer_println("--------------------------------");
    thermal_printer_feed(1);

    /* Quote block (or degraded line, or skip if both fail). */
    thermal_printer_set_justify('L');
    if (have_quote) {
        char wrap_in[320];
        snprintf(wrap_in, sizeof(wrap_in), "\"%s\"", q.body);
        text_wrap(wrap_in, PRINT_LINE_WIDTH - 4, &println_indented);
        thermal_printer_println("");
        char attribution[80];
        snprintf(attribution, sizeof(attribution), "       -- %s", q.author);
        thermal_printer_println(attribution);
    } else if (!have_weather) {
        /* Both APIs failed — confirm the schedule still ran. */
        println_indented("nothing to report today");
    }

    thermal_printer_feed(1);
    thermal_printer_set_justify('C');
    thermal_printer_println("================================");
    thermal_printer_feed(3);
    thermal_printer_sleep(60);

    xSemaphoreGive(s_print_mutex);

    ESP_LOGI(TAG, "briefing_run done");
}
