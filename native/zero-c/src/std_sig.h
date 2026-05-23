#ifndef ZERO_C_STD_SIG_H
#define ZERO_C_STD_SIG_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
  const char *name;
  const char *return_type;
  int arg_count;
  const char *capability;
  const char *target_support;
  const char *allocation_behavior;
  bool emits_runtime_helper;
} ZStdHelperInfo;

extern const ZStdHelperInfo z_std_helpers[];

const ZStdHelperInfo *z_std_helper_find(const char *name);
int z_std_helper_index(const char *name, size_t max_helpers);

#endif
