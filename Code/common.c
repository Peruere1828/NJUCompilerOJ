#include "common.h"

#include <stdlib.h>
#include <string.h>

char* strdup(const char* src_str) {
  int len = strlen(src_str);
  char* dst_str = (char*)malloc(len * sizeof(char));
  strcpy(dst_str, src_str);
  return dst_str;
}