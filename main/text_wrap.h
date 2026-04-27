#pragma once

#include <stddef.h>

typedef void (*text_wrap_emit_fn)(const char *line);

/*
 * Greedy word-wrap `in` to `width` columns, calling `emit` once per output
 * line. Whitespace runs are collapsed to a single space; words longer than
 * `width` are emitted on their own line (will overflow the line, by design).
 * `in` must be NUL-terminated.
 */
void text_wrap(const char *in, size_t width, text_wrap_emit_fn emit);
