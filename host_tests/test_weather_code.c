#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Copy of weather_code_to_string from main/weather.c — keep in sync. */
static const char *weather_code_to_string(int code) {
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

#define EXPECT(code, expected) do {                                         \
    const char *got = weather_code_to_string(code);                         \
    if (strcmp(got, expected) != 0) {                                       \
        fprintf(stderr, "FAIL: code=%d expected='%s' got='%s'\n",           \
                code, expected, got);                                       \
        exit(1);                                                            \
    }                                                                       \
} while (0)

int main(void) {
    EXPECT(0,   "Clear");
    EXPECT(1,   "Partly cloudy");
    EXPECT(3,   "Overcast");
    EXPECT(45,  "Fog");
    EXPECT(63,  "Rain");
    EXPECT(95,  "Thunderstorm");
    EXPECT(999, "Unknown");
    printf("PASS\n");
    return 0;
}
