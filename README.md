# little-printer

A XIAO ESP32-C3 drives an MC206H thermal printer to print:

- A **daily briefing** at 09:00 America/Los_Angeles — date header, weather sprite, current weather for San Carlos, and one inspirational quote.
- **Short messages** sent by the public via a Fly.io-hosted form. The C3 polls the queue at the top of every hour during waking hours (08:00–22:00 default) and prints whatever's queued.

Both print paths pre-flight a printer status query (ESC/POS `DLE EOT 4`) and silently skip if the printer is offline, unpowered, or out of paper. Queued messages stay queued; the briefing waits for the next day. No silent data loss.

Manual triggers via the USB-CDC serial console:
- `p` — print the daily briefing immediately
- `m` — poll the message queue immediately
- `s` — query printer status (handy for "is the printer alive" diagnostics)

Otherwise the device is silent until the next scheduled cycle.

## Hardware

- XIAO ESP32-C3 (Seeed Studio)
- MC206H thermal printer (58mm paper, ESC/POS, TTL serial @ 9600 8N1)
- 5–9V DC supply, minimum 2.5A
- 58mm thermal paper roll

Full wiring (4-pin TTL JST + 2-pin power JST), printer self-test verification, and the rationale behind it all lives in [SPEC.md](./SPEC.md).

## Build and flash

Requires ESP-IDF v5.x with the export script sourced (`. $IDF_PATH/export.sh`).

```bash
idf.py set-target esp32c3
idf.py menuconfig          # set Wi-Fi creds + message-queue URL/token
idf.py build flash monitor # Ctrl+] to exit monitor
```

On boot the firmware:
1. Connects to Wi-Fi and syncs time via NTP.
2. Initialises the printer (early — the GPIO needs to be driven HIGH before Wi-Fi connect to avoid line-noise garbage).
3. Spawns three FreeRTOS tasks: console (input), briefing scheduler, message-queue poller.
4. Idles until something fires.

## Configuration

Available under `idf.py menuconfig → Briefing Printer Config`:

| Key                       | Default                              | Notes                                                                      |
| ------------------------- | ------------------------------------ | -------------------------------------------------------------------------- |
| `WIFI_SSID`               | (empty)                              | 2.4 GHz only — the C3 doesn't do 5 GHz                                     |
| `WIFI_PASSWORD`           | (empty)                              |                                                                            |
| `PRINT_HOUR`              | 9                                    | Local hour to fire the daily briefing                                      |
| `PRINT_MINUTE`            | 0                                    |                                                                            |
| `PRINTER_BAUD`            | 9600                                 | Match the printer's self-test report                                       |
| `MESSAGES_BASE_URL`       | `https://little-printer-msgs.fly.dev` | Without trailing slash. C3 GETs `<url>/pending` and POSTs `<url>/confirm`. |
| `MESSAGES_TOKEN`          | (empty)                              | Bearer token; must match `PRINTER_TOKEN` on the Fly app                    |
| `MESSAGES_START_HOUR`     | 8                                    | Inclusive — message poll fires when local hour ≥ this                      |
| `MESSAGES_END_HOUR`       | 22                                   | Exclusive — message poll fires when local hour < this                      |

Pins, timezone, and location coordinates are compile-time constants in `main/config.h`.

## Public message queue

Anyone can post a short message at `https://little-printer-msgs.fly.dev/` (or wherever you've deployed the sister app):

- Sender name (1–24 chars, required)
- Message body (1–280 chars, required)
- Up to 3 messages can be queued at any one time; submissions past that get a 429.

The queue is owned by a small Go service in [`fly-message-queue/`](./fly-message-queue) — SQLite on a Fly volume. The C3 polls hourly, prints, then confirms (deletes) the printed IDs. Fetch-then-confirm means a print failure causes a duplicate next hour, never silent loss.

To deploy the service yourself:

```bash
cd fly-message-queue
fly launch --no-deploy        # first time only — pick an app name, region sjc
fly volumes create queue_data --size 1 --region sjc
fly secrets set PRINTER_TOKEN=$(openssl rand -hex 32)
fly deploy
```

Then plug the same token + your Fly app URL into the C3's `MESSAGES_BASE_URL` / `MESSAGES_TOKEN` Kconfig entries.

## Repo layout

```
little-printer/
├── CMakeLists.txt              top-level IDF project (briefing firmware)
├── sdkconfig.defaults
├── SPEC.md                     v1 firmware spec (hardware, ESC/POS, APIs)
├── README.md                   you are here
├── main/                       app code (Wi-Fi, NTP, briefing, scheduler, messages, printer mutex)
├── components/thermal_printer/ ESC/POS UART driver
├── host_tests/                 host-side unit tests for pure helpers (cc, no IDF)
├── fly-message-queue/          public message queue — Go + SQLite, deploys to Fly.io
├── diag/                       printer-acceptance firmware (separate IDF project)
├── diagnostic-firmware-spec.md spec for diag/
└── docs/superpowers/           per-feature design docs and implementation plans
```

The `diag/` subfolder is its own minimal IDF project that exercises a freshly-acquired printer at three escalating heat levels. Useful as a smoke test if anything ever seems wrong with the printer; not part of the main build.

`fly-message-queue/` is a separate Go module — its own go.mod, tests, Dockerfile, fly.toml.

## Tests

Pure firmware helpers (text wrap, weather-code lookup) have host-side unit tests:

```bash
cc -Wall -o /tmp/test_text_wrap host_tests/test_text_wrap.c main/text_wrap.c
/tmp/test_text_wrap
cc -Wall -o /tmp/test_weather_code host_tests/test_weather_code.c
/tmp/test_weather_code
```

The Fly.io Go service has a proper test suite:

```bash
cd fly-message-queue
go test ./...
```

Hardware-touching firmware code is verified by flashing and observing the actual printer.

## CI

GitHub Actions on every push to `main`:
- Host-side firmware tests
- ESP-IDF v5.5 build verification (briefing firmware + diag/)
- Go test suite for the Fly app

On a `v*` tag push, `release.yml` builds both firmwares and attaches `bin`/`elf`/bootloader/partition-table artifacts to a GitHub Release.

## Known polish items

- **Weather sprites at 24×24 are too small to read.** Bigger sprites (or a proper icon font) would help.
- **A 10kΩ pullup from GPIO21 to 3.3V** would silence the burst of garbage that prints during `idf.py flash` (the boot ROM controls the pin during programming).

## Future scope

- Physical button on a spare GPIO for on-demand quotes
- Deep sleep + RTC alarm for battery operation
- OTA updates
- Additional content sources (Bluesky mentions, xkcd of the day, etc.)

## Credits

Open weather data: [Open-Meteo](https://open-meteo.com/). Quotes: [ZenQuotes](https://zenquotes.io/). ESC/POS reverse engineering: Adafruit's `Adafruit_Thermal.cpp` (originally for the CSN-A2; the byte sequences carry over to the MC206H cleanly). Hosting for the message queue: [Fly.io](https://fly.io/).
