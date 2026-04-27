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
