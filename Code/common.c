#include "common.h"

#include <stdlib.h>
#include <string.h>

char* strdup(const char* src_str) {
  int len = strlen(src_str) + 1;
  char* dst_str = (char*)malloc(len * sizeof(char));
  memcpy(dst_str, src_str, len);
  return dst_str;
}

unsigned int gen_hash(const char* name) {
  unsigned int val = 0, i;
  for (; *name; ++name) {
    val = (val << 2) + *name;
    if (i = val & ~HASH_TABLE_SIZE) val = (val ^ (i >> 12)) & HASH_TABLE_SIZE;
  }
  return val;
}