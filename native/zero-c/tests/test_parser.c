/*
 * Unit tests for src/parser.c (z_parse).
 *
 * Build & run from native/zero-c/:
 *   make test-parser
 *
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
   * parse_statement). Verify the outer statement kind here. */
  Stmt *s = fn->body.items[0];
  ASSERT_NOT_NULL(s);
  ASSERT_EQ_INT(s->kind, STMT_CHECK);
  ASSERT_EQ_INT(s->expr->kind, EXPR_CALL);
  ASSERT_NULL(s->expr->text);
  
  ASSERT_EQ_INT(s->expr->left->kind, EXPR_MEMBER);
  ASSERT_EQ_STR(s->expr->left->text, "write");
  ASSERT_EQ_INT(s->expr->left->left->kind, EXPR_MEMBER);
  ASSERT_EQ_STR(s->expr->left->left->text, "out");
  ASSERT_EQ_INT(s->expr->left->left->left->kind, EXPR_IDENT);
  ASSERT_EQ_STR(s->expr->left->left->left->text, "world");

  ASSERT_EQ_INT(s->expr->args.len, 1);
  ASSERT_EQ_INT(s->expr->args.items[0]->kind, EXPR_STRING);
  ASSERT_EQ_STR(s->expr->args.items[0]->text, "hello from zero\n");

  z_free_program(&p);
  z_free_tokens(&tokens);
}

/*
 * Helper: parse a single expression by wrapping it in a return statement, then return the AST root of that expression.
 * The Program / TokenVec ownership is handed back via out-params so the caller can free them after asserting on the expression tree.
 */
static Expr *parse_return_expr(const char *expr_src, TokenVec *tokens_out,
                               Program *program_out, ZDiag *diag) {
  char src[512];
  snprintf(src, sizeof(src), "fun expr() -> i32 { return %s }", expr_src);
  *tokens_out = z_tokenize(src, diag);
  *program_out = z_parse(tokens_out, diag);
  if (program_out->functions.len == 0) return NULL;
  Function *fn = &program_out->functions.items[0];
  if (fn->body.len == 0) return NULL;
  Stmt *ret = fn->body.items[0];
  return ret ? ret->expr : NULL;
}

/* ---------- Precedence-group tests (parse_binary + precedence table) ---------- */

static void test_parse_binary_mul_tighter_than_add(void) {
  BEGIN_CASE("1 + 2 * 3 parses as 1 + (2 * 3), not (1 + 2) * 3");
  TokenVec t; Program p; ZDiag diag = {0};
  Expr *root = parse_return_expr("1 + 2 * 3", &t, &p, &diag);
  ASSERT_EQ_INT(diag.code, 0);
  ASSERT_NOT_NULL(root);

  /* root = '+', left=1, right='*' */
  ASSERT_EQ_INT(root->kind, EXPR_BINARY);
  ASSERT_EQ_STR(root->text, "+");
  ASSERT_NOT_NULL(root->left);
  ASSERT_EQ_INT(root->left->kind, EXPR_NUMBER);
  ASSERT_EQ_STR(root->left->text, "1");
  ASSERT_NOT_NULL(root->right);
  ASSERT_EQ_INT(root->right->kind, EXPR_BINARY);
  ASSERT_EQ_STR(root->right->text, "*");
  /* deeper: '*' has left=2, right=3 */
  ASSERT_EQ_STR(root->right->left->text, "2");
  ASSERT_EQ_STR(root->right->right->text, "3");

  z_free_program(&p);
  z_free_tokens(&t);
}

static void test_parse_binary_left_associative(void) {
  BEGIN_CASE("1 - 2 - 3 parses as (1 - 2) - 3 (left-associative)");
  TokenVec t; Program p; ZDiag diag = {0};
  Expr *root = parse_return_expr("1 - 2 - 3", &t, &p, &diag);
  ASSERT_EQ_INT(diag.code, 0);
  ASSERT_NOT_NULL(root);

  /* root = '-', left='-'(1,2), right=3 */
  ASSERT_EQ_INT(root->kind, EXPR_BINARY);
  ASSERT_EQ_STR(root->text, "-");
  ASSERT_NOT_NULL(root->left);
  ASSERT_EQ_INT(root->left->kind, EXPR_BINARY);
  ASSERT_EQ_STR(root->left->text, "-");
  ASSERT_EQ_STR(root->left->left->text, "1");
  ASSERT_EQ_STR(root->left->right->text, "2");
  ASSERT_NOT_NULL(root->right);
  ASSERT_EQ_INT(root->right->kind, EXPR_NUMBER);
  ASSERT_EQ_STR(root->right->text, "3");

  z_free_program(&p);
  z_free_tokens(&t);
}

static void test_parse_binary_arith_tighter_than_comparison(void) {
  BEGIN_CASE("1 + 2 == 3 parses as (1 + 2) == 3");
  TokenVec t; Program p; ZDiag diag = {0};
  Expr *root = parse_return_expr("1 + 2 == 3", &t, &p, &diag);
  ASSERT_EQ_INT(diag.code, 0);
  ASSERT_NOT_NULL(root);

  /* root = '==', left='+'(1,2), right=3 */
  ASSERT_EQ_INT(root->kind, EXPR_BINARY);
  ASSERT_EQ_STR(root->text, "==");
  ASSERT_NOT_NULL(root->left);
  ASSERT_EQ_INT(root->left->kind, EXPR_BINARY);
  ASSERT_EQ_STR(root->left->text, "+");
  ASSERT_EQ_STR(root->left->left->text, "1");
  ASSERT_EQ_STR(root->left->right->text, "2");
  ASSERT_NOT_NULL(root->right);
  ASSERT_EQ_STR(root->right->text, "3");

  z_free_program(&p);
  z_free_tokens(&t);
}

static void test_parse_binary_and_tighter_than_or(void) {
  BEGIN_CASE("a || b && c parses as a || (b && c)");
  TokenVec t; Program p; ZDiag diag = {0};
  Expr *root = parse_return_expr("a || b && c", &t, &p, &diag);
  ASSERT_EQ_INT(diag.code, 0);
  ASSERT_NOT_NULL(root);

  /* root = '||', left=a, right='&&'(b,c) */
  ASSERT_EQ_INT(root->kind, EXPR_BINARY);
  ASSERT_EQ_STR(root->text, "||");
  ASSERT_NOT_NULL(root->left);
  ASSERT_EQ_INT(root->left->kind, EXPR_IDENT);
  ASSERT_EQ_STR(root->left->text, "a");
  ASSERT_NOT_NULL(root->right);
  ASSERT_EQ_INT(root->right->kind, EXPR_BINARY);
  ASSERT_EQ_STR(root->right->text, "&&");
  ASSERT_EQ_STR(root->right->left->text, "b");
  ASSERT_EQ_STR(root->right->right->text, "c");

  z_free_program(&p);
  z_free_tokens(&t);
}

static void test_parse_error_missing_arrow(void) {
  BEGIN_CASE("missing return arrow sets diag.code (PAR100 family)");
  TokenVec tokens; ZDiag diag = {0};
  /*
   * Parser requires `-> <Type>` after the parameter list. Omitting it
   * should trip the `expect(parser, "->", ...)` call in parse_function.
   *
   * KNOWN ISSUE (not fixed here): parser.c:fail() unconditionally
   * overwrites diag.message, so the message we read below is from the
   * cascaded parse_type() call ("expected type name"), not the real
   * root cause ("expected return type after parameters"). The
   * column=14 also points at '{' instead of the missing arrow site.
   * A one-line sticky-diag fix in fail() would surface the root
   * cause; tracking only — do not change behavior here.
   */
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
  /* ---- Top-level declarations: Program / Function fields ---- */
  test_parse_empty_program();          // empty source -> empty Program
  test_parse_minimal_main();           // smallest function: pub fun main() -> Void {}
  test_parse_function_with_params();   // param list keeps name + type, in order
  test_parse_raises_flag();            // raises flag (no explicit error set)

  /* ---- Body statements + real-world example ---- */
  test_parse_let_statement();          // STMT_LET / STMT_RETURN with nested expr
  test_parse_hello_world();            // examples/hello.0 end-to-end AST layout

  /* ---- Binary operator precedence + associativity (parse_binary table) ---- */
  test_parse_binary_mul_tighter_than_add();          // *  tighter than +
  test_parse_binary_left_associative();              // same-level left-assoc: 1 - 2 - 3
  test_parse_binary_arith_tighter_than_comparison(); // +  tighter than ==
  test_parse_binary_and_tighter_than_or();           // && tighter than ||

  /* ---- Error paths ---- */
  test_parse_error_missing_arrow();    // missing `->` triggers PAR100 (cascade issue noted inside)

  return TEST_SUMMARY();
}
