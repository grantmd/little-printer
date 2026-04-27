# Public Message Queue — Design

## Background

The little-printer firmware (v1, 2026-04-26) prints a daily briefing at 09:00 PT containing weather, an inspirational quote, and (via this feature) optionally up to five short messages submitted by the public.

The motivation is hobby-shaped: let friends (and curious strangers, since there's no auth) leave short notes that show up on the printer the next morning. The "next morning" delivery model is intentional — it removes the realtime expectation, gives the maintainer (the user) a daily rate cap by construction, and dovetails with the existing briefing schedule.

## Goals

- Accept short text messages from the public via a simple web form.
- Cap the in-flight queue at 5 messages — the 6th is rejected at submission time.
- Cap message bodies at 280 characters and sender names at 24 characters.
- Print all queued messages with the next 09:00 briefing, in their own labelled block.
- Survive a missed day gracefully: if the C3 is offline at 09:00, messages stay queued; they print on the next successful 09:00.

## Non-goals

- No realtime delivery. MQTT was considered and rejected — for once-a-day delivery, HTTPS polling is simpler.
- No authentication on the public form. Length cap, queue cap, and length-of-day rate limiting are the only abuse defences for v1.
- No moderation queue, profanity filter, or manual approval. v1 prints whatever comes in. If abuse becomes a problem, that's a v1.1 conversation.
- No per-sender or per-IP rate limiting in v1. The hard queue cap is the rate limit.

## Architecture

```
┌──────────────────┐   POST /submit (HTML form)
│  Public web form │ ────────────────────────► ┌────────────────┐
│  little-printer  │ ◄──────────────────────── │   Fly.io app   │
│  .fly.dev        │   201 / 429 / 400          │  (Go binary)   │
└──────────────────┘                            │  SQLite-on-vol │
                                                │                │
        ┌──────────────────────────────────────┤  /data/queue.db│
        │ GET /pending  (Bearer: <token>)      │                │
        │ POST /confirm (Bearer: <token>)      │                │
        ▼                                      └────────────────┘
┌──────────────────┐
│  XIAO ESP32-C3   │  at 09:00, after weather + quote block:
│  + MC206H        │  fetch /pending → print → POST /confirm
└──────────────────┘
```

- **Fly.io app is the authoritative store.** SQLite on a Fly volume holds the queue.
- **Public form is unauthenticated.** Anyone with the URL can submit, subject to length and queue caps.
- **C3 → Fly endpoints are protected by a bearer token.** Otherwise anyone could read pending messages or drain the queue. The token is stored in `CONFIG_MESSAGES_TOKEN` on the C3 and as a Fly secret on the app side.
- **Fetch-then-confirm pattern.** Messages are not deleted on `GET /pending`. The C3 issues `POST /confirm` only after a successful print. A crash or print failure between fetch and confirm causes those messages to print again the next day — annoying but recoverable, and strictly better than silent loss.

## Fly.io app

### Stack

- **Language:** Go, single static binary.
- **HTTP:** stdlib `net/http`.
- **Templating:** stdlib `html/template` for the form page.
- **Storage:** SQLite via `modernc.org/sqlite` (CGO-free; keeps the build trivial and the image small).
- **Container:** distroless or scratch base, ~10MB final image.
- **Hosting:** single Fly machine, smallest size, with a 1 GB volume mounted at `/data`.

### Endpoints

#### `GET /`

Renders the public form (HTML). Page contains:

- A sender-name input (1-24 chars, required).
- A message textarea (1-280 chars, required), with a live character counter.
- A "current queue: N / 5" indicator, fetched server-side at render time (no JS polling).
- A submit button.
- A small footer note: "Messages print at 09:00 Pacific."

Form posts to `/submit`. The page can be styled minimally — the goal is "clearly a friendly little form," not a CMS.

#### `POST /submit`

Content type: `application/x-www-form-urlencoded`. Fields: `sender`, `message`.

Validation order:

1. `sender` is non-empty after trim, ≤ 24 chars after trim. Reject 400 with reason if not.
2. `message` is non-empty after trim, ≤ 280 chars after trim. Reject 400 with reason if not.
3. Current queue size < 5. If the queue is full, reject 429 with a friendly "queue is full, try after tomorrow's print" message.

On success, insert a row and return 201 (rendered as a small "thanks, queued!" page or simple text).

Server should reject obvious abuse paths (e.g., requests with no `Content-Type` or impossibly large bodies) with 400, but no need for clever heuristics. Length cap + queue cap is enough for v1.

#### `GET /pending`

Auth: `Authorization: Bearer <token>` header required. 401 on missing/wrong token.

Returns JSON:

```json
[
  { "id": 17, "sender": "alice", "message": "good luck on the interview!", "created_at": 1745799123 },
  { "id": 18, "sender": "bob",   "message": "hello from london",            "created_at": 1745801234 }
]
```

Order by `created_at` ascending. Empty array if nothing pending.

This endpoint does NOT delete or mark anything. It is a pure read.

#### `POST /confirm`

Auth: same bearer token.

Body: `application/json` `{"ids": [17, 18]}`. Deletes those rows. Returns 204.

IDs that don't exist are silently ignored (idempotent — useful if the C3 retries a confirm after a partial failure).

### Schema

```sql
CREATE TABLE messages (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    sender     TEXT    NOT NULL,
    message    TEXT    NOT NULL,
    created_at INTEGER NOT NULL  -- unix epoch seconds
);
```

No status column. Pending = row exists. Printed = row deleted.

### Configuration

Two Fly secrets:

- `PRINTER_TOKEN` — the bearer token. The C3 must send the same value.
- `LISTEN_ADDR` (optional) — defaults to `:8080`.

Volume: one volume, ≥ 1 GB, mounted at `/data`. Database lives at `/data/queue.db`.

`fly.toml` declares one machine in `sjc` (closest to San Carlos), one volume, the secrets.

### Deploy

`fly launch` for initial bootstrap, `fly deploy` for updates. CI workflow can be added later but isn't a v1 requirement.

### Observability

- Standard log lines per request (method, path, status, duration). Stdout — Fly captures these automatically.
- No metrics or tracing for v1. The traffic is too small to need them.

## C3 firmware integration

### New Kconfig entries

```kconfig
config MESSAGES_BASE_URL
    string "Base URL for the public message queue service"
    default "https://little-printer.fly.dev"
    help
      Without trailing slash. The C3 fetches GET <url>/pending and POSTs
      to <url>/confirm.

config MESSAGES_TOKEN
    string "Bearer token for the message queue service"
    default ""
    help
      Must match PRINTER_TOKEN on the Fly.io app.
```

### Extend `http_fetch`

The existing `http_fetch(const char *url, char **out)` doesn't support custom headers or POST. Two reasonable extension paths:

- **Variant A:** add `http_fetch_with_header(url, header_line, out)` and a sibling `http_post_json(url, header_line, body_str, out)`.
- **Variant B:** rewrite `http_fetch` to take an opaque `request_t` struct.

Pick A for simplicity — fewer concepts, only what's needed. The signatures stay narrow.

### New `main/messages.c/h`

```c
typedef struct {
    int  id;
    char sender[32];   /* 24 + slack */
    char message[320]; /* 280 + slack */
} message_t;

esp_err_t messages_fetch_pending(message_t **out, size_t *count);  /* caller frees array */
esp_err_t messages_confirm(const int *ids, size_t n);
```

Implementation:

- `messages_fetch_pending`: GET `<MESSAGES_BASE_URL>/pending` with `Authorization: Bearer <MESSAGES_TOKEN>`, parse JSON via cJSON, return an array on the heap.
- `messages_confirm`: build `{"ids":[...]}` body, POST to `<MESSAGES_BASE_URL>/confirm` with the same Authorization header.

### Changes to `briefing.c`

After the existing footer separator (the closing `=========`), conditionally print the messages block:

```
================================
   [existing date header]
================================

[weather block]

[divider]

[quote block]

================================       ← end of existing briefing

   ----- MESSAGES -----                 ← only if pending non-empty

  good luck on the interview!
       -- alice

  hello from london
       -- bob

================================
```

Pseudocode:

```c
message_t *msgs = NULL;
size_t n = 0;
if (messages_fetch_pending(&msgs, &n) == ESP_OK && n > 0) {
    thermal_printer_feed(1);
    thermal_printer_set_justify('C');
    thermal_printer_println("----- MESSAGES -----");
    thermal_printer_feed(1);
    thermal_printer_set_justify('L');
    int printed_ids[5];
    for (size_t i = 0; i < n; i++) {
        text_wrap(msgs[i].message, PRINT_LINE_WIDTH - 4, &println_indented);
        char attribution[40];
        snprintf(attribution, sizeof(attribution), "       -- %s", msgs[i].sender);
        thermal_printer_println(attribution);
        thermal_printer_feed(1);
        printed_ids[i] = msgs[i].id;
    }
    thermal_printer_set_justify('C');
    thermal_printer_println("================================");
    thermal_printer_feed(3);
    messages_confirm(printed_ids, n);
}
free(msgs);
```

### Failure modes

- **`/pending` fetch fails (network, 5xx, parse error):** skip the messages block this run. The DB is untouched; messages will print tomorrow. Same shape as weather/quote degraded paths.
- **`/pending` succeeds, print fails partway:** unlikely with healthy hardware. If it happens, some messages may be partially printed and still in the queue. They'll reprint tomorrow.
- **`/confirm` fails:** messages reprint tomorrow. Slightly annoying duplicate but no data loss.
- **`/pending` returns empty array:** no messages block printed. Silent.

## Testing approach

### Fly.io app

- **Unit tests** for the validators (length checks, queue-full check). Plain Go `testing` package.
- **Integration tests** against an in-memory SQLite database for the full submit → fetch → confirm flow.
- **Manual end-to-end** via `curl` and the form, exercising each endpoint and rejection path.

### C3 firmware

- No new pure helpers worth host-testing — the messages module is mostly HTTP+JSON glue.
- Verification is by flashing and confirming the print output looks right with a few seeded messages in the Fly DB.

## Repo layout impact

The Fly app is its own subproject. Two layout options:

- **A:** put the Go code in a sibling subdirectory, `fly-message-queue/`, alongside `diag/` and `main/`. Keeps everything in one repo.
- **B:** make it a separate repo entirely.

Pick A. One repo per project is friendlier for a hobby setup, and CI workflows can fan out per subdirectory.

```
little-printer/
├── ...
├── fly-message-queue/
│   ├── main.go
│   ├── handlers.go
│   ├── handlers_test.go
│   ├── db.go
│   ├── templates/index.html
│   ├── go.mod / go.sum
│   ├── Dockerfile
│   └── fly.toml
├── ...
```

Fly app builds + tests get their own CI workflow (separate from the IDF builds).

## Out of scope (intentionally)

- Per-sender / per-IP rate limiting beyond the queue cap.
- Authenticated submissions or per-friend tokens.
- Profanity filter, abuse triage, or manual approval queue.
- Multi-message-per-day cap beyond the queue size.
- Custom domain on the Fly app (use the default `*.fly.dev`).
- Realtime push (MQTT) — explicitly rejected; HTTPS polling fits the once-a-day model.
- Quiet hours / hold-back logic — the existing 09:00 schedule is the only knob.
- Web form polish beyond a single-page minimal HTML form.
- Rich text, images, or unicode beyond what the printer's default character set already prints.

## Success criteria

1. A stranger with the URL can submit a 280-char message + 24-char sender name and get a clear "queued" or "queue full" response.
2. Up to 5 messages stay queued in the Fly DB through restarts.
3. The C3 fetches and prints them at the next 09:00 briefing, in a clearly demarcated MESSAGES block, then clears the queue.
4. If the network is down at 09:00, the briefing still prints (without messages), and the messages print on the next successful day.
5. The bearer token guards the /pending and /confirm endpoints — a curl without the header gets 401.
