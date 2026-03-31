#include "type.h"

#include <string.h>

Type type_int = {.kind = TYPE_BASIC, .u.basic = BASIC_INT};

Type type_float = {.kind = TYPE_BASIC, .u.basic = BASIC_FLOAT};

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