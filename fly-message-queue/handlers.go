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
