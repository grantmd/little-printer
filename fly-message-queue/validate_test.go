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
