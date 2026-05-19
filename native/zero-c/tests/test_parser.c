/*
 * Unit tests for src/parser.c (z_parse).
 *
 * Build & run from native/zero-c/:
 *   make test-parser
 * or manually:
 *   cc -std=c11 -Wall -Wextra -Iinclude -Itests \
 *      tests/test_parser.c src/lexer.c src/parser.c src/fs.c src/target.c \
 *      -o .zero/bin/test_parser && .zero/bin/test_parser
 *
 * Exit 0 = all passed. Exit 1 = at least one failure.
 *
 * NOTE: Zero's parser requires every function to declare a return type
 * via `-> <Type>` (parser.c:733). The smallest valid function is therefore
 * `pub fun main() -> Void {}` — not `fun main() {}`.
 */

#include "zero.h"
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- Helper: tokenize + parse, returning Program. -----------
 * Caller owns the returned Program and the out-parameter tokens; both
 * must be freed via z_free_program / z_free_tokens.
 */
static Program parse(const char *src, TokenVec *tokens_out, ZDiag *diag) {
  ZDiag local = {0};
  if (!diag) diag = &local;
  *tokens_out = z_tokenize(src, diag);
  return z_parse(tokens_out, diag);
}

/* ================== Test cases ================== */

static void test_parse_empty_program(void) {
  BEGIN_CASE("empty source -> empty Program");
  TokenVec tokens; ZDiag diag = {0};
  Program p = parse("", &tokens, &diag);

  ASSERT_EQ_INT(diag.code, 0);

  ASSERT_EQ_INT(p.functions.len, 0);
  ASSERT_EQ_INT(p.shapes.len, 0);
  ASSERT_EQ_INT(p.enums.len, 0);
  ASSERT_EQ_INT(p.choices.len, 0);
  ASSERT_EQ_INT(p.interfaces.len, 0);
  ASSERT_EQ_INT(p.aliases.len, 0);
  ASSERT_EQ_INT(p.consts.len, 0);
  ASSERT_EQ_INT(p.c_imports.len, 0);
  z_free_program(&p);
  z_free_tokens(&tokens);
}

static void test_parse_minimal_main(void) {
  BEGIN_CASE("pub fun main() -> Void {} produces one public Function");
  TokenVec tokens; ZDiag diag = {0};
  Program p = parse("pub fun main() -> Void {}", &tokens, &diag);
  ASSERT_EQ_INT(diag.code, 0);
  ASSERT_EQ_INT(p.functions.len, 1);

  Function *fn = &p.functions.items[0];
  ASSERT_EQ_STR(fn->name, "main");
  ASSERT_EQ_STR(fn->return_type, "Void");
  ASSERT_TRUE(fn->is_public);
  ASSERT_FALSE(fn->raises);
  ASSERT_FALSE(fn->is_test);
  ASSERT_FALSE(fn->export_c);
  ASSERT_EQ_INT(fn->params.len, 0);
  ASSERT_EQ_INT(fn->body.len, 0);
  z_free_program(&p);
  z_free_tokens(&tokens);
}

static void test_parse_function_with_params(void) {
  BEGIN_CASE("typed parameters are captured with name + type in order");
  TokenVec tokens; ZDiag diag = {0};
  Program p = parse("fun add(a: i32, b: i32) -> i32 {}", &tokens, &diag);
  ASSERT_EQ_INT(diag.code, 0);
  ASSERT_EQ_INT(p.functions.len, 1);

  Function *fn = &p.functions.items[0];
  ASSERT_EQ_STR(fn->name, "add");
  ASSERT_EQ_STR(fn->return_type, "i32");
  ASSERT_FALSE(fn->is_public); /* no `pub` prefix */
  ASSERT_EQ_INT(fn->params.len, 2);
  ASSERT_EQ_STR(fn->params.items[0].name, "a");
  ASSERT_EQ_STR(fn->params.items[0].type, "i32");
  ASSERT_EQ_STR(fn->params.items[1].name, "b");
  ASSERT_EQ_STR(fn->params.items[1].type, "i32");
  z_free_program(&p);
  z_free_tokens(&tokens);
}

static void test_parse_raises_flag(void) {
  BEGIN_CASE("`raises` after return type sets the fallibility flag");
  TokenVec tokens; ZDiag diag = {0};
  Program p = parse("pub fun main(world: World) -> Void raises {}", &tokens, &diag);
  ASSERT_EQ_INT(diag.code, 0);
  ASSERT_EQ_INT(p.functions.len, 1);

  Function *fn = &p.functions.items[0];
  ASSERT_TRUE(fn->raises);
  ASSERT_FALSE(fn->has_error_set); /* unspecified errors */
  ASSERT_EQ_INT(fn->params.len, 1);
  ASSERT_EQ_STR(fn->params.items[0].name, "world");
  ASSERT_EQ_STR(fn->params.items[0].type, "World");
  z_free_program(&p);
  z_free_tokens(&tokens);
}

static void test_parse_let_statement(void) {
  BEGIN_CASE("`let` produces STMT_LET with name, type, and init expression");
  TokenVec tokens; ZDiag diag = {0};
  Program p = parse(
    "fun answer() -> i32 {\n"
    "    let x: i32 = 42\n"
    "    return x\n"
    "}", &tokens, &diag);
  ASSERT_EQ_INT(diag.code, 0);
  ASSERT_EQ_INT(p.functions.len, 1);

  Function *fn = &p.functions.items[0];
  ASSERT_EQ_INT(fn->body.len, 2);

  Stmt *let_stmt = fn->body.items[0];
  ASSERT_NOT_NULL(let_stmt);
  ASSERT_EQ_INT(let_stmt->kind, STMT_LET);
  ASSERT_EQ_STR(let_stmt->name, "x");
  ASSERT_EQ_STR(let_stmt->type, "i32");
  ASSERT_FALSE(let_stmt->mutable_binding);
  ASSERT_NOT_NULL(let_stmt->expr);
  ASSERT_EQ_INT(let_stmt->expr->kind, EXPR_NUMBER);
  ASSERT_EQ_STR(let_stmt->expr->text, "42");

  Stmt *ret_stmt = fn->body.items[1];
  ASSERT_EQ_INT(ret_stmt->kind, STMT_RETURN);
  ASSERT_NOT_NULL(ret_stmt->expr);
  ASSERT_EQ_INT(ret_stmt->expr->kind, EXPR_IDENT);
  ASSERT_EQ_STR(ret_stmt->expr->text, "x");
  z_free_program(&p);
  z_free_tokens(&tokens);
}

static void test_parse_hello_world(void) {
  BEGIN_CASE("examples/hello.0: check world.out.write(\"...\") shape");
  TokenVec tokens; ZDiag diag = {0};
  Program p = parse(
    "pub fun main(world: World) -> Void raises {\n"
    "    check world.out.write(\"hello from zero\\n\")\n"
    "}", &tokens, &diag);
  ASSERT_EQ_INT(diag.code, 0);
  ASSERT_EQ_INT(p.functions.len, 1);

  Function *fn = &p.functions.items[0];
  ASSERT_TRUE(fn->is_public);
  ASSERT_TRUE(fn->raises);
  ASSERT_EQ_INT(fn->body.len, 1);

  /* The single body statement wraps a check-expression. `check` lowers
   * to STMT_CHECK when used as a statement (parser.c handles this in
   * parse_statement). Verify the outer statement kind here; the exact
   * Expr tree shape underneath is left to a future, finer-grained test. */
  Stmt *s = fn->body.items[0];
  ASSERT_NOT_NULL(s);
  ASSERT_EQ_INT(s->kind, STMT_CHECK);
  ASSERT_NOT_NULL(s->expr);
  z_free_program(&p);
  z_free_tokens(&tokens);
}

static void test_parse_error_missing_arrow(void) {
  BEGIN_CASE("missing return arrow sets diag.code (PAR100 family)");
  TokenVec tokens; ZDiag diag = {0};
  /* Parser requires `-> <Type>` after the parameter list. Omitting it
   * should trip the `expect(parser, "->", ...)` call in parse_function. */
  Program p = parse("fun broken() {}", &tokens, &diag);
  ASSERT_EQ_INT(diag.code, 100);
  ASSERT_EQ_STR(diag.message, "expected type name");
  ASSERT_EQ_STR(diag.expected, "");
  ASSERT_EQ_STR(diag.help, "");
  ASSERT_NULL(diag.path);
  ASSERT_EQ_INT(diag.line, 1);
  ASSERT_EQ_INT(diag.column, 14);
  ASSERT_EQ_INT(diag.length, 0);
  z_free_program(&p);
  z_free_tokens(&tokens);
}

/* ================== Driver ================== */

int main(void) {
  test_parse_empty_program();
  test_parse_minimal_main();
  test_parse_function_with_params();
  test_parse_raises_flag();
  test_parse_let_statement();
  test_parse_hello_world();
  test_parse_error_missing_arrow();

  return TEST_SUMMARY();
}
