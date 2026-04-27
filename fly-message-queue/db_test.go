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
