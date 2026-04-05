#ifndef TYPE_H
#define TYPE_H

#include <stddef.h>

#include "config.h"

typedef struct Type Type;
typedef struct FieldList FieldList;

// 字段列表结构体，用于表示结构体成员或函数参数列表。
// 语义分析阶段中，FieldList 既可表示结构体字段链表，也可表示函数参数链表。
struct FieldList {
  char* name;      // 字段名称
  Type* type;      // 字段类型
  FieldList* nxt;  // 指向下一个字段的指针
  int lineno;      // 字段名所在行号
};

typedef enum { TYPE_BASIC, TYPE_ARRAY, TYPE_STRUCTURE, TYPE_FUNCTION } TypeKind;

typedef enum { BASIC_INT, BASIC_FLOAT } BasicType;

typedef struct ArrayType {
  Type* element_type;  // 元素类型，只能是基本、数组或结构体
  int size;            // 数组在当前维度的大小
} ArrayType;

typedef struct FunctionType {
  int argc;         // 参数数量
  FieldList* args;  // 参数列表
  Type* ret_type;   // 返回类型
  int is_defined;   // 是否被定义过（针对阶段二要求一）
} FunctionType;

typedef struct StructType {
  char* name;          // 结构体名称，仅用于名等价检查
  FieldList* members;  // 成员列表
} StructType;

struct Type {
  TypeKind kind;  // 类型种类
  union {
    BasicType basic;        // 基本类型
    ArrayType array;        // 数组类型
    FunctionType function;  // 函数类型
    StructType structure;   // 结构体类型
  } u;
};

// 预定义的基本类型变量
extern Type type_int;
extern Type type_float;

// 比较两个类型是否相同，相同返回1，否则返回0
int compare_two_types(Type* t1, Type* t2);

#endif