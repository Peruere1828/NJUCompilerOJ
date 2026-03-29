#ifndef TYPE_H
#define TYPE_H

#include <stddef.h>

#include "config.h"

typedef struct Type Type;
typedef struct FieldList FieldList;

struct FieldList {
  char* name;
  Type* type;
  FieldList* nxt;
};

typedef enum { BASIC, ARRAY, STRUCTURE, FUNCTION } TypeKind;

typedef enum { INT, FLOAT } BasicType;

typedef struct ArrayType {
  Type* element_type;  // element_type只有可能是basic、array或struct
  int dimension;
} ArrayType;

typedef struct FunctionType {
  int argc;
  FieldList* args;
  Type* ret_type;
} FunctionType;

typedef struct StructType {
  char* name; // 只在名等价使用
  FieldList* members;
} StructType;

struct Type {
  TypeKind kind;
  union {
    BasicType basic;
    ArrayType array;
    FunctionType function;
    StructType structure;
  } u;
};

int compare_two_types(Type* t1, Type* t2);

#endif