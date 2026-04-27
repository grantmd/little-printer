# Public Message Queue Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let strangers submit short messages via a public web form; up to 5 are queued in a Fly.io-hosted Go service backed by SQLite, then printed by the C3 with the next 09:00 daily briefing.

**Architecture:** Two cooperating subsystems sharing an HTTP API. Phase A: a Go service on Fly.io that owns the queue (SQLite on a volume), exposes a public form + 4 endpoints, and is the source of truth. Phase B: the C3 firmware adds a messages component that, during the daily 09:00 briefing, fetches pending messages, prints them as a labelled trailing block, and confirms (deletes) them.

**Tech Stack:** Go 1.22+ stdlib (`net/http`, `html/template`, `database/sql`) plus `modernc.org/sqlite` (CGO-free SQLite driver), Fly.io machine + volume, ESP-IDF v5.x C against the existing `esp_http_client` and `cJSON`.

**Reference docs:**
- Spec: `docs/superpowers/specs/2026-04-27-public-message-queue-design.md`
- Existing firmware: `SPEC.md`, `main/briefing.c`, `main/http_fetch.c`

**Pre-flight: choose a bearer token now**

Before starting, generate a 32-byte hex token. The same value goes into the Fly app (as a Fly secret, `PRINTER_TOKEN`) and the C3 firmware (as `CONFIG_MESSAGES_TOKEN`). Run once and save it somewhere durable:

```bash
openssl rand -hex 32
```

The token will be referred to as `<TOKEN>` in steps below.

---

## File structure (after the plan completes)

```
little-printer/
├── ...
├── fly-message-queue/                          (NEW SUBPROJECT)
│   ├── go.mod
│   ├── go.sum
│   ├── main.go                                 (entrypoint: env, DB, HTTP server)
│   ├── handlers.go                             (HTTP handlers)
│   ├── handlers_test.go
│   ├── db.go                                   (SQLite open + CRUD)
│   ├── db_test.go
│   ├── validate.go                             (length + queue-full checks)
│   ├── validate_test.go
│   ├── templates/
│   │   └── index.html
│   ├── Dockerfile
│   ├── fly.toml
│   └── .gitignore
├── main/
│   ├── http_fetch.h                            (MODIFY — add header / POST variants)
│   ├── http_fetch.c                            (MODIFY)
│   ├── messages.h                              (NEW)
│   ├── messages.c                              (NEW)
│   ├── briefing.c                              (MODIFY — wire messages block)
│   ├── Kconfig.projbuild                       (MODIFY — add 2 entries)
│   └── CMakeLists.txt                          (MODIFY — add messages.c)
└── ...
```

---

## Phase A — Fly.io Go app

### Task A1: Project skeleton

**Files:**
- Create: `/Users/myles/dev/little-printer/fly-message-queue/go.mod`
- Create: `/Users/myles/dev/little-printer/fly-message-queue/main.go`
- Create: `/Users/myles/dev/little-printer/fly-message-queue/.gitignore`

- [ ] **Step 1: Create the directory + go.mod**

```bash
mkdir -p /Users/myles/dev/little-printer/fly-message-queue
cd /Users/myles/dev/little-printer/fly-message-queue
go mod init github.com/grantmd/little-printer/fly-message-queue
```

(The exact module path doesn't really matter for a self-deployed binary; `grantmd/little-printer/fly-message-queue` matches the existing GitHub repo.)

- [ ] **Step 2: Stub main.go**

Write this exact content to `/Users/myles/dev/little-printer/fly-message-queue/main.go`:

```go
package main

import (
	"log"
	"net/http"
	"os"
)

func main() {
	addr := os.Getenv("LISTEN_ADDR")
	if addr == "" {
		addr = ":8080"
	}
	log.Printf("starting on %s", addr)
	if err := http.ListenAndServe(addr, http.NotFoundHandler()); err != nil {
		log.Fatal(err)
	}
}
```

- [ ] **Step 3: .gitignore**

Write this exact content to `/Users/myles/dev/little-printer/fly-message-queue/.gitignore`:

```
# Local binaries
/fly-message-queue
/queue.db
/queue.db-journal
/queue.db-wal
/queue.db-shm

# IDE noise
/.idea/
/.vscode/

# Go test cache
/coverage.out
```

- [ ] **Step 4: Verify it builds**

```bash
cd /Users/myles/dev/little-printer/fly-message-queue
go build ./...
```

Expected: no output, no errors. A binary named `fly-message-queue` appears (and is now ignored).

- [ ] **Step 5: Commit**

```bash
cd /Users/myles/dev/little-printer
git add fly-message-queue
git commit -m "fly-message-queue: project skeleton"
```

---

### Task A2: DB open + schema

**Files:**
- Create: `/Users/myles/dev/little-printer/fly-message-queue/db.go`
- Create: `/Users/myles/dev/little-printer/fly-message-queue/db_test.go`

- [ ] **Step 1: Add the SQLite driver to go.mod**

```bash
cd /Users/myles/dev/little-printer/fly-message-queue
go get modernc.org/sqlite@latest
```

This populates `go.sum`.

- [ ] **Step 2: Write the failing test**

Write this exact content to `/Users/myles/dev/little-printer/fly-message-queue/db_test.go`:

```go
package main

import (
	"testing"
)

func TestOpenDBCreatesSchema(t *testing.T) {
	db, err := openDB(":memory:")
	if err != nil {
		t.Fatalf("openDB: %v", err)
	}
	defer db.Close()

	// Insert a row to prove the table exists with expected columns.
	_, err = db.Exec(`INSERT INTO messages (sender, message, created_at) VALUES (?, ?, ?)`,
		"alice", "hello", 1700000000)
	if err != nil {
		t.Fatalf("insert into messages: %v", err)
	}
}
```

- [ ] **Step 3: Run the test to verify it fails**

```bash
cd /Users/myles/dev/little-printer/fly-message-queue
go test ./...
```

Expected: `undefined: openDB`. Compile failure is the expected "fail" here.

- [ ] **Step 4: Implement `db.go`**

Write this exact content to `/Users/myles/dev/little-printer/fly-message-queue/db.go`:

```go
package main

import (
	"database/sql"

	_ "modernc.org/sqlite"
)

const schema = `
CREATE TABLE IF NOT EXISTS messages (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    sender     TEXT    NOT NULL,
    message    TEXT    NOT NULL,
    created_at INTEGER NOT NULL
);
`

func openDB(path string) (*sql.DB, error) {
	db, err := sql.Open("sqlite", path)
	if err != nil {
		return nil, err
	}
	if _, err := db.Exec(schema); err != nil {
		db.Close()
		return nil, err
	}
	return db, nil
}
```

- [ ] **Step 5: Run the test to verify it passes**

```bash
go test ./...
```

Expected: `PASS`.

- [ ] **Step 6: Commit**

```bash
cd /Users/myles/dev/little-printer
git add fly-message-queue/db.go fly-message-queue/db_test.go fly-message-queue/go.mod fly-message-queue/go.sum
git commit -m "fly-message-queue: SQLite open + schema"
```

---

### Task A3: DB CRUD (insert, list pending, delete by ids, count)

**Files:**
- Modify: `/Users/myles/dev/little-printer/fly-message-queue/db.go`
- Modify: `/Users/myles/dev/little-printer/fly-message-queue/db_test.go`

- [ ] **Step 1: Add the failing tests**

Append this to `/Users/myles/dev/little-printer/fly-message-queue/db_test.go`:

```go
func TestInsertListDelete(t *testing.T) {
	db, err := openDB(":memory:")
	if err != nil {
		t.Fatal(err)
	}
	defer db.Close()

	id1, err := insertMessage(db, "alice", "hello", 1700000000)
	if err != nil {
		t.Fatal(err)
	}
	id2, err := insertMessage(db, "bob", "world", 1700000001)
	if err != nil {
		t.Fatal(err)
	}

	pending, err := listPending(db)
	if err != nil {
		t.Fatal(err)
	}
	if len(pending) != 2 {
		t.Fatalf("want 2 pending, got %d", len(pending))
	}
	if pending[0].ID != id1 || pending[0].Sender != "alice" || pending[0].Message != "hello" {
		t.Errorf("first row mismatch: %+v", pending[0])
	}
	if pending[1].ID != id2 {
		t.Errorf("second row id mismatch: %+v", pending[1])
	}

	if err := deleteByIDs(db, []int64{id1}); err != nil {
		t.Fatal(err)
	}
	pending, err = listPending(db)
	if err != nil {
		t.Fatal(err)
	}
	if len(pending) != 1 || pending[0].ID != id2 {
		t.Errorf("after delete, want only id2, got %+v", pending)
	}
}

func TestCountPending(t *testing.T) {
	db, err := openDB(":memory:")
	if err != nil {
		t.Fatal(err)
	}
	defer db.Close()

	n, err := countPending(db)
	if err != nil {
		t.Fatal(err)
	}
	if n != 0 {
		t.Errorf("want 0, got %d", n)
	}

	for i := 0; i < 3; i++ {
		if _, err := insertMessage(db, "x", "y", int64(1700000000+i)); err != nil {
			t.Fatal(err)
		}
	}

	n, err = countPending(db)
	if err != nil {
		t.Fatal(err)
	}
	if n != 3 {
		t.Errorf("want 3, got %d", n)
	}
}

func TestDeleteByIDsIgnoresMissing(t *testing.T) {
	db, err := openDB(":memory:")
	if err != nil {
		t.Fatal(err)
	}
	defer db.Close()

	id, err := insertMessage(db, "alice", "hello", 1700000000)
	if err != nil {
		t.Fatal(err)
	}

	// Delete a mix of real + missing IDs. Missing IDs are silently ignored.
	if err := deleteByIDs(db, []int64{id, 99999}); err != nil {
		t.Fatalf("deleteByIDs with missing id should not error: %v", err)
	}
	n, _ := countPending(db)
	if n != 0 {
		t.Errorf("want 0 after deleting both, got %d", n)
	}
}
```

- [ ] **Step 2: Run tests, expect compile failure**

```bash
go test ./...
```

Expected: undefined symbols `insertMessage`, `listPending`, `deleteByIDs`, `countPending`, and the `Message` type.

- [ ] **Step 3: Implement the CRUD functions**

Append this to `/Users/myles/dev/little-printer/fly-message-queue/db.go`:

```go
type Message struct {
	ID        int64  `json:"id"`
	Sender    string `json:"sender"`
	Message   string `json:"message"`
	CreatedAt int64  `json:"created_at"`
}

func insertMessage(db *sql.DB, sender, message string, createdAt int64) (int64, error) {
	res, err := db.Exec(
		`INSERT INTO messages (sender, message, created_at) VALUES (?, ?, ?)`,
		sender, message, createdAt,
	)
	if err != nil {
		return 0, err
	}
	return res.LastInsertId()
}

func listPending(db *sql.DB) ([]Message, error) {
	rows, err := db.Query(
		`SELECT id, sender, message, created_at FROM messages ORDER BY created_at ASC, id ASC`,
	)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	out := []Message{}
	for rows.Next() {
		var m Message
		if err := rows.Scan(&m.ID, &m.Sender, &m.Message, &m.CreatedAt); err != nil {
			return nil, err
		}
		out = append(out, m)
	}
	return out, rows.Err()
}

func countPending(db *sql.DB) (int, error) {
	var n int
	err := db.QueryRow(`SELECT COUNT(*) FROM messages`).Scan(&n)
	return n, err
}

func deleteByIDs(db *sql.DB, ids []int64) error {
	if len(ids) == 0 {
		return nil
	}
	tx, err := db.Begin()
	if err != nil {
		return err
	}
	stmt, err := tx.Prepare(`DELETE FROM messages WHERE id = ?`)
	if err != nil {
		tx.Rollback()
		return err
	}
	defer stmt.Close()
	for _, id := range ids {
		if _, err := stmt.Exec(id); err != nil {
			tx.Rollback()
			return err
		}
	}
	return tx.Commit()
}
```

- [ ] **Step 4: Run tests, expect PASS**

```bash
go test ./...
```

Expected: all four tests pass.

- [ ] **Step 5: Commit**

```bash
cd /Users/myles/dev/little-printer
git add fly-message-queue/db.go fly-message-queue/db_test.go
git commit -m "fly-message-queue: DB CRUD (insert/list/count/delete)"
```

---

### Task A4: Validation helpers

**Files:**
- Create: `/Users/myles/dev/little-printer/fly-message-queue/validate.go`
- Create: `/Users/myles/dev/little-printer/fly-message-queue/validate_test.go`

- [ ] **Step 1: Write the failing tests**

Write this exact content to `/Users/myles/dev/little-printer/fly-message-queue/validate_test.go`:

```go
package main

import "testing"

func TestValidateSender(t *testing.T) {
	cases := []struct {
		in        string
		wantOK    bool
		wantClean string
	}{
		{"alice", true, "alice"},
		{"  alice  ", true, "alice"},          // trimmed
		{"", false, ""},                        // empty
		{"   ", false, ""},                     // whitespace-only
		{"abcdefghijklmnopqrstuvwx", true, "abcdefghijklmnopqrstuvwx"}, // exactly 24
		{"abcdefghijklmnopqrstuvwxy", false, ""},                       // 25 — too long
	}
	for _, c := range cases {
		got, ok := validateSender(c.in)
		if ok != c.wantOK {
			t.Errorf("validateSender(%q): ok=%v want %v", c.in, ok, c.wantOK)
		}
		if ok && got != c.wantClean {
			t.Errorf("validateSender(%q): got %q want %q", c.in, got, c.wantClean)
		}
	}
}

func TestValidateMessage(t *testing.T) {
	long := ""
	for i := 0; i < 280; i++ {
		long += "a"
	}
	tooLong := long + "a"

	cases := []struct {
		in     string
		wantOK bool
	}{
		{"hello", true},
		{"  hello  ", true},
		{"", false},
		{"   ", false},
		{long, true},     // exactly 280
		{tooLong, false}, // 281 — too long
	}
	for _, c := range cases {
		_, ok := validateMessage(c.in)
		if ok != c.wantOK {
			t.Errorf("validateMessage(len=%d): ok=%v want %v", len(c.in), ok, c.wantOK)
		}
	}
}
```

- [ ] **Step 2: Run tests, expect compile failure**

```bash
go test ./...
```

Expected: `undefined: validateSender`, `undefined: validateMessage`.

- [ ] **Step 3: Implement**

Write this exact content to `/Users/myles/dev/little-printer/fly-message-queue/validate.go`:

```go
package main

import "strings"

const (
	maxSenderLen  = 24
	maxMessageLen = 280
	maxQueueSize  = 5
)

// validateSender trims whitespace and verifies length. Returns the cleaned
// value and true on success.
func validateSender(s string) (string, bool) {
	s = strings.TrimSpace(s)
	if s == "" || len(s) > maxSenderLen {
		return "", false
	}
	return s, true
}

// validateMessage trims whitespace and verifies length.
func validateMessage(s string) (string, bool) {
	s = strings.TrimSpace(s)
	if s == "" || len(s) > maxMessageLen {
		return "", false
	}
	return s, true
}
```

- [ ] **Step 4: Run tests, expect PASS**

```bash
go test ./...
```

- [ ] **Step 5: Commit**

```bash
cd /Users/myles/dev/little-printer
git add fly-message-queue/validate.go fly-message-queue/validate_test.go
git commit -m "fly-message-queue: validation helpers"
```

---

### Task A5: Read endpoints (form + /pending)

**Files:**
- Create: `/Users/myles/dev/little-printer/fly-message-queue/handlers.go`
- Create: `/Users/myles/dev/little-printer/fly-message-queue/handlers_test.go`
- Create: `/Users/myles/dev/little-printer/fly-message-queue/templates/index.html`

- [ ] **Step 1: Write the failing tests**

Write this exact content to `/Users/myles/dev/little-printer/fly-message-queue/handlers_test.go`:

```go
package main

import (
	"encoding/json"
	"io"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
)

func newTestServer(t *testing.T) *httptest.Server {
	t.Helper()
	db, err := openDB(":memory:")
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { db.Close() })

	srv := httptest.NewServer(newRouter(db, "test-token"))
	t.Cleanup(srv.Close)
	return srv
}

func TestFormPageRenders(t *testing.T) {
	srv := newTestServer(t)
	resp, err := http.Get(srv.URL + "/")
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != 200 {
		t.Fatalf("status: %d", resp.StatusCode)
	}
	body, _ := io.ReadAll(resp.Body)
	if !strings.Contains(string(body), "<form") {
		t.Errorf("body did not contain a <form>: %q", string(body))
	}
	if !strings.Contains(string(body), "0 / 5") {
		t.Errorf("body did not contain queue indicator: %q", string(body))
	}
}

func TestPendingRequiresAuth(t *testing.T) {
	srv := newTestServer(t)
	resp, err := http.Get(srv.URL + "/pending")
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != 401 {
		t.Errorf("want 401, got %d", resp.StatusCode)
	}
}

func TestPendingReturnsEmptyArray(t *testing.T) {
	srv := newTestServer(t)
	req, _ := http.NewRequest("GET", srv.URL+"/pending", nil)
	req.Header.Set("Authorization", "Bearer test-token")
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != 200 {
		t.Fatalf("status: %d", resp.StatusCode)
	}
	var got []Message
	if err := json.NewDecoder(resp.Body).Decode(&got); err != nil {
		t.Fatal(err)
	}
	if len(got) != 0 {
		t.Errorf("want empty, got %+v", got)
	}
}
```

- [ ] **Step 2: Run tests, expect compile failure**

```bash
go test ./...
```

Expected: undefined `newRouter`.

- [ ] **Step 3: Create the form template**

Write this exact content to `/Users/myles/dev/little-printer/fly-message-queue/templates/index.html`:

```html
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <title>little-printer</title>
  <style>
    body { font-family: system-ui, sans-serif; max-width: 32rem; margin: 3rem auto; padding: 0 1rem; line-height: 1.5; }
    label { display: block; margin-top: 1rem; font-weight: 600; }
    input, textarea { width: 100%; box-sizing: border-box; padding: 0.5rem; font-family: inherit; font-size: 1rem; }
    textarea { min-height: 6rem; resize: vertical; }
    .queue { color: #666; font-size: 0.9rem; margin-bottom: 1rem; }
    button { margin-top: 1rem; padding: 0.5rem 1.5rem; font-size: 1rem; cursor: pointer; }
    .footer { margin-top: 2rem; color: #888; font-size: 0.85rem; }
    .ok    { color: #2a7; }
    .err   { color: #b22; }
  </style>
</head>
<body>
  <h1>send a message to my thermal printer</h1>
  <p class="queue">queue: {{.QueueSize}} / {{.QueueMax}}</p>
  {{if .Status}}<p class="{{.StatusClass}}">{{.Status}}</p>{{end}}
  <form method="post" action="/submit">
    <label for="sender">your name <small>(max 24 chars)</small></label>
    <input id="sender" name="sender" maxlength="24" required value="{{.Sender}}">
    <label for="message">message <small>(max 280 chars)</small></label>
    <textarea id="message" name="message" maxlength="280" required>{{.MessageText}}</textarea>
    <button type="submit">queue it</button>
  </form>
  <p class="footer">messages print at 09:00 Pacific the next morning. up to 5 queued at a time.</p>
</body>
</html>
```

- [ ] **Step 4: Implement the read handlers**

Write this exact content to `/Users/myles/dev/little-printer/fly-message-queue/handlers.go`:

```go
package main

import (
	"database/sql"
	"encoding/json"
	"html/template"
	"log"
	"net/http"
	"strings"
)

type formData struct {
	QueueSize   int
	QueueMax    int
	Status      string
	StatusClass string
	Sender      string
	MessageText string
}

// newRouter wires up handlers against the given DB and bearer token.
func newRouter(db *sql.DB, token string) http.Handler {
	tmpl := template.Must(template.ParseFiles("templates/index.html"))
	mux := http.NewServeMux()

	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/" {
			http.NotFound(w, r)
			return
		}
		switch r.Method {
		case http.MethodGet:
			renderForm(w, db, tmpl, formData{})
		case http.MethodPost:
			http.Error(w, "not implemented", http.StatusNotImplemented)
		default:
			http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		}
	})

	mux.HandleFunc("/submit", func(w http.ResponseWriter, r *http.Request) {
		http.Error(w, "not implemented", http.StatusNotImplemented)
	})

	mux.HandleFunc("/pending", func(w http.ResponseWriter, r *http.Request) {
		if !checkAuth(r, token) {
			http.Error(w, "unauthorized", http.StatusUnauthorized)
			return
		}
		msgs, err := listPending(db)
		if err != nil {
			log.Printf("listPending: %v", err)
			http.Error(w, "internal error", http.StatusInternalServerError)
			return
		}
		w.Header().Set("Content-Type", "application/json")
		_ = json.NewEncoder(w).Encode(msgs)
	})

	mux.HandleFunc("/confirm", func(w http.ResponseWriter, r *http.Request) {
		http.Error(w, "not implemented", http.StatusNotImplemented)
	})

	return mux
}

func renderForm(w http.ResponseWriter, db *sql.DB, tmpl *template.Template, data formData) {
	n, err := countPending(db)
	if err != nil {
		log.Printf("countPending: %v", err)
		http.Error(w, "internal error", http.StatusInternalServerError)
		return
	}
	data.QueueSize = n
	data.QueueMax = maxQueueSize
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	if err := tmpl.Execute(w, data); err != nil {
		log.Printf("template execute: %v", err)
	}
}

func checkAuth(r *http.Request, token string) bool {
	header := r.Header.Get("Authorization")
	if !strings.HasPrefix(header, "Bearer ") {
		return false
	}
	return strings.TrimPrefix(header, "Bearer ") == token
}
```

Note: the `templates/index.html` file path is relative. Tests must be run from inside `fly-message-queue/`, which `go test` does by default. Production deployment ships the templates dir alongside the binary (handled in the Dockerfile in Task A8).

- [ ] **Step 5: Run tests, expect PASS**

```bash
cd /Users/myles/dev/little-printer/fly-message-queue
go test ./...
```

Expected: all tests pass (form renders with `0 / 5`, /pending returns 401 without auth and `[]` with auth).

- [ ] **Step 6: Commit**

```bash
cd /Users/myles/dev/little-printer
git add fly-message-queue/handlers.go fly-message-queue/handlers_test.go fly-message-queue/templates
git commit -m "fly-message-queue: form + /pending read endpoints"
```

---

### Task A6: Write endpoints (/submit, /confirm)

**Files:**
- Modify: `/Users/myles/dev/little-printer/fly-message-queue/handlers.go`
- Modify: `/Users/myles/dev/little-printer/fly-message-queue/handlers_test.go`

- [ ] **Step 1: Add the failing tests**

Append this to `/Users/myles/dev/little-printer/fly-message-queue/handlers_test.go`:

```go
func TestSubmitInsertsMessage(t *testing.T) {
	srv := newTestServer(t)
	form := strings.NewReader("sender=alice&message=hello+there")
	resp, err := http.Post(srv.URL+"/submit", "application/x-www-form-urlencoded", form)
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != 201 && resp.StatusCode != 303 {
		body, _ := io.ReadAll(resp.Body)
		t.Fatalf("status: %d, body: %s", resp.StatusCode, string(body))
	}

	// Now /pending should return 1 row.
	req, _ := http.NewRequest("GET", srv.URL+"/pending", nil)
	req.Header.Set("Authorization", "Bearer test-token")
	resp2, err := http.DefaultClient.Do(req)
	if err != nil {
		t.Fatal(err)
	}
	defer resp2.Body.Close()
	var got []Message
	json.NewDecoder(resp2.Body).Decode(&got)
	if len(got) != 1 || got[0].Sender != "alice" || got[0].Message != "hello there" {
		t.Errorf("unexpected pending: %+v", got)
	}
}

func TestSubmitRejectsTooLong(t *testing.T) {
	srv := newTestServer(t)
	long := strings.Repeat("a", 281)
	form := strings.NewReader("sender=alice&message=" + long)
	resp, err := http.Post(srv.URL+"/submit", "application/x-www-form-urlencoded", form)
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != 400 {
		t.Errorf("want 400, got %d", resp.StatusCode)
	}
}

func TestSubmitRejectsWhenQueueFull(t *testing.T) {
	srv := newTestServer(t)
	for i := 0; i < 5; i++ {
		form := strings.NewReader("sender=alice&message=hello")
		resp, _ := http.Post(srv.URL+"/submit", "application/x-www-form-urlencoded", form)
		resp.Body.Close()
	}
	// 6th submission should be rejected.
	form := strings.NewReader("sender=alice&message=hello")
	resp, err := http.Post(srv.URL+"/submit", "application/x-www-form-urlencoded", form)
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != 429 {
		t.Errorf("want 429, got %d", resp.StatusCode)
	}
}

func TestConfirmDeletes(t *testing.T) {
	srv := newTestServer(t)

	// Seed 2 messages.
	for i := 0; i < 2; i++ {
		form := strings.NewReader("sender=alice&message=msg")
		resp, _ := http.Post(srv.URL+"/submit", "application/x-www-form-urlencoded", form)
		resp.Body.Close()
	}

	// Fetch the IDs.
	req, _ := http.NewRequest("GET", srv.URL+"/pending", nil)
	req.Header.Set("Authorization", "Bearer test-token")
	resp, _ := http.DefaultClient.Do(req)
	var got []Message
	json.NewDecoder(resp.Body).Decode(&got)
	resp.Body.Close()
	if len(got) != 2 {
		t.Fatalf("seed: want 2 pending, got %d", len(got))
	}

	// Confirm both.
	body := strings.NewReader(fmt.Sprintf(`{"ids":[%d,%d]}`, got[0].ID, got[1].ID))
	req2, _ := http.NewRequest("POST", srv.URL+"/confirm", body)
	req2.Header.Set("Authorization", "Bearer test-token")
	req2.Header.Set("Content-Type", "application/json")
	resp2, err := http.DefaultClient.Do(req2)
	if err != nil {
		t.Fatal(err)
	}
	defer resp2.Body.Close()
	if resp2.StatusCode != 204 {
		t.Errorf("want 204, got %d", resp2.StatusCode)
	}

	// /pending now empty.
	req3, _ := http.NewRequest("GET", srv.URL+"/pending", nil)
	req3.Header.Set("Authorization", "Bearer test-token")
	resp3, _ := http.DefaultClient.Do(req3)
	defer resp3.Body.Close()
	var after []Message
	json.NewDecoder(resp3.Body).Decode(&after)
	if len(after) != 0 {
		t.Errorf("after confirm, want empty pending, got %+v", after)
	}
}

func TestConfirmRequiresAuth(t *testing.T) {
	srv := newTestServer(t)
	body := strings.NewReader(`{"ids":[1]}`)
	resp, err := http.Post(srv.URL+"/confirm", "application/json", body)
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != 401 {
		t.Errorf("want 401, got %d", resp.StatusCode)
	}
}
```

Make sure the test file's import block at the top includes `"fmt"` (the new test in `TestConfirmDeletes` uses `fmt.Sprintf`). The full import list after this task is:

```go
import (
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
)
```

(The `jsonInt64s` / `stringFromInt64` helpers in the test file are intentionally lo-fi — they avoid pulling `strconv` into the test for one usage. If `strconv` is preferred for clarity, replace with `strconv.FormatInt(id, 10)`.)

- [ ] **Step 2: Run tests, expect failures**

```bash
go test ./...
```

Expected: handlers return 501 (not implemented), tests fail.

- [ ] **Step 3: Implement /submit**

Replace the stub `/submit` handler in `handlers.go` with:

```go
	mux.HandleFunc("/submit", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodPost {
			http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
			return
		}
		if err := r.ParseForm(); err != nil {
			http.Error(w, "bad form", http.StatusBadRequest)
			return
		}
		sender, ok := validateSender(r.PostFormValue("sender"))
		if !ok {
			http.Error(w, "sender must be 1-24 characters", http.StatusBadRequest)
			return
		}
		message, ok := validateMessage(r.PostFormValue("message"))
		if !ok {
			http.Error(w, "message must be 1-280 characters", http.StatusBadRequest)
			return
		}
		n, err := countPending(db)
		if err != nil {
			log.Printf("countPending: %v", err)
			http.Error(w, "internal error", http.StatusInternalServerError)
			return
		}
		if n >= maxQueueSize {
			http.Error(w, "queue full — try again after tomorrow's print", http.StatusTooManyRequests)
			return
		}
		if _, err := insertMessage(db, sender, message, time.Now().Unix()); err != nil {
			log.Printf("insertMessage: %v", err)
			http.Error(w, "internal error", http.StatusInternalServerError)
			return
		}
		// Render the form again with a "queued!" status.
		renderForm(w, db, tmpl, formData{
			Status:      "queued — thanks!",
			StatusClass: "ok",
		})
	})
```

Add `"time"` to the import block at the top of the file (next to `"strings"` etc).

The status code on a successful HTML form render is 200, not 201. The test accepts either 201 or 303 — adjust the test expectation accordingly. Cleaner: change the test to accept 200:

In `handlers_test.go` `TestSubmitInsertsMessage`, replace:
```go
	if resp.StatusCode != 201 && resp.StatusCode != 303 {
```
with:
```go
	if resp.StatusCode != 200 {
```

- [ ] **Step 4: Implement /confirm**

Replace the stub `/confirm` handler with:

```go
	mux.HandleFunc("/confirm", func(w http.ResponseWriter, r *http.Request) {
		if !checkAuth(r, token) {
			http.Error(w, "unauthorized", http.StatusUnauthorized)
			return
		}
		if r.Method != http.MethodPost {
			http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
			return
		}
		var body struct {
			IDs []int64 `json:"ids"`
		}
		if err := json.NewDecoder(r.Body).Decode(&body); err != nil {
			http.Error(w, "bad json", http.StatusBadRequest)
			return
		}
		if err := deleteByIDs(db, body.IDs); err != nil {
			log.Printf("deleteByIDs: %v", err)
			http.Error(w, "internal error", http.StatusInternalServerError)
			return
		}
		w.WriteHeader(http.StatusNoContent)
	})
```

- [ ] **Step 5: Run tests, expect PASS**

```bash
go test ./...
```

Expected: all tests pass.

- [ ] **Step 6: Commit**

```bash
cd /Users/myles/dev/little-printer
git add fly-message-queue/handlers.go fly-message-queue/handlers_test.go
git commit -m "fly-message-queue: /submit and /confirm endpoints"
```

---

### Task A7: main.go wiring + local smoke test

**Files:**
- Modify: `/Users/myles/dev/little-printer/fly-message-queue/main.go`

- [ ] **Step 1: Replace main.go with the wired version**

Write this exact content to `/Users/myles/dev/little-printer/fly-message-queue/main.go`:

```go
package main

import (
	"log"
	"net/http"
	"os"
)

func main() {
	addr := os.Getenv("LISTEN_ADDR")
	if addr == "" {
		addr = ":8080"
	}
	dbPath := os.Getenv("DB_PATH")
	if dbPath == "" {
		dbPath = "queue.db"
	}
	token := os.Getenv("PRINTER_TOKEN")
	if token == "" {
		log.Fatal("PRINTER_TOKEN environment variable is required")
	}

	db, err := openDB(dbPath)
	if err != nil {
		log.Fatalf("openDB(%s): %v", dbPath, err)
	}
	defer db.Close()

	router := newRouter(db, token)
	log.Printf("starting on %s (db=%s)", addr, dbPath)
	if err := http.ListenAndServe(addr, logRequests(router)); err != nil {
		log.Fatal(err)
	}
}

// logRequests is a minimal middleware: logs method, path, and remote addr.
func logRequests(h http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		log.Printf("%s %s %s", r.RemoteAddr, r.Method, r.URL.Path)
		h.ServeHTTP(w, r)
	})
}
```

- [ ] **Step 2: Build**

```bash
cd /Users/myles/dev/little-printer/fly-message-queue
go build ./...
```

Expected: clean build.

- [ ] **Step 3: Local smoke test**

In one shell:
```bash
cd /Users/myles/dev/little-printer/fly-message-queue
PRINTER_TOKEN=local-test ./fly-message-queue
```

In another shell:
```bash
# Form renders
curl -s http://localhost:8080/ | head -20

# /pending requires auth
curl -i http://localhost:8080/pending
# expect 401

# Submit a message
curl -i -X POST http://localhost:8080/submit \
    -d 'sender=alice' --data-urlencode 'message=hello world'
# expect 200

# Pending now has 1 row
curl -s -H 'Authorization: Bearer local-test' http://localhost:8080/pending | head

# Confirm
curl -i -X POST -H 'Authorization: Bearer local-test' \
    -H 'Content-Type: application/json' \
    -d '{"ids":[1]}' http://localhost:8080/confirm
# expect 204

# Pending empty again
curl -s -H 'Authorization: Bearer local-test' http://localhost:8080/pending
# expect []
```

Stop the server with Ctrl+C. Delete the `queue.db` file in the working directory (already in .gitignore).

- [ ] **Step 4: Commit**

```bash
cd /Users/myles/dev/little-printer
git add fly-message-queue/main.go
git commit -m "fly-message-queue: wire main.go (env config + middleware)"
```

---

### Task A8: Dockerfile + fly.toml

**Files:**
- Create: `/Users/myles/dev/little-printer/fly-message-queue/Dockerfile`
- Create: `/Users/myles/dev/little-printer/fly-message-queue/fly.toml`

- [ ] **Step 1: Dockerfile**

Write this exact content to `/Users/myles/dev/little-printer/fly-message-queue/Dockerfile`:

```dockerfile
# Build stage
FROM golang:1.22-alpine AS build
WORKDIR /src
COPY go.mod go.sum ./
RUN go mod download
COPY . .
RUN CGO_ENABLED=0 go build -ldflags="-s -w" -o /out/fly-message-queue ./

# Runtime stage
FROM gcr.io/distroless/static-debian12
WORKDIR /app
COPY --from=build /out/fly-message-queue /app/fly-message-queue
COPY templates /app/templates
ENV DB_PATH=/data/queue.db
ENV LISTEN_ADDR=:8080
EXPOSE 8080
ENTRYPOINT ["/app/fly-message-queue"]
```

- [ ] **Step 2: fly.toml**

Write this exact content to `/Users/myles/dev/little-printer/fly-message-queue/fly.toml`:

```toml
# Fly config — adjust app name on first `fly launch`.
app = "little-printer"
primary_region = "sjc"

[build]

[env]
  LISTEN_ADDR = ":8080"
  DB_PATH     = "/data/queue.db"

[[mounts]]
  source      = "queue_data"
  destination = "/data"

[[services]]
  internal_port = 8080
  protocol      = "tcp"

  [[services.ports]]
    port     = 80
    handlers = ["http"]
    force_https = true

  [[services.ports]]
    port     = 443
    handlers = ["tls", "http"]

[[services.tcp_checks]]
  interval     = "30s"
  timeout      = "5s"
  grace_period = "10s"

[[vm]]
  cpu_kind = "shared"
  cpus     = 1
  memory   = "256mb"
```

- [ ] **Step 3: Local Dockerfile sanity check (optional but recommended)**

```bash
cd /Users/myles/dev/little-printer/fly-message-queue
docker build -t little-printer-msgs .
```

Expected: builds successfully. (If the user doesn't have Docker installed, skip; the deploy step in Task A9 builds remotely.)

- [ ] **Step 4: Commit**

```bash
cd /Users/myles/dev/little-printer
git add fly-message-queue/Dockerfile fly-message-queue/fly.toml
git commit -m "fly-message-queue: Dockerfile + fly.toml"
```

---

### Task A9: Deploy to Fly.io

**Files:** none changed. This task is user-driven.

- [ ] **Step 1: Install / authenticate flyctl**

If not already installed:
```bash
brew install flyctl
fly auth login
```

- [ ] **Step 2: First-time launch (interactive)**

```bash
cd /Users/myles/dev/little-printer/fly-message-queue
fly launch --no-deploy
```

Answer the prompts:
- Use the existing `fly.toml`? **Yes**
- App name: pick one (e.g., `little-printer-msgs`). Update `app = "..."` in `fly.toml` to match.
- Region: `sjc`
- PostgreSQL: **No**
- Redis: **No**
- Deploy now: **No**

If fly's linter complains about anything in `fly.toml`, accept its corrections.

- [ ] **Step 3: Create the volume**

```bash
fly volumes create queue_data --size 1 --region sjc
```

Expected: a volume named `queue_data` is created. Confirms with `fly volumes list`.

- [ ] **Step 4: Set the bearer token secret**

```bash
fly secrets set PRINTER_TOKEN=<TOKEN>
```

Replace `<TOKEN>` with the value generated in the pre-flight step. The secret is encrypted at rest.

- [ ] **Step 5: Deploy**

```bash
fly deploy
```

Expected: build runs remotely, image pushed, machine starts, health check passes. Log shows `starting on :8080 (db=/data/queue.db)`.

- [ ] **Step 6: Smoke-test the live deployment**

Note your app's URL — it'll be something like `https://little-printer-msgs.fly.dev`.

```bash
URL=https://little-printer-msgs.fly.dev   # adjust
TOKEN=<TOKEN>                              # the same one

# Form
curl -s "$URL/" | head -10
# Submit
curl -i -X POST "$URL/submit" -d 'sender=test' --data-urlencode 'message=live deploy works'
# Pending
curl -s -H "Authorization: Bearer $TOKEN" "$URL/pending"
# Confirm (use the id returned above)
curl -i -X POST -H "Authorization: Bearer $TOKEN" -H 'Content-Type: application/json' \
    -d '{"ids":[1]}' "$URL/confirm"
```

If everything works, the queue endpoints are healthy.

- [ ] **Step 7: Note the URL and token for Phase B**

You'll plug `$URL` into `CONFIG_MESSAGES_BASE_URL` and `$TOKEN` into `CONFIG_MESSAGES_TOKEN` during Phase B.

- [ ] **Step 8: No commit** (`fly.toml` was likely already committed; the only thing that may have changed is the `app = "..."` line if the user picked a different name, in which case commit that change.)

```bash
cd /Users/myles/dev/little-printer
git diff --quiet fly-message-queue/fly.toml || {
    git add fly-message-queue/fly.toml
    git commit -m "fly-message-queue: set app name from fly launch"
}
```

---

## Phase B — C3 firmware integration

### Task B1: Extend http_fetch with bearer-header GET + JSON POST

**Files:**
- Modify: `/Users/myles/dev/little-printer/main/http_fetch.h`
- Modify: `/Users/myles/dev/little-printer/main/http_fetch.c`

- [ ] **Step 1: Update the header**

Replace the entire content of `/Users/myles/dev/little-printer/main/http_fetch.h` with:

```c
#pragma once

#include <stddef.h>
#include "esp_err.h"

/*
 * GET `url` over HTTPS and return the response body as a heap-allocated
 * NUL-terminated C string. Caller must free() the result on success.
 *
 * Returns ESP_OK on a 2xx response, ESP_FAIL otherwise. *out is set to
 * NULL on failure.
 */
esp_err_t http_fetch(const char *url, char **out);

/*
 * Like http_fetch but also sends a single extra request header (e.g.
 * "Authorization: Bearer ..."). Pass NULL for header_name/header_value
 * to behave identically to http_fetch.
 */
esp_err_t http_fetch_with_header(const char *url,
                                 const char *header_name,
                                 const char *header_value,
                                 char **out);

/*
 * POST `body` to `url` with Content-Type: application/json and an optional
 * extra header. `out` may be NULL if the caller doesn't need the response
 * body. Returns ESP_OK on a 2xx response.
 */
esp_err_t http_post_json(const char *url,
                         const char *header_name,
                         const char *header_value,
                         const char *body,
                         char **out);
```

- [ ] **Step 2: Update the implementation**

Replace the entire content of `/Users/myles/dev/little-printer/main/http_fetch.c` with:

```c
#include "http_fetch.h"

#include <stdlib.h>
#include <string.h>

#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"

static const char *TAG = "http_fetch";

#define MAX_RESPONSE_BYTES (16 * 1024)

typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
} body_t;

static esp_err_t event_handler(esp_http_client_event_t *evt) {
    body_t *body = (body_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        if (body->len + evt->data_len + 1 > body->cap) {
            size_t new_cap = body->cap ? body->cap * 2 : 1024;
            while (new_cap < body->len + evt->data_len + 1) new_cap *= 2;
            if (new_cap > MAX_RESPONSE_BYTES) {
                ESP_LOGE(TAG, "response exceeded %d bytes; aborting", MAX_RESPONSE_BYTES);
                return ESP_FAIL;
            }
            char *grown = realloc(body->buf, new_cap);
            if (!grown) return ESP_ERR_NO_MEM;
            body->buf = grown;
            body->cap = new_cap;
        }
        memcpy(body->buf + body->len, evt->data, evt->data_len);
        body->len += evt->data_len;
        body->buf[body->len] = '\0';
    }
    return ESP_OK;
}

static esp_err_t do_request(esp_http_client_method_t method,
                            const char *url,
                            const char *header_name,
                            const char *header_value,
                            const char *content_type,
                            const char *body_str,
                            char **out) {
    if (out) *out = NULL;

    body_t body = { 0 };

    esp_http_client_config_t cfg = {
        .url = url,
        .event_handler = event_handler,
        .user_data = &body,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10 * 1000,
        .method = method,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        free(body.buf);
        return ESP_FAIL;
    }

    if (header_name && header_value) {
        esp_http_client_set_header(client, header_name, header_value);
    }
    if (content_type) {
        esp_http_client_set_header(client, "Content-Type", content_type);
    }
    if (body_str) {
        esp_http_client_set_post_field(client, body_str, strlen(body_str));
    }

    esp_err_t err = esp_http_client_perform(client);
    int status = err == ESP_OK ? esp_http_client_get_status_code(client) : -1;
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status < 200 || status >= 300) {
        ESP_LOGW(TAG, "%s failed: err=%s status=%d url=%s",
                 method == HTTP_METHOD_POST ? "POST" : "GET",
                 esp_err_to_name(err), status, url);
        free(body.buf);
        return ESP_FAIL;
    }

    if (out) {
        if (!body.buf) {
            body.buf = calloc(1, 1);
            if (!body.buf) return ESP_ERR_NO_MEM;
        }
        *out = body.buf;
    } else {
        free(body.buf);
    }
    return ESP_OK;
}

esp_err_t http_fetch(const char *url, char **out) {
    return do_request(HTTP_METHOD_GET, url, NULL, NULL, NULL, NULL, out);
}

esp_err_t http_fetch_with_header(const char *url,
                                 const char *header_name,
                                 const char *header_value,
                                 char **out) {
    return do_request(HTTP_METHOD_GET, url, header_name, header_value, NULL, NULL, out);
}

esp_err_t http_post_json(const char *url,
                         const char *header_name,
                         const char *header_value,
                         const char *body,
                         char **out) {
    return do_request(HTTP_METHOD_POST, url, header_name, header_value,
                      "application/json", body, out);
}
```

- [ ] **Step 3: Commit**

```bash
cd /Users/myles/dev/little-printer
git add main/http_fetch.c main/http_fetch.h
git commit -m "http_fetch: add header-aware GET and JSON POST variants"
```

(Build verification happens at the next on-hardware step in Task B5; `idf.py` is not on the controller's PATH.)

---

### Task B2: Kconfig entries for messages

**Files:**
- Modify: `/Users/myles/dev/little-printer/main/Kconfig.projbuild`

- [ ] **Step 1: Append the new entries**

Read the current `/Users/myles/dev/little-printer/main/Kconfig.projbuild` to confirm its content. The current `endmenu` is the last line. Insert the new entries before it.

Replace:
```
endmenu
```

With:
```kconfig
config MESSAGES_BASE_URL
    string "Base URL for the public message queue service"
    default "https://little-printer-msgs.fly.dev"
    help
      Without trailing slash. The C3 fetches GET <url>/pending and POSTs to <url>/confirm.

config MESSAGES_TOKEN
    string "Bearer token for the message queue service"
    default ""
    help
      Must match PRINTER_TOKEN on the Fly.io app. Generate with `openssl rand -hex 32`.

endmenu
```

- [ ] **Step 2: Commit**

```bash
cd /Users/myles/dev/little-printer
git add main/Kconfig.projbuild
git commit -m "kconfig: add MESSAGES_BASE_URL and MESSAGES_TOKEN"
```

---

### Task B3: messages.c/h component

**Files:**
- Create: `/Users/myles/dev/little-printer/main/messages.h`
- Create: `/Users/myles/dev/little-printer/main/messages.c`

- [ ] **Step 1: Header**

Write this exact content to `/Users/myles/dev/little-printer/main/messages.h`:

```c
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
```

- [ ] **Step 2: Implementation**

Write this exact content to `/Users/myles/dev/little-printer/main/messages.c`:

```c
#include "messages.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "cJSON.h"

#include "http_fetch.h"

static const char *TAG = "messages";

static const char *AUTH_HEADER = "Authorization";

static void make_bearer(char *buf, size_t buf_size) {
    snprintf(buf, buf_size, "Bearer %s", CONFIG_MESSAGES_TOKEN);
}

esp_err_t messages_fetch_pending(message_t **out, size_t *count) {
    *out = NULL;
    *count = 0;

    if (CONFIG_MESSAGES_TOKEN[0] == '\0') {
        ESP_LOGW(TAG, "MESSAGES_TOKEN is empty; skipping fetch");
        return ESP_FAIL;
    }

    char url[256];
    snprintf(url, sizeof(url), "%s/pending", CONFIG_MESSAGES_BASE_URL);

    char auth[128];
    make_bearer(auth, sizeof(auth));

    char *body = NULL;
    if (http_fetch_with_header(url, AUTH_HEADER, auth, &body) != ESP_OK) {
        return ESP_FAIL;
    }

    esp_err_t result = ESP_FAIL;
    cJSON *root = cJSON_Parse(body);
    if (!root || !cJSON_IsArray(root)) {
        ESP_LOGW(TAG, "expected top-level JSON array");
        goto done;
    }

    int n = cJSON_GetArraySize(root);
    if (n <= 0) {
        result = ESP_OK;  /* empty queue is success */
        goto done;
    }

    message_t *msgs = calloc((size_t)n, sizeof(message_t));
    if (!msgs) {
        ESP_LOGE(TAG, "calloc failed for %d messages", n);
        goto done;
    }

    for (int i = 0; i < n; i++) {
        cJSON *item = cJSON_GetArrayItem(root, i);
        if (!item) continue;
        cJSON *id      = cJSON_GetObjectItem(item, "id");
        cJSON *sender  = cJSON_GetObjectItem(item, "sender");
        cJSON *message = cJSON_GetObjectItem(item, "message");
        if (!id || !sender || !message) continue;

        msgs[i].id = id->valueint;
        if (cJSON_IsString(sender)) {
            strncpy(msgs[i].sender, sender->valuestring, sizeof(msgs[i].sender) - 1);
        }
        if (cJSON_IsString(message)) {
            strncpy(msgs[i].message, message->valuestring, sizeof(msgs[i].message) - 1);
        }
    }

    *out = msgs;
    *count = (size_t)n;
    result = ESP_OK;
    ESP_LOGI(TAG, "fetched %d pending message(s)", n);

done:
    cJSON_Delete(root);
    free(body);
    return result;
}

esp_err_t messages_confirm(const int *ids, size_t n) {
    if (n == 0) return ESP_OK;

    char url[256];
    snprintf(url, sizeof(url), "%s/confirm", CONFIG_MESSAGES_BASE_URL);

    char auth[128];
    make_bearer(auth, sizeof(auth));

    /* Build the JSON body: {"ids":[1,2,3]} */
    char body[256];
    int off = snprintf(body, sizeof(body), "{\"ids\":[");
    for (size_t i = 0; i < n; i++) {
        if (off >= (int)sizeof(body)) break;
        off += snprintf(body + off, sizeof(body) - off, "%s%d",
                        i == 0 ? "" : ",", ids[i]);
    }
    if (off < (int)sizeof(body)) {
        off += snprintf(body + off, sizeof(body) - off, "]}");
    }

    if (http_post_json(url, AUTH_HEADER, auth, body, NULL) != ESP_OK) {
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "confirmed %u message(s)", (unsigned)n);
    return ESP_OK;
}
```

- [ ] **Step 3: Commit**

```bash
cd /Users/myles/dev/little-printer
git add main/messages.c main/messages.h
git commit -m "messages: fetch pending + confirm helpers"
```

---

### Task B4: CMakeLists.txt update

**Files:**
- Modify: `/Users/myles/dev/little-printer/main/CMakeLists.txt`

- [ ] **Step 1: Add `messages.c` to SRCS**

Read `/Users/myles/dev/little-printer/main/CMakeLists.txt`. The current `SRCS` block lists `briefing.c` last. Add `messages.c` after it.

Replace:
```cmake
        "briefing.c"
    INCLUDE_DIRS "."
```

With:
```cmake
        "briefing.c"
        "messages.c"
    INCLUDE_DIRS "."
```

- [ ] **Step 2: Commit**

```bash
cd /Users/myles/dev/little-printer
git add main/CMakeLists.txt
git commit -m "cmake: add messages.c to main SRCS"
```

---

### Task B5: Wire messages block into briefing.c

**Files:**
- Modify: `/Users/myles/dev/little-printer/main/briefing.c`

- [ ] **Step 1: Add the include**

Read `/Users/myles/dev/little-printer/main/briefing.c`. The existing includes block ends with `#include "text_wrap.h"`. After it, add:

```c
#include "messages.h"
```

- [ ] **Step 2: Add the messages block before `briefing_run` returns**

The current function ends with:
```c
    thermal_printer_feed(1);
    thermal_printer_set_justify('C');
    thermal_printer_println("================================");
    thermal_printer_feed(3);
    thermal_printer_sleep(60);

    ESP_LOGI(TAG, "briefing_run done");
}
```

Replace those lines with:

```c
    thermal_printer_feed(1);
    thermal_printer_set_justify('C');
    thermal_printer_println("================================");

    /* Optional messages block — only if the queue has anything pending. */
    message_t *msgs = NULL;
    size_t n = 0;
    if (messages_fetch_pending(&msgs, &n) == ESP_OK && n > 0) {
        thermal_printer_feed(2);
        thermal_printer_set_justify('C');
        thermal_printer_println("----- MESSAGES -----");
        thermal_printer_feed(1);

        thermal_printer_set_justify('L');
        int printed_ids[8];
        size_t to_confirm = n > 8 ? 8 : n;
        for (size_t i = 0; i < to_confirm; i++) {
            text_wrap(msgs[i].message, PRINT_LINE_WIDTH - 4, &println_indented);
            char attribution[48];
            snprintf(attribution, sizeof(attribution), "       -- %s", msgs[i].sender);
            thermal_printer_println(attribution);
            thermal_printer_feed(1);
            printed_ids[i] = msgs[i].id;
        }

        thermal_printer_set_justify('C');
        thermal_printer_println("================================");

        messages_confirm(printed_ids, to_confirm);
    }
    free(msgs);

    thermal_printer_feed(3);
    thermal_printer_sleep(60);

    ESP_LOGI(TAG, "briefing_run done");
}
```

(The `printed_ids[8]` cap is defensive — the server enforces 5, but giving the array a little headroom protects against a server-side bug delivering more than expected.)

- [ ] **Step 3: Commit**

```bash
cd /Users/myles/dev/little-printer
git add main/briefing.c
git commit -m "briefing: print pending messages block when non-empty"
```

---

### Task B6: Hardware verification

**Files:** none changed; user runs the device.

- [ ] **Step 1: Configure C3 with the live URL + token**

```bash
cd /Users/myles/dev/little-printer
idf.py menuconfig
# Briefing Printer Config →
#   Base URL for the public message queue service: https://<your-fly-app>.fly.dev
#   Bearer token for the message queue service:    <TOKEN>
# Save (S), Quit (Q)
```

- [ ] **Step 2: Build and flash**

```bash
idf.py build flash monitor
```

Expected log on boot:
```
I (xxx) main: little-printer booting
I (xxx) wifi: got IP a.b.c.d
I (xxx) time_sync: local time: ...
I (xxx) thermal_printer: initialised on UART1 @ 9600 baud
I (xxx) main: ready — briefing scheduled for 09:00
```

- [ ] **Step 3: Seed two test messages via the form**

In a browser, visit `https://<your-fly-app>.fly.dev/`. Submit:
1. Sender `alice`, message `good luck on the interview!`
2. Sender `bob`, message `hello from london`

The form should report `queue: 2 / 5` afterwards.

- [ ] **Step 4: Trigger a manual briefing**

In the IDF monitor, type `p` and Enter. Expected log lines:
```
I (xxx) main: console: manual briefing trigger
I (xxx) briefing: briefing_run starting
... (existing weather + quote logs)
I (xxx) messages: fetched 2 pending message(s)
I (xxx) messages: confirmed 2 message(s)
I (xxx) briefing: briefing_run done
```

The printer should print:
- The normal date / weather / quote briefing
- A `----- MESSAGES -----` header
- Both messages, each with `-- alice` / `-- bob` attribution
- Closing `========` separator
- Paper feed

- [ ] **Step 5: Verify the queue is empty**

Visit the form again — it should show `queue: 0 / 5`. Or:

```bash
curl -s -H "Authorization: Bearer <TOKEN>" https://<your-fly-app>.fly.dev/pending
```

Expected: `[]`

- [ ] **Step 6: Verify queue-full rejection**

Submit 5 messages via the form. The 6th should fail with the "queue full" message (HTTP 429).

- [ ] **Step 7: Verify the no-messages path is unchanged**

With the queue empty, type `p` again. The briefing should print exactly as it did before this feature — no MESSAGES block, no extra paper.

---

## Self-review notes

**Spec coverage:**

| Spec section | Covered by |
|---|---|
| Goals | Tasks A4, A5, A6, B5 cover length caps, queue cap, print location, missed-day grace |
| Non-goals (no auth on submit, no MQTT, no moderation) | Honoured throughout — no auth check on /submit, no MQTT code, no filter code |
| Architecture (Fly authoritative, HTTPS poll, fetch-then-confirm) | Tasks A1-A9 (Fly side) + B1-B6 (firmware side) |
| Fly app stack (Go, modernc/sqlite, distroless) | Tasks A1, A2, A8 |
| Endpoints (GET /, POST /submit, GET /pending, POST /confirm) | Tasks A5 (GET / and /pending) + A6 (POST /submit, /confirm) |
| Auth (public submit, bearer for /pending and /confirm) | Tasks A5, A6 |
| Schema (no status column) | Task A2 |
| Configuration (PRINTER_TOKEN secret, /data volume) | Tasks A8, A9 |
| Kconfig (MESSAGES_BASE_URL, MESSAGES_TOKEN) | Task B2 |
| http_fetch extension (Variant A) | Task B1 |
| messages.c/h component | Task B3 |
| briefing.c integration (after footer separator) | Task B5 |
| Failure modes (network down, /confirm fails) | The implementation in B3 returns ESP_FAIL on failures; B5's caller skips confirm if fetch failed; if confirm fails the messages reprint tomorrow per spec |
| Testing approach (Go unit + integration, firmware on-device) | Tasks A2, A3, A4, A5, A6 (Go); Task B6 (firmware) |
| Repo layout (sibling subdirectory) | Task A1 |

**Placeholder scan:** No "TBD", "TODO", or "appropriate" language. Every step contains literal code, exact paths, or precise commands. The `<TOKEN>` and `<your-fly-app>` placeholders in Task A9 / B6 are user-substituted secrets/URLs, not code-quality placeholders.

**Type / identifier consistency:**
- `Message` struct (Go) and `message_t` struct (C) have matching fields (`id`, `sender`, `message`, `created_at` on the Go side; the C side omits `created_at` since the firmware doesn't need it).
- `validateSender`, `validateMessage`, `maxQueueSize`, `maxSenderLen`, `maxMessageLen` referenced consistently.
- `newRouter(db, token)` signature matches across handlers.go and tests.
- `messages_fetch_pending`, `messages_confirm` signatures match between header and impl and call sites.
- `CONFIG_MESSAGES_BASE_URL` and `CONFIG_MESSAGES_TOKEN` referenced consistently in messages.c and Kconfig.
- `PRINTER_TOKEN` (Fly secret) and `MESSAGES_TOKEN` (C3 Kconfig) are different names but represent the same value; spec notes this — kept this way because Fly's secret is server-side ("the printer's token") and the C3's Kconfig is what the firmware module is named (messages).

**Open assumptions:**
- User has Go 1.22+ installed for `go test` / `go build`. If they're behind, the SQLite driver still works on 1.21+.
- User has flyctl + a Fly.io account for Task A9. If not, the firmware portion (Phase B) can still be implemented but B6 verification will fail until the service is live somewhere.
- The `app = "little-printer"` line in `fly.toml` may collide with another user's app name — `fly launch` will prompt for a new name.
