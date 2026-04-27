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
