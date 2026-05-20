#!/usr/bin/env -S node --experimental-strip-types --disable-warning=ExperimentalWarning
import { execFileSync } from "node:child_process";
import { rmSync, writeFileSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const scriptDir = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(scriptDir, "..");
const tmpBase = path.join("/tmp", `zero-type-core-smoke-${process.pid}`);
const sourcePath = `${tmpBase}.c`;
const exePath = process.platform === "win32" ? `${tmpBase}.exe` : tmpBase;

const source = String.raw`
#include "type_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void expect(int ok, const char *message) {
  if (ok) return;
  fprintf(stderr, "%s\n", message);
  exit(1);
}

static ZTypeId parse_or_die(ZTypeArena *arena, const char *text) {
  ZTypeId type = Z_TYPE_ID_INVALID;
  ZTypeParseError error = {0};
  if (!z_type_parse(arena, text, &type, &error)) {
    fprintf(stderr, "failed to parse '%s': %s at %zu\n", text, error.message, error.offset);
    exit(1);
  }
  return type;
}

static void expect_roundtrip(const char *source, const char *expected) {
  ZTypeArena arena;
  z_type_arena_init(&arena);
  ZTypeId type = parse_or_die(&arena, source);
  char *formatted = z_type_format(&arena, type);
  expect(formatted && strcmp(formatted, expected) == 0, "type format mismatch");
  free(formatted);
  z_type_arena_free(&arena);
}

static void expect_static_roundtrip(const char *source, const char *expected, ZStaticValueKind kind) {
  ZStaticValue value = {0};
  ZTypeParseError error = {0};
  if (!z_static_value_parse(source, &value, &error)) {
    fprintf(stderr, "failed to parse static value '%s': %s at %zu\n", source, error.message, error.offset);
    exit(1);
  }
  expect(value.kind == kind, "static value kind mismatch");
  char *formatted = z_static_value_format(&value);
  expect(formatted && strcmp(formatted, expected) == 0, "static value format mismatch");
  free(formatted);
  z_static_value_free(&value);
}

static void expect_invalid_type(const char *source) {
  ZTypeArena arena;
  z_type_arena_init(&arena);
  ZTypeId type = Z_TYPE_ID_INVALID;
  ZTypeParseError error = {0};
  int ok = z_type_parse(&arena, source, &type, &error);
  expect(!ok && type == Z_TYPE_ID_INVALID && error.message[0] != 0, "invalid type parsed successfully");
  expect(arena.len == 0, "failed type parse mutated arena");
  z_type_arena_free(&arena);
}

static void expect_invalid_type_offset(const char *source, size_t offset) {
  ZTypeArena arena;
  z_type_arena_init(&arena);
  ZTypeId type = Z_TYPE_ID_INVALID;
  ZTypeParseError error = {0};
  int ok = z_type_parse(&arena, source, &type, &error);
  expect(!ok && type == Z_TYPE_ID_INVALID && error.message[0] != 0, "invalid type parsed successfully");
  expect(error.offset == offset, "invalid type reported wrong offset");
  expect(arena.len == 0, "failed type parse mutated arena");
  z_type_arena_free(&arena);
}

static void expect_invalid_static(const char *source) {
  ZStaticValue value = {0};
  ZTypeParseError error = {0};
  int ok = z_static_value_parse(source, &value, &error);
  expect(!ok && error.message[0] != 0, "invalid static value parsed successfully");
  z_static_value_free(&value);
}

static void expect_equal_and_hash(const char *left_text, const char *right_text) {
  ZTypeArena arena;
  z_type_arena_init(&arena);
  ZTypeId left = parse_or_die(&arena, left_text);
  ZTypeId right = parse_or_die(&arena, right_text);
  expect(z_type_equal(&arena, left, right), "equal types did not compare equal");
  expect(z_type_hash(&arena, left) == z_type_hash(&arena, right), "equal types produced different hashes");
  z_type_arena_free(&arena);
}

static void expect_not_equal(const char *left_text, const char *right_text) {
  ZTypeArena arena;
  z_type_arena_init(&arena);
  ZTypeId left = parse_or_die(&arena, left_text);
  ZTypeId right = parse_or_die(&arena, right_text);
  expect(!z_type_equal(&arena, left, right), "different types compared equal");
  z_type_arena_free(&arena);
}

static void expect_failed_parse_rewinds(void) {
  ZTypeArena arena;
  z_type_arena_init(&arena);
  ZTypeId base = parse_or_die(&arena, "i32");
  size_t before = arena.len;
  ZTypeId bad = Z_TYPE_ID_INVALID;
  ZTypeParseError error = {0};
  expect(!z_type_parse(&arena, "Span<", &bad, &error), "bad type parsed successfully");
  expect(arena.len == before, "failed parse did not rewind arena");
  char *formatted = z_type_format(&arena, base);
  expect(formatted && strcmp(formatted, "i32") == 0, "rewind corrupted prior type");
  free(formatted);
  z_type_arena_free(&arena);
}

static void expect_speculative_arg_rewinds(void) {
  ZTypeArena arena;
  z_type_arena_init(&arena);
  ZTypeId gate = parse_or_die(&arena, "Gate<Mode.tiny>");
  expect(arena.len == 1, "static enum arg left speculative type nodes");
  char *formatted = z_type_format(&arena, gate);
  expect(formatted && strcmp(formatted, "Gate<Mode.tiny>") == 0, "static enum arg formatted incorrectly");
  free(formatted);
  z_type_arena_free(&arena);
}

int main(void) {
  expect_roundtrip("i32", "i32");
  expect_roundtrip("const i32", "const i32");
  expect_roundtrip("[4]u8", "[4]u8");
  expect_roundtrip("[0x10]u8", "[16]u8");
  expect_roundtrip("[cap]u8", "[cap]u8");
  expect_roundtrip("Span<u8>", "Span<u8>");
  expect_roundtrip("MutSpan<Span<u8>>", "MutSpan<Span<u8>>");
  expect_roundtrip("Maybe<owned<ByteBuf>>", "Maybe<owned<ByteBuf>>");
  expect_roundtrip("Box<trueThing<u8>>", "Box<trueThing<u8>>");
  expect_roundtrip("ref<FixedVec<u8, 4>>", "ref<FixedVec<u8,4>>");
  expect_roundtrip("mutref<MutSpan<u8>>", "mutref<MutSpan<u8>>");
  expect_roundtrip("FixedVec<T,N>", "FixedVec<T,N>");
  expect_roundtrip("FixedVec<u8, 4_usize>", "FixedVec<u8,4>");
  expect_roundtrip("Gate<true, Mode.tiny>", "Gate<true,Mode.tiny>");
  expect_roundtrip("const [4]Maybe<ref<Point>>", "const [4]Maybe<ref<Point>>");

  expect_static_roundtrip("42", "42", Z_STATIC_VALUE_NUMBER);
  expect_static_roundtrip("4_096", "4096", Z_STATIC_VALUE_NUMBER);
  expect_static_roundtrip("0b1010", "10", Z_STATIC_VALUE_NUMBER);
  expect_static_roundtrip("true", "true", Z_STATIC_VALUE_BOOL);
  expect_static_roundtrip("Mode.tiny", "Mode.tiny", Z_STATIC_VALUE_SYMBOL);

  expect_equal_and_hash("FixedVec<u8,4>", "FixedVec<u8,0x4>");
  expect_equal_and_hash("Maybe<Span<u8>>", "Maybe<Span<u8>>");
  expect_not_equal("FixedVec<u8,4>", "FixedVec<u8,5>");
  expect_not_equal("Span<u8>", "MutSpan<u8>");

  expect_failed_parse_rewinds();
  expect_speculative_arg_rewinds();

  expect_invalid_type("");
  expect_invalid_type("Span<");
  expect_invalid_type("[4");
  expect_invalid_type("Maybe<>");
  expect_invalid_type("Span<u8,,i32>");
  expect_invalid_type("[]u8");
  expect_invalid_type("const");
  expect_invalid_type("FixedVec<u8,>");
  expect_invalid_type("[4_]u8");
  expect_invalid_type("[4__5]u8");
  expect_invalid_type("[0x_1]u8");
  expect_invalid_type("FixedVec<u8,4_nope>");
  expect_invalid_type_offset("FixedVec<u8,4_>", 12);
  expect_invalid_type_offset("[4_]u8", 1);

  expect_invalid_static("");
  expect_invalid_static("Mode.");
  expect_invalid_static("0x");
  expect_invalid_static("4_");
  expect_invalid_static("4__5");
  expect_invalid_static("0x_1");
  expect_invalid_static("4_nope");

  printf("type core smoke ok\n");
  return 0;
}
`;

try {
  writeFileSync(sourcePath, source);
  execFileSync(
    "cc",
    [
      "-std=c11",
      "-Wall",
      "-Wextra",
      "-Wpedantic",
      "-I",
      path.join(repoRoot, "native/zero-c/src"),
      sourcePath,
      path.join(repoRoot, "native/zero-c/src/type_core.c"),
      "-o",
      exePath,
    ],
    { stdio: "inherit" },
  );
  execFileSync(exePath, [], { stdio: "inherit" });
} finally {
  rmSync(sourcePath, { force: true });
  rmSync(exePath, { force: true });
}
