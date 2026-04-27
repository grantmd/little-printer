#pragma once

/*
 * Fetch weather + quote, format the briefing, and emit it to the thermal
 * printer. Never throws; on individual fetch failures, prints a degraded
 * version (see SPEC.md error-handling section). The thermal_printer must
 * already be initialised.
 */
void briefing_run(void);
