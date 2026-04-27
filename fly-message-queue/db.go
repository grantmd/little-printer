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
