/*
 * Minimal C test framework shared across native/zero-c/tests/.
 *
 * Usage:
 *   #include "test_framework.h"
 *
 *   static void test_something(void) {
 *     BEGIN_CASE("descriptive name shown on failure");
 *     ASSERT_EQ_INT(1 + 1, 2);
 *     ASSERT_EQ_STR(strstr("foobar", "bar"), "bar");
 *     ASSERT_TRUE(some_predicate());
 *   }
 *
 *   int main(void) {
 *     test_something();
 *     return TEST_SUMMARY();
 *   }
 *
 * Each test binary is its own translation unit, so the static globals below
 * give every binary its own counters without link-time conflicts.
 */
#ifndef ZERO_TEST_FRAMEWORK_H
#define ZERO_TEST_FRAMEWORK_H

#include <stdio.h>
#include <string.h>

/* Render a byte for diagnostics: "'h' (104)" for printable ASCII,
 * "(10)" for control chars / non-ASCII. Buf must be at least 12 bytes. */
static inline void zt_fmt_char(char *buf, size_t cap, int c) {
  unsigned char b = (unsigned char)c;
  if (b >= 0x20 && b < 0x7f && b != '\'' && b != '\\') {
    snprintf(buf, cap, "'%c' (%d)", (char)b, b);
  } else {
    snprintf(buf, cap, "(%d)", b);
  }
}

static int g_failures = 0;
static int g_assertions = 0;
static const char *g_case = "";

#define BEGIN_CASE(name) do { g_case = (name); } while (0)

#define FAIL(...) do { \
  g_failures++; \
  fprintf(stderr, "  FAIL [%s]: ", g_case); \
  fprintf(stderr, __VA_ARGS__); \
  fprintf(stderr, " (at %s:%d)\n", __FILE__, __LINE__); \
} while (0)

#define ASSERT_TRUE(expr) do { \
  g_assertions++; \
  if (!(expr)) FAIL("expected: %s", #expr); \
} while (0)

#define ASSERT_FALSE(expr) do { \
  g_assertions++; \
  if (expr) FAIL("expected NOT: %s", #expr); \
} while (0)

#define ASSERT_EQ_INT(actual, expected) do { \
  g_assertions++; \
  long _a = (long)(actual), _e = (long)(expected); \
  if (_a != _e) FAIL("expected %ld, got %ld (%s)", _e, _a, #actual); \
} while (0)

#define ASSERT_EQ_CHAR(actual, expected) do { \
  g_assertions++; \
  int _a = (unsigned char)(actual), _e = (unsigned char)(expected); \
  if (_a != _e) { \
    char _ab[12], _eb[12]; \
    zt_fmt_char(_ab, sizeof(_ab), _a); \
    zt_fmt_char(_eb, sizeof(_eb), _e); \
    FAIL("expected %s, got %s (%s)", _eb, _ab, #actual); \
  } \
} while (0)

#define ASSERT_EQ_STR(actual, expected) do { \
  g_assertions++; \
  const char *_a = (actual), *_e = (expected); \
  if (!_a || strcmp(_a, _e) != 0) FAIL("expected \"%s\", got \"%s\"", _e, _a ? _a : "(null)"); \
} while (0)

#define ASSERT_NOT_NULL(ptr) do { \
  g_assertions++; \
  if ((ptr) == NULL) FAIL("expected non-NULL: %s", #ptr); \
} while (0)

#define ASSERT_NULL(ptr) do { \
  g_assertions++; \
  if ((ptr) != NULL) FAIL("expected NULL: %s", #ptr); \
} while (0)

/* Print summary and return exit code: 0 on full pass, 1 if any failure. */
#define TEST_SUMMARY() ( \
  printf("\n%d assertions, %d failures\n", g_assertions, g_failures), \
  g_failures == 0 ? 0 : 1 \
)

#endif /* ZERO_TEST_FRAMEWORK_H */
