#ifndef ZERO_C_TYPE_CORE_H
#define ZERO_C_TYPE_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint32_t ZTypeId;

#define Z_TYPE_ID_INVALID ((ZTypeId)0)

typedef enum {
  Z_STATIC_VALUE_INVALID,
  Z_STATIC_VALUE_NUMBER,
  Z_STATIC_VALUE_BOOL,
  Z_STATIC_VALUE_SYMBOL
} ZStaticValueKind;

typedef struct {
  ZStaticValueKind kind;
  char *text;
  unsigned long long number;
  bool boolean;
} ZStaticValue;

typedef enum {
  Z_TYPE_NODE_INVALID,
  Z_TYPE_NODE_NAME,
  Z_TYPE_NODE_CONST,
  Z_TYPE_NODE_ARRAY,
  Z_TYPE_NODE_APPLY
} ZTypeNodeKind;

typedef enum {
  Z_TYPE_ARG_TYPE,
  Z_TYPE_ARG_STATIC
} ZTypeArgKind;

typedef struct {
  ZTypeArgKind kind;
  union {
    ZTypeId type;
    ZStaticValue static_value;
  } as;
} ZTypeArg;

typedef struct ZTypeNode ZTypeNode;

typedef struct {
  ZTypeNode *nodes;
  size_t len;
  size_t cap;
} ZTypeArena;

typedef struct {
  size_t offset;
  char message[128];
} ZTypeParseError;

void z_type_arena_init(ZTypeArena *arena);
void z_type_arena_free(ZTypeArena *arena);

bool z_static_value_parse(const char *text, ZStaticValue *out, ZTypeParseError *error);
char *z_static_value_format(const ZStaticValue *value);
void z_static_value_free(ZStaticValue *value);

bool z_type_parse(ZTypeArena *arena, const char *text, ZTypeId *out, ZTypeParseError *error);
char *z_type_format(const ZTypeArena *arena, ZTypeId type);
bool z_type_equal(const ZTypeArena *arena, ZTypeId left, ZTypeId right);
uint64_t z_type_hash(const ZTypeArena *arena, ZTypeId type);

#endif
