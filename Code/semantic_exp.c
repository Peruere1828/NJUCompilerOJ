#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "AST.h"
#include "common.h"
#include "config.h"
#include "semantic_error.h"
#include "symbol_table.h"
#include "type.h"

// 判断一个Exp节点是否是左值
static int is_lvalue(ASTNode* exp_node) {
  if (exp_node == NULL || exp_node->kind != NODE_EXP) return 0;
  // 只有三种可能是左值：ID、Exp LB Exp RB、Exp DOT ID
  if (exp_node->child_count == 1 && exp_node->children[0]->kind == TOKEN_ID)
    return 1;
  if (exp_node->child_count == 4 && exp_node->children[1]->kind == TOKEN_LB)
    return 1;
  if (exp_node->child_count == 3 && exp_node->children[1]->kind == TOKEN_DOT)
    return 1;
  return 0;
}

// 定义一个静态缓冲区，足够容纳绝大多数报错表达式
static char exp_name_buf[1024];

static void build_exp_str(ASTNode* node, char* buf, size_t max_len) {
  if (node == NULL || strlen(buf) >= max_len - 1) return;

  if (node->child_count == 1) {
    ASTNode* child = node->children[0];
    if (child->kind == TOKEN_ID) {
      strncat(buf, child->val.str_val, max_len - strlen(buf) - 1);
    } else if (child->kind == TOKEN_INT) {
      char temp[32];
      snprintf(temp, sizeof(temp), "%lu", child->val.int_val);
      strncat(buf, temp, max_len - strlen(buf) - 1);
    } else if (child->kind == TOKEN_FLOAT) {
      strncat(buf, "<float>", max_len - strlen(buf) - 1);
    }
  } else if (node->children[0]->kind == TOKEN_LP) {
    // 处理括号: LP Exp RP
    strncat(buf, "(", max_len - strlen(buf) - 1);
    build_exp_str(node->children[1], buf, max_len);
    strncat(buf, ")", max_len - strlen(buf) - 1);
  } else if (node->children[1]->kind == TOKEN_DOT) {
    // 处理结构体域访问: Exp DOT ID
    build_exp_str(node->children[0], buf, max_len);
    strncat(buf, ".", max_len - strlen(buf) - 1);
    strncat(buf, node->children[2]->val.str_val, max_len - strlen(buf) - 1);
  } else if (node->children[1]->kind == TOKEN_LB) {
    // 处理数组访问: Exp LB Exp RB
    build_exp_str(node->children[0], buf, max_len);
    strncat(buf, "[", max_len - strlen(buf) - 1);
    build_exp_str(node->children[2], buf, max_len);
    strncat(buf, "]", max_len - strlen(buf) - 1);
  } else if (node->children[0]->kind == TOKEN_ID) {
    // 处理函数调用: ID LP ... RP
    strncat(buf, node->children[0]->val.str_val, max_len - strlen(buf) - 1);
    strncat(buf, "(...)", max_len - strlen(buf) - 1);  //忽略函数参数
  } else {
    // 对于其他复杂的算术/逻辑运算，退化为泛型标识
    strncat(buf, "<expr>", max_len - strlen(buf) - 1);
  }
}

// 供外部调用的包装函数
static const char* get_exp_name(ASTNode* exp_node) {
  exp_name_buf[0] = '\0';
  build_exp_str(exp_node, exp_name_buf, sizeof(exp_name_buf));

  if (exp_name_buf[0] == '\0') {
    return "expression";
  }
  return exp_name_buf;
}

FieldList* visit_Args(ASTNode* node);

Type* visit_Exp(ASTNode* node) {
  if (node == NULL) return NULL;
  if (node->child_count == 1) {
    // Exp: ID | INT | FLOAT
    ASTNode* child = node->children[0];
    if (child->kind == TOKEN_ID) {
      Type* tp = lookup_symbol(child->val.str_val);
      if (tp == NULL) {
        print_semantic_error(ERR_UNDEFINED_VARIABLE, child->lineno,
                             child->val.str_val);
        return NULL;
      }
      return tp;
    } else if (child->kind == TOKEN_INT) {
      return &type_int;
    } else {
      return &type_float;
    }
  } else if (node->children[0]->kind == TOKEN_LP) {
    // Exp: LP Exp RP
    return visit_Exp(node->children[1]);
  } else if (node->children[0]->kind == TOKEN_MINUS) {
    // Exp: MINUS Exp
    Type* tp = visit_Exp(node->children[1]);
    if (tp == NULL) return NULL;
    if (tp->kind != TYPE_BASIC) {
      print_semantic_error(ERR_TYPE_MISMATCH_OPERATOR,
                           node->children[1]->lineno, NULL);
      return NULL;
    }
    return tp;
  } else if (node->children[0]->kind == TOKEN_NOT) {
    // Exp: NOT Exp
    Type* tp = visit_Exp(node->children[1]);
    if (tp == NULL) return NULL;
    if (!compare_two_types(tp, &type_int)) {
      print_semantic_error(ERR_TYPE_MISMATCH_OPERATOR,
                           node->children[1]->lineno, NULL);
      return NULL;
    }
    return tp;
  } else if (node->child_count == 3 && node->children[0]->kind == NODE_EXP &&
             node->children[2]->kind == NODE_EXP) {
    // Exp: Exp ASSIGNOP|AND|OR|RELOP|PLUS|MINUS|STAR|DIV Exp
    ASTNode *lson = node->children[0], *rson = node->children[2];
    Type* tp1 = visit_Exp(lson);
    Type* tp2 = visit_Exp(rson);
    if (tp1 == NULL || tp2 == NULL) return NULL;

    NodeKind op_kind = node->children[1]->kind;
    int op_lineno = node->children[1]->lineno;
    if (op_kind == TOKEN_ASSIGNOP) {
      // Exp: Exp ASSIGNOP Exp
      if (!is_lvalue(node->children[0])) {
        print_semantic_error(ERR_LVALUE_REQUIRED, node->children[0]->lineno,
                             get_exp_name(node->children[0]));
        return NULL;
      }
      if (!compare_two_types(tp1, tp2)) {
        print_semantic_error(ERR_TYPE_MISMATCH_ASSIGNMENT, op_lineno, NULL);
        return NULL;
      }
      return tp1;
    }

    if (tp1->kind != TYPE_BASIC) {
      print_semantic_error(ERR_TYPE_MISMATCH_OPERATOR, lson->lineno, NULL);
      return NULL;
    }
    if (tp2->kind != TYPE_BASIC) {
      print_semantic_error(ERR_TYPE_MISMATCH_OPERATOR, rson->lineno, NULL);
      return NULL;
    }

    switch (node->children[1]->kind) {
      case TOKEN_AND:
      case TOKEN_OR:
        if (tp1->u.basic != BASIC_INT || tp2->u.basic != BASIC_INT) {
          print_semantic_error(ERR_TYPE_MISMATCH_OPERATOR, op_lineno, NULL);
          return NULL;
        }
        return &type_int;
        break;
      case TOKEN_RELOP:
        if (!compare_two_types(tp1, tp2)) {
          print_semantic_error(ERR_TYPE_MISMATCH_OPERATOR, op_lineno, NULL);
          return NULL;
        }
        return &type_int;
        break;
      case TOKEN_PLUS:
      case TOKEN_MINUS:
      case TOKEN_STAR:
      case TOKEN_DIV:
        if (!compare_two_types(tp1, tp2)) {
          print_semantic_error(ERR_TYPE_MISMATCH_OPERATOR, op_lineno, NULL);
          return NULL;
        }
        return tp1;
        break;
      default:
        break;
    }
  } else if (node->children[0]->kind == TOKEN_ID) {
    // Exp: ID LP Args RP | ID LP RP
    ASTNode* func = node->children[0];
    Type* tp = lookup_symbol(func->val.str_val);
    if (tp == NULL) {
      print_semantic_error(ERR_UNDEFINED_FUNCTION, func->lineno,
                           func->val.str_val);
      return NULL;
    }
    if (tp->kind != TYPE_FUNCTION) {
      print_semantic_error(ERR_FUNCTION_CALL_ON_NON_FUNCTION, func->lineno,
                           func->val.str_val);
      return NULL;
    }

    FieldList* args = NULL;
    if (node->child_count == 4) {
      // Exp: ID LP Args RP
      args = visit_Args(node->children[2]);
    }
    // 比对函数参数
    FieldList* param = tp->u.function.args;  // 形参链表
    FieldList* arg = args;                   // 实参链表
    int is_match = 1;

    while (param != NULL && arg != NULL) {
      if (!compare_two_types(param->type, arg->type)) {
        is_match = 0;
        break;
      }
      param = param->nxt;
      arg = arg->nxt;
    }
    if (param != NULL || arg != NULL) {
      is_match = 0;
    }
    if (!is_match) {
      print_semantic_error(ERR_ARGUMENT_MISMATCH, func->lineno,
                           func->val.str_val);
    }

    FieldList* temp = args;
    while (temp != NULL) {
      FieldList* to_free = temp;
      temp = temp->nxt;
      free(to_free);
    }

    return tp->u.function.ret_type;
  } /* Exp: Exp LB Exp RB | Exp DOT ID */
  else if (node->children[1]->kind == TOKEN_LB) {
    // Exp: Exp LB Exp RB
    ASTNode* exp_node = node->children[0];
    Type* arr_type = visit_Exp(exp_node);
    if (arr_type == NULL) return NULL;
    if (arr_type->kind != TYPE_ARRAY) {
      print_semantic_error(ERR_ARRAY_ACCESS_ON_NON_ARRAY, exp_node->lineno,
                           get_exp_name(exp_node));
      return NULL;
    }
    Type* ind_type = visit_Exp(node->children[2]);
    if (ind_type == NULL) return NULL;
    if (!compare_two_types(ind_type, &type_int)) {
      print_semantic_error(ERR_NON_INTEGER_ARRAY_INDEX,
                           node->children[2]->lineno, NULL);
      // FEAT: 当下标类型出错，仍然返回arr对应的type
    }
    return arr_type->u.array.element_type;
  } else {
    // Exp: Exp DOT ID
    ASTNode* exp_node = node->children[0];
    Type* struct_type = visit_Exp(exp_node);
    if (struct_type == NULL) return NULL;
    if (struct_type->kind != TYPE_STRUCTURE) {
      print_semantic_error(ERR_DOT_OPERATOR_ON_NON_STRUCT, exp_node->lineno,
                           get_exp_name(exp_node));
      return NULL;
    }
    const char* name = node->children[2]->val.str_val;
    FieldList* cur = struct_type->u.structure.members;
    while (cur != NULL) {
      if (strcmp(cur->name, name) == 0) {
        return cur->type;
      }
      cur = cur->nxt;
    }
    print_semantic_error(ERR_UNDEFINED_STRUCT_FIELD, node->children[2]->lineno,
                         name);
    return NULL;
  }
  return NULL;
}

FieldList* visit_Args(ASTNode* node) {
  if (node == NULL) return NULL;
  // Args: Exp COMMA Args | Exp
  FieldList* arg = (FieldList*)malloc(sizeof(FieldList));
  arg->type = visit_Exp(node->children[0]);
  arg->lineno = node->children[0]->lineno;
  arg->nxt = NULL;
  if (node->child_count == 3) {
    arg->nxt = visit_Args(node->children[2]);
  }
  return arg;
}