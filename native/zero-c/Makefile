CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Os
ROOT := ../..
OUT := $(ROOT)/.zero/bin/zero
BUILD_STAMP := $(OUT).build
HOST_ID := $(shell uname -s 2>/dev/null || echo unknown)-$(shell uname -m 2>/dev/null || echo unknown)
BUILD_ID := $(HOST_ID)|$(CC)|$(CFLAGS)
SRC := $(wildcard src/*.c)
TEST_LEXER := $(ROOT)/.zero/bin/test_lexer
TEST_LEXER_DEPS := src/lexer.c src/fs.c src/target.c
TEST_PARSER := $(ROOT)/.zero/bin/test_parser
TEST_PARSER_DEPS := src/lexer.c src/parser.c src/fs.c src/target.c

.PHONY: all clean install-local test test-lexer test-parser FORCE

all: $(OUT)

$(BUILD_STAMP): FORCE
	@mkdir -p $(dir $(OUT))
	@current="$$(cat "$(BUILD_STAMP)" 2>/dev/null || true)"; \
	if [ "$$current" != "$(BUILD_ID)" ]; then \
		printf '%s\n' "$(BUILD_ID)" > $(BUILD_STAMP); \
	fi

$(OUT): $(BUILD_STAMP) $(SRC) include/zero.h
	@mkdir -p $(dir $(OUT))
	# -Iinclude表示在搜索 #include 引用的头文件时，额外把 include/ 这个目录也算进搜索路径
	# 所以当 src/lexer.c 写 #include "zero.h" 时，编译器实际上从 <makefile所在目录>/include/zero.h 找到了它，并不依赖文件相对路径
	$(CC) $(CFLAGS) -Iinclude $(SRC) -o $(OUT)

clean:
	rm -f $(OUT) $(BUILD_STAMP) $(TEST_LEXER) $(TEST_PARSER)

install-local: $(OUT)
	@printf 'installed %s\n' "$(OUT)"

$(TEST_LEXER): tests/test_lexer.c tests/test_framework.h $(TEST_LEXER_DEPS) include/zero.h
	@mkdir -p $(dir $(TEST_LEXER))
	$(CC) $(CFLAGS) -Iinclude -Itests tests/test_lexer.c $(TEST_LEXER_DEPS) -o $(TEST_LEXER)

test-lexer: $(TEST_LEXER)
	$(TEST_LEXER)

$(TEST_PARSER): tests/test_parser.c tests/test_framework.h $(TEST_PARSER_DEPS) include/zero.h
	@mkdir -p $(dir $(TEST_PARSER))
	$(CC) $(CFLAGS) -Iinclude -Itests tests/test_parser.c $(TEST_PARSER_DEPS) -o $(TEST_PARSER)

test-parser: $(TEST_PARSER)
	$(TEST_PARSER)

test: test-lexer test-parser
