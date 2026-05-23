/*
 * Unit tests for src/lexer.c (z_tokenize).
 *
 * Build & run from native/zero-c/:
 *   make test-lexer
 *
 */

#include "zero.h"
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- Helper: tokenize and return vec; caller must free. ---------- */
static TokenVec lex(const char *src, ZDiag *diag) {
  ZDiag local = {0};
  if (!diag) diag = &local;
  return z_tokenize(src, diag);
}

/* ================== Test cases ================== */

static void test_empty_input(void) {
  BEGIN_CASE("empty input -> EOF only");
  TokenVec t = lex("", NULL);
  ASSERT_EQ_INT(t.len, 1);
  ASSERT_EQ_INT(t.items[0].kind, TOK_EOF);
  ASSERT_EQ_INT(t.items[0].line, 1);
  ASSERT_EQ_INT(t.items[0].column, 1);
  z_free_tokens(&t);
}

static void test_whitespace_and_comment_skipped(void) {
  BEGIN_CASE("whitespace and // comments are ignored");
  TokenVec t = lex("  \n  // a comment\n  x  ", NULL);
  ASSERT_EQ_INT(t.len, 2);  /* x + EOF */
  ASSERT_EQ_INT(t.items[0].kind, TOK_IDENT);
  ASSERT_EQ_STR(t.items[0].text, "x");
  ASSERT_EQ_INT(t.items[0].line, 3);    /* third line */
  ASSERT_EQ_INT(t.items[0].column, 3);  /* after two spaces */
  ASSERT_EQ_INT(t.items[1].kind, TOK_EOF); /* EOF */
  z_free_tokens(&t);
}

static void test_identifier_vs_keyword(void) {
  BEGIN_CASE("keywords are tagged TOK_KEYWORD, others TOK_IDENT");
  TokenVec t = lex("fun let_value while_loop raises", NULL);
  /* Expected: KEYWORD(fun), IDENT(let_value), IDENT(while_loop), KEYWORD(raises), EOF */
  ASSERT_EQ_INT(t.len, 5);
  ASSERT_EQ_INT(t.items[0].kind, TOK_KEYWORD);
  ASSERT_EQ_STR(t.items[0].text, "fun");
  ASSERT_EQ_INT(t.items[1].kind, TOK_IDENT);
  ASSERT_EQ_STR(t.items[1].text, "let_value");
  ASSERT_EQ_INT(t.items[2].kind, TOK_IDENT);
  ASSERT_EQ_STR(t.items[2].text, "while_loop");
  ASSERT_EQ_INT(t.items[3].kind, TOK_KEYWORD);
  ASSERT_EQ_STR(t.items[3].text, "raises");
  ASSERT_EQ_INT(t.items[4].kind, TOK_EOF);
  z_free_tokens(&t);
}

static void test_numbers(void) {
  BEGIN_CASE("integer and decimal numbers are TOK_NUMBER");
  TokenVec t = lex("0 42 3.14 1_000", NULL);
  ASSERT_EQ_INT(t.len, 5);
  for (int i = 0; i < 4; i++) ASSERT_EQ_INT(t.items[i].kind, TOK_NUMBER);
  ASSERT_EQ_STR(t.items[0].text, "0");
  ASSERT_EQ_STR(t.items[1].text, "42");
  ASSERT_EQ_STR(t.items[2].text, "3.14");
  ASSERT_EQ_STR(t.items[3].text, "1_000");
  z_free_tokens(&t);
}

static void test_string_with_escape(void) {
  BEGIN_CASE("string literal decodes \\n escape");
  TokenVec t = lex("\"hello\\nworld\"", NULL);
  ASSERT_EQ_INT(t.len, 2);
  ASSERT_EQ_INT(t.items[0].kind, TOK_STRING);
  ASSERT_EQ_STR(t.items[0].text, "hello\nworld");
  ASSERT_EQ_INT(t.items[0].line, 1);
  ASSERT_EQ_INT(t.items[0].column, 1);
  ASSERT_EQ_INT(t.items[0].offset, 0);
  // 14 (包括两边的引号 + \n的字面两字符)
  ASSERT_EQ_INT(t.items[0].length, 14);
  /* Decoded .text is 11 bytes: h e l l o LF w o r l d, then a NUL terminator. */
  // 11 (去引号 + \n合并成单字节)
  ASSERT_EQ_INT(strlen(t.items[0].text), 11);
  ASSERT_EQ_CHAR(t.items[0].text[0], 'h');
  ASSERT_EQ_CHAR(t.items[0].text[1], 'e');
  ASSERT_EQ_CHAR(t.items[0].text[2], 'l');
  ASSERT_EQ_CHAR(t.items[0].text[3], 'l');
  ASSERT_EQ_CHAR(t.items[0].text[4], 'o');
  ASSERT_EQ_CHAR(t.items[0].text[5], '\n'); /* the decoded LF byte */
  ASSERT_EQ_CHAR(t.items[0].text[6], 'w');
  ASSERT_EQ_CHAR(t.items[0].text[7], 'o');
  ASSERT_EQ_CHAR(t.items[0].text[8], 'r');
  ASSERT_EQ_CHAR(t.items[0].text[9], 'l');
  ASSERT_EQ_CHAR(t.items[0].text[10], 'd');
  ASSERT_EQ_CHAR(t.items[0].text[11], '\0'); /* NUL terminator at strlen */
  z_free_tokens(&t);
}

static void test_char_literal(void) {
  BEGIN_CASE("char literal stored as decimal byte value");
  TokenVec t = lex("'a' '\\n' '\\x41'", NULL);
  /* 'a' -> 97, '\n' -> 10, '\x41' -> 65 */
  ASSERT_EQ_INT(t.len, 4);
  ASSERT_EQ_INT(t.items[0].kind, TOK_CHAR);
  ASSERT_EQ_STR(t.items[0].text, "97");
  ASSERT_EQ_STR(t.items[1].text, "10");
  ASSERT_EQ_STR(t.items[2].text, "65");
  z_free_tokens(&t);
}

static void test_two_char_symbols(void) {
  BEGIN_CASE("multi-char symbols are recognized as a single token");
  TokenVec t = lex("-> == != <= && ||", NULL);
  /* 6 symbol tokens + EOF */
  ASSERT_EQ_INT(t.len, 7);
  ASSERT_EQ_STR(t.items[0].text, "->");
  ASSERT_EQ_INT(t.items[0].length, 2);
  ASSERT_EQ_STR(t.items[1].text, "==");
  ASSERT_EQ_STR(t.items[2].text, "!=");
  ASSERT_EQ_STR(t.items[3].text, "<=");
  ASSERT_EQ_STR(t.items[4].text, "&&");
  ASSERT_EQ_STR(t.items[5].text, "||");
  for (int i = 0; i < 6; i++) ASSERT_EQ_INT(t.items[i].kind, TOK_SYMBOL);
  z_free_tokens(&t);
}

static void test_single_char_symbols(void) {
  BEGIN_CASE("single-char symbols around an ident");
  TokenVec t = lex("(world: World)", NULL);
  /* ( IDENT : IDENT ) EOF */
  ASSERT_EQ_INT(t.len, 6);
  ASSERT_EQ_INT(t.items[0].kind, TOK_SYMBOL);
  ASSERT_EQ_STR(t.items[0].text, "(");
  ASSERT_EQ_INT(t.items[1].kind, TOK_IDENT);
  ASSERT_EQ_STR(t.items[1].text, "world");
  ASSERT_EQ_INT(t.items[2].kind, TOK_SYMBOL);
  ASSERT_EQ_STR(t.items[2].text, ":");
  ASSERT_EQ_INT(t.items[3].kind, TOK_IDENT);
  ASSERT_EQ_STR(t.items[3].text, "World");
  ASSERT_EQ_INT(t.items[4].kind, TOK_SYMBOL);
  ASSERT_EQ_STR(t.items[4].text, ")");
  ASSERT_EQ_INT(t.items[5].kind, TOK_EOF);
  z_free_tokens(&t);
}

static void test_line_column_after_newline(void) {
  BEGIN_CASE("line/column tracking across newlines");
  TokenVec t = lex("a\nb", NULL);
  ASSERT_EQ_INT(t.len, 3);
  ASSERT_EQ_INT(t.items[0].kind, TOK_IDENT);
  ASSERT_EQ_INT(t.items[0].line, 1);
  ASSERT_EQ_INT(t.items[0].column, 1);
  ASSERT_EQ_INT(t.items[1].kind, TOK_IDENT);
  ASSERT_EQ_INT(t.items[1].line, 2);
  ASSERT_EQ_INT(t.items[1].column, 1);
  ASSERT_EQ_INT(t.items[2].kind, TOK_EOF);
  z_free_tokens(&t);
}

static void test_unterminated_string_error(void) {
  BEGIN_CASE("unterminated string sets diag.code=100");
  ZDiag diag = {0};
  TokenVec t = lex("\"oops", &diag);
  ASSERT_EQ_INT(diag.code, 100);
  ASSERT_EQ_STR(diag.message, "unterminated string literal");
  ASSERT_EQ_STR(diag.expected, "");
  ASSERT_EQ_STR(diag.help, "");
  ASSERT_NULL(diag.path);
  ASSERT_EQ_INT(diag.line, 1);
  ASSERT_EQ_INT(diag.column, 1);
  ASSERT_EQ_INT(diag.length, 0);
  z_free_tokens(&t);
}

static void test_unexpected_character_error(void) {
  BEGIN_CASE("unsupported character sets diag.code=101");
  ZDiag diag = {0};
  TokenVec t = lex("@", &diag);
  ASSERT_EQ_INT(diag.code, 101);
  ASSERT_EQ_STR(diag.message, "unexpected character '@'");
  ASSERT_EQ_STR(diag.expected, "");
  ASSERT_EQ_STR(diag.help, "");
  ASSERT_NULL(diag.path);
  ASSERT_EQ_INT(diag.line, 1);
  ASSERT_EQ_INT(diag.column, 1);
  ASSERT_EQ_INT(diag.length, 0);
  z_free_tokens(&t);
}

static void test_real_hello_signature(void) {
  BEGIN_CASE("real example: pub fun main(world: World) -> Void raises {");
  TokenVec t = lex("pub fun main(world: World) -> Void raises {", NULL);

  /* Walk the expected token stream */
  TokenKind expected_kinds[] = {
    TOK_KEYWORD,  /* pub */
    TOK_KEYWORD,  /* fun */
    TOK_IDENT,    /* main */
    TOK_SYMBOL,   /* ( */
    TOK_IDENT,    /* world */
    TOK_SYMBOL,   /* : */
    TOK_IDENT,    /* World */
    TOK_SYMBOL,   /* ) */
    TOK_SYMBOL,   /* -> */
    TOK_IDENT,    /* Void */
    TOK_KEYWORD,  /* raises */
    TOK_SYMBOL,   /* { */
    TOK_EOF
  };
  const char *expected_text[] = {
    "pub", "fun", "main", "(", "world", ":", "World", ")", "->", "Void", "raises", "{", ""
  };
  size_t expected_count = sizeof(expected_kinds) / sizeof(expected_kinds[0]);
  ASSERT_EQ_INT(t.len, expected_count);
  for (size_t i = 0; i < expected_count && i < t.len; i++) {
    ASSERT_EQ_INT(t.items[i].kind, expected_kinds[i]);
    ASSERT_EQ_STR(t.items[i].text, expected_text[i]);
  }
  z_free_tokens(&t);
}

/* ================== Driver ================== */

int main(void) {
  test_empty_input();
  test_whitespace_and_comment_skipped();
  test_identifier_vs_keyword();
  test_numbers();
  test_string_with_escape();
  test_char_literal();
  test_two_char_symbols();
  test_single_char_symbols();
  test_line_column_after_newline();
  test_unterminated_string_error();
  test_unexpected_character_error();
  test_real_hello_signature();

  return TEST_SUMMARY();
}
