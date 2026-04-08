#include "type.h"

#include <string.h>

Type type_int = {.kind = TYPE_BASIC, .u.basic = BASIC_INT};

Type type_float = {.kind = TYPE_BASIC, .u.basic = BASIC_FLOAT};

// 类型比较函数，用于判断两个类型是否“等价”。
// 基本类型使用值相等，数组类型递归比较元素类型，函数类型比较返回值与形参列表，
// 结构体类型默认使用名等价（阶段三可切换为成员等价）。
int compare_two_types(Type* t1, Type* t2) {
  if (t1 == t2) return 1;
  if (t1 == NULL || t2 == NULL) return 0;
  if (t1->kind != t2->kind) return 0;
  switch (t1->kind) {
    case TYPE_BASIC: {
      return t1->u.basic == t2->u.basic;
    }
    case TYPE_ARRAY: {
      return compare_two_types(t1->u.array.element_type,
                               t2->u.array.element_type);
    }
    case TYPE_FUNCTION: {
      if (t1->u.function.argc != t2->u.function.argc) return 0;
      if (!compare_two_types(t1->u.function.ret_type, t2->u.function.ret_type))
        return 0;
      FieldList *t1_arg = t1->u.function.args, *t2_arg = t2->u.function.args;
      while (t1_arg && t2_arg) {
        if (!compare_two_types(t1_arg->type, t2_arg->type)) return 0;
        t1_arg = t1_arg->nxt;
        t2_arg = t2_arg->nxt;
      }
      return t1_arg == NULL && t2_arg == NULL;
    }
    case TYPE_STRUCTURE: {
#ifndef STAGE_TWO_REQ_THREE
      return strcmp(t1->u.structure.name, t2->u.structure.name) == 0;
#else
      FieldList *t1_arg = t1->u.structure.members,
                *t2_arg = t2->u.structure.members;
      while (t1_arg && t2_arg) {
        if (!compare_two_types(t1_arg->type, t2_arg->type)) return 0;
        t1_arg = t1_arg->nxt;
        t2_arg = t2_arg->nxt;
      }
      return t1_arg == NULL && t2_arg == NULL;
#endif
    }
    default:
      break;
  }
  return 0;
}

int calculate_type_size(Type* tp) {
  if (tp == NULL) return 0;
  switch (tp->kind) {
    case TYPE_BASIC:
      return 4;
      break;
    case TYPE_ARRAY:
      return calculate_type_size(tp->u.array.element_type) * tp->u.array.size;
      break;
    case TYPE_STRUCTURE: {
      int sum = 0;
      FieldList* cur = tp->u.structure.members;
      while (cur != NULL) {
        sum += calculate_type_size(cur->type);
        cur = cur->nxt;
      }
      return sum;
    }
    case TYPE_FUNCTION:
      return 0;
      break;

    default:
      break;
  }
  return 0;
}

int calculate_struct_field_offset(Type* struct_tp, const char* field_name) {
  if (struct_tp == NULL || struct_tp->kind != TYPE_STRUCTURE) return -1;

  int offset = 0;
  FieldList* member = struct_tp->u.structure.members;

  while (member != NULL) {
    if (strcmp(member->name, field_name) == 0) {
      return offset;
    }
    offset += calculate_type_size(member->type);
    member = member->nxt;
  }

  return -1;
}