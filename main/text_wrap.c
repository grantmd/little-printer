#include "text_wrap.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>

void text_wrap(const char *in, size_t width, text_wrap_emit_fn emit) {
    if (!in || !emit || width == 0) return;

    char buf[256];
    size_t buf_len = 0;

    const char *p = in;
    while (*p) {
        /* Skip whitespace. */
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;

        /* Collect a word. */
        const char *word_start = p;
        while (*p && !isspace((unsigned char)*p)) p++;
        size_t word_len = (size_t)(p - word_start);
        if (word_len >= sizeof(buf)) word_len = sizeof(buf) - 1;

        /* Decide whether to emit current line and start fresh. */
        if (buf_len == 0) {
            memcpy(buf, word_start, word_len);
            buf_len = word_len;
        } else if (buf_len + 1 + word_len <= width) {
            buf[buf_len++] = ' ';
            memcpy(buf + buf_len, word_start, word_len);
            buf_len += word_len;
        } else {
            buf[buf_len] = '\0';
            emit(buf);
            memcpy(buf, word_start, word_len);
            buf_len = word_len;
        }
    }

    if (buf_len > 0) {
        buf[buf_len] = '\0';
        emit(buf);
    }
}
