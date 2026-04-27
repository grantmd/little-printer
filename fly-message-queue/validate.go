package main

import "strings"

const (
	maxSenderLen  = 24
	maxMessageLen = 280
	maxQueueSize  = 3
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
