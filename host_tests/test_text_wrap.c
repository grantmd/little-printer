#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "../main/text_wrap.h"

#define MAX_LINES 32
static char captured[MAX_LINES][128];
static int captured_count;

static void capture(const char *line) {
    if (captured_count < MAX_LINES) {
        strncpy(captured[captured_count], line, sizeof(captured[0]) - 1);
        captured[captured_count][sizeof(captured[0]) - 1] = '\0';
        captured_count++;
    }
}

static void reset(void) { captured_count = 0; }

#define ASSERT_LINES(expected_count) do {                                   \
    if (captured_count != (expected_count)) {                               \
        fprintf(stderr, "FAIL %s: expected %d lines, got %d\n",             \
                __func__, (expected_count), captured_count);                \
        for (int i = 0; i < captured_count; i++) {                          \
            fprintf(stderr, "  [%d] '%s'\n", i, captured[i]);               \
        }                                                                   \
        exit(1);                                                            \
    }                                                                       \
} while (0)

#define ASSERT_LINE(idx, expected) do {                                     \
    if (strcmp(captured[idx], (expected)) != 0) {                           \
        fprintf(stderr, "FAIL %s: line %d expected '%s' got '%s'\n",        \
                __func__, (idx), (expected), captured[idx]);                \
        exit(1);                                                            \
    }                                                                       \
} while (0)

static void test_short_input(void) {
    reset();
    text_wrap("hello world", 32, capture);
    ASSERT_LINES(1);
    ASSERT_LINE(0, "hello world");
}

static void test_wraps_at_word_boundary(void) {
    reset();
    text_wrap("the quick brown fox", 10, capture);
    /* Expected greedy wrap: "the quick" (9 chars), "brown fox" (9 chars). */
    ASSERT_LINES(2);
    ASSERT_LINE(0, "the quick");
    ASSERT_LINE(1, "brown fox");
}

static void test_collapses_whitespace(void) {
    reset();
    text_wrap("hello   world", 32, capture);
    ASSERT_LINES(1);
    ASSERT_LINE(0, "hello world");
}

static void test_word_longer_than_width(void) {
    reset();
    text_wrap("supercalifragilisticexpialidocious me", 10, capture);
    /* Long word emitted on its own line, then "me" on the next. */
    ASSERT_LINES(2);
    ASSERT_LINE(0, "supercalifragilisticexpialidocious");
    ASSERT_LINE(1, "me");
}

static void test_empty_input(void) {
    reset();
    text_wrap("", 32, capture);
    ASSERT_LINES(0);
}

int main(void) {
    test_short_input();
    test_wraps_at_word_boundary();
    test_collapses_whitespace();
    test_word_longer_than_width();
    test_empty_input();
    printf("PASS\n");
    return 0;
}
