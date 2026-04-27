package main

import (
	"encoding/json"
	"fmt"
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

func TestSubmitInsertsMessage(t *testing.T) {
	srv := newTestServer(t)
	form := strings.NewReader("sender=alice&message=hello+there")
	resp, err := http.Post(srv.URL+"/submit", "application/x-www-form-urlencoded", form)
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != 200 {
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
