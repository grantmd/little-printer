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
