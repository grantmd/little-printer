package main

import (
	"database/sql"
	"encoding/json"
	"html/template"
	"log"
	"net/http"
	"strings"
	"time"
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

	/* Serve files from ./static/ at /static/. Used for the page's image. */
	mux.Handle("/static/", http.StripPrefix("/static/", http.FileServer(http.Dir("static"))))

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
