#include "semantic.h"

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

// 当前解析的函数名称，用于语义报错
static const char* current_parsing_function = NULL;

static void insert_read_and_write() {
  char* read_name = strdup("read");
  Type* read_type = (Type*)malloc(sizeof(Type));
  read_type->kind = TYPE_FUNCTION;
  read_type->u.function.argc = 0;
  read_type->u.function.args = NULL;
  read_type->u.function.is_defined = 1;
  read_type->u.function.ret_type = &type_int;

  char* write_name = strdup("write");
  Type* write_type = (Type*)malloc(sizeof(Type));
  write_type->kind = TYPE_FUNCTION;
  write_type->u.function.argc = 1;
  write_type->u.function.args = (FieldList*)malloc(sizeof(FieldList));
  write_type->u.function.args->type = &type_int;
  write_type->u.function.args->nxt = NULL;
  write_type->u.function.args->lineno = -1;
  write_type->u.function.args->name = strdup("1_out_anon");
  write_type->u.function.is_defined = 1;
  write_type->u.function.ret_type = &type_int;

  insert_symbol(read_name, read_type, -1);
  insert_symbol(write_name, write_type, -1);
}

// 语义分析采用递归遍历AST的方式。
// 通过访问各个节点，构造类型信息、函数参数列表、结构体成员列表，
// 同时维护符号表进行作用域检查、重定义检查和类型一致性检查。
// 错误类型由 semantic_error.h 中定义的枚举统一输出。

// 语义分析入口
void semantic_analysis(ASTNode* root) {
  if (root == NULL) return;
  enter_scope();  // 全局作用域
#if defined(STAGE_THREE)
  insert_read_and_write();
#endif
  visit_Program(root);
#ifdef STAGE_TWO_REQ_ONE
  scan_function_declared_but_not_defined();
#endif
  exit_scope();
}

// helper function: 将l2链表追加到l1链表尾部，返回合并后的链表头
// 不会对名称进行去重
static FieldList* merge_lists_nonunique(FieldList* l1, FieldList* l2) {
  if (l1 == NULL) {
    return l2;
  } else {
    FieldList* tail = l1;
    while (tail->nxt != NULL) {
      tail = tail->nxt;
    }
    tail->nxt = l2;
    return l1;
  }
}

// helper function: 合并两个链表并去重
// 将 l2 追加到 l1 尾部，并在整条新链表中查找重复名。
// 保留首次出现的符号，对后续出现的所有同名符号报 err_type
// 错误并将其从链表中剔除。
static FieldList* merge_field_lists(FieldList* l1, FieldList* l2,
                                    SemanticErrorType err_type) {
  l1 = merge_lists_nonunique(l1, l2);

  if (l1 == NULL) return NULL;

  FieldList* p1 = l1;
  while (p1 != NULL) {
    FieldList* prev = p1;
    FieldList* p2 = p1->nxt;

    while (p2 != NULL) {
      if (strcmp(p1->name, p2->name) == 0) {
        // 重名，报错
        print_semantic_error(err_type, p2->lineno, p2->name);
        prev->nxt = p2->nxt;
        free(p2);
        p2 = prev->nxt;
      } else {
        prev = p2;
        p2 = p2->nxt;
      }
    }
    p1 = p1->nxt;
  }

  return l1;
}

// helper function: 获取VarDec最底层的ID的节点
ASTNode* get_id_node_from_vardec(ASTNode* node) {
  if (node == NULL || node->kind != NODE_VARDEC) return NULL;
  while (node->kind == NODE_VARDEC) {
    node = node->children[0];
  }
  return node;
}

// helper function：专门用于在形参入表后，遍历 FunDec 子树回填 ir_val_id
/// TODO: 可以更优雅地实现，让insert_symbol直接返回SymbolNode*，避免多次lookup
static void backpatch_param_ir_id(ASTNode* node) {
  if (node == NULL) return;

  if (node->kind == NODE_VARDEC) {
    ASTNode* id_node = get_id_node_from_vardec(node);
    if (id_node) {
      // 此时恰好在函数的局部作用域内，直接查表就能拿到刚刚分配的 ID
      SymbolNode* sn = lookup_symbol_node(id_node->val.str_val);
      id_node->is_param = sn->is_param = 1;
      id_node->ir_val_id = sn->ir_var_id;
    }
    return;
  }

  for (int i = 0; i < node->child_count; ++i) {
    backpatch_param_ir_id(node->children[i]);
  }
}

void visit_Program(ASTNode* node) {
  if (node == NULL) return;
  // Program: ExtDefList
  visit_ExtDefList(node->children[0]);
}

void visit_ExtDefList(ASTNode* node) {
  if (node == NULL) return;
  // ExtDefList: ExtDef ExtDefList
  visit_ExtDef(node->children[0]);
  visit_ExtDefList(node->children[1]);
}

void visit_ExtDef(ASTNode* node) {
  if (node == NULL) return;
  Type* base_type = visit_Specifier(node->children[0]);
  if (base_type == NULL) return;
  if (node->child_count == 3 && node->children[1]->kind == NODE_EXTDECLIST) {
    // ExtDef: Specifier ExtDecList SEMI
    visit_ExtDecList(node->children[1], base_type);
  } else if (node->child_count == 2) {
    // ExtDef: Specifier SEMI
  } else if (node->child_count == 3 && node->children[1]->kind == NODE_FUNDEC) {
    // ExtDef: Specifier FunDec CompSt | Specifier FunDec SEMI
    // 当没有开启宏STAGE_TWO_REQ_ONE时，函数声明已经在语法上被排除了
    // 因此只存在ExtDef: Specifier FunDec CompSt的情况
    FieldList* fun_dec = visit_FunDec(node->children[1], base_type);
    if (fun_dec == NULL) return;
    Type* symbol_type = lookup_symbol(fun_dec->name);
    int is_definition = (node->children[2]->kind == NODE_COMPST);
#ifndef STAGE_TWO_REQ_ONE
    if (symbol_type != NULL) {
      print_semantic_error(ERR_REDEFINED_FUNCTION, node->children[1]->lineno,
                           fun_dec->name);
    } else {
      fun_dec->type->u.function.is_defined = 1;
      insert_symbol(fun_dec->name, fun_dec->type, fun_dec->lineno);
    }
#else
    if (symbol_type != NULL) {
      if (symbol_type->u.function.is_defined) {
        if (is_definition) {
          print_semantic_error(ERR_REDEFINED_FUNCTION,
                               node->children[1]->lineno, fun_dec->name);
        } else if (!compare_two_types(symbol_type, fun_dec->type)) {
          print_semantic_error(ERR_CONFLICTING_FUNCTION_DECLARATIONS,
                               node->children[1]->lineno, fun_dec->name);
        }
      } else {
        if (!compare_two_types(symbol_type, fun_dec->type)) {
          print_semantic_error(ERR_CONFLICTING_FUNCTION_DECLARATIONS,
                               node->children[1]->lineno, fun_dec->name);
        } else {
          symbol_type->u.function.is_defined = is_definition;
        }
      }
    } else {
      fun_dec->type->u.function.is_defined = is_definition;
      insert_symbol(fun_dec->name, fun_dec->type, fun_dec->lineno);
    }
#endif
    if (is_definition) {
      // ExtDef: Specifier FunDec CompSt
      enter_scope();
      current_parsing_function = fun_dec->name;
      FieldList* arg = fun_dec->type->u.function.args;
      while (arg != NULL) {
        // 处理FunDec的VarList的时候去掉了重复的参数
        // 但是不保证name跟某个type重名，或者不保证跟之前某个全局变量重名，所以仍然要判断
        if (!insert_symbol(arg->name, arg->type, arg->lineno)) {
          print_semantic_error(ERR_REDEFINED_VARIABLE_OR_STRUCT_NAME,
                               arg->lineno, arg->name);
        }
        arg = arg->nxt;
      }
      // 回填ir_val_id
      backpatch_param_ir_id(node->children[1]);
      visit_CompSt(node->children[2], base_type);
      current_parsing_function = NULL;
      exit_scope();
    }
    free(fun_dec);
  }
}

// 全局变量定义列表
void visit_ExtDecList(ASTNode* node, Type* base_type) {
  if (node == NULL || base_type == NULL) return;
  // ExtDecList: VarDec | VarDec COMMA ExtDecList
  FieldList* var_dec = visit_VarDec(node->children[0], base_type);
  if (var_dec != NULL) {
    if (!insert_symbol(var_dec->name, var_dec->type, var_dec->lineno)) {
      print_semantic_error(ERR_REDEFINED_VARIABLE_OR_STRUCT_NAME,
                           node->children[0]->lineno, var_dec->name);
    } else {
      ASTNode* id_node = get_id_node_from_vardec(node->children[0]);
      id_node->ir_val_id = lookup_symbol_id(var_dec->name);
    }
    free(var_dec);
  }
  if (node->child_count == 3) {
    visit_ExtDecList(node->children[2], base_type);
  }
}

Type* visit_Specifier(ASTNode* node) {
  if (node == NULL) return NULL;
  Type* tp = NULL;
  if (node->children[0]->kind == TOKEN_TYPE) {
    // Specifier: TYPE
    if (strcmp(node->children[0]->val.str_val, "int") == 0) {
      tp = &type_int;
    } else if (strcmp(node->children[0]->val.str_val, "float") == 0) {
      tp = &type_float;
    }
  } else if (node->children[0]->kind == NODE_STRUCTSPECIFIER) {
    // Specifier: StructSpecifier
    tp = visit_StructSpecifier(node->children[0]);
  }
  node->val_type = tp;
  return tp;
}

// 结构体定义和结构体声明
Type* visit_StructSpecifier(ASTNode* node) {
  if (node == NULL) return NULL;
  if (node->child_count == 5) {
    // StructSpecifier: STRUCT OptTag LC DefList RC
    ASTNode* opttag_node = node->children[1];
    char* struct_name = NULL;
    if (opttag_node == NULL) {
      // OptTag: empty
      /* 由于词法分析和语法分析保证了符号名不会以数字开头，
         因此直接用<anon_count>_anon充当匿名符号名 */
      static int anon_count = 0;
      char buf[64];
      sprintf(buf, "%d_anon", anon_count++);
      struct_name = strdup(buf);
    } else {
      // OptTag: ID
      struct_name = opttag_node->children[0]->val.str_val;
      Type* tag_type = lookup_symbol(struct_name);
      if (tag_type != NULL) {
        print_semantic_error(ERR_REDEFINED_STRUCT_NAME, opttag_node->lineno,
                             struct_name);
        return NULL;
      }
    }
    Type* struct_type = (Type*)malloc(sizeof(Type));
    struct_type->kind = TYPE_STRUCTURE;
    struct_type->u.structure.name = struct_name;
    // 此处的DefList并不等于CompSt中的DefList，因为在结构体中禁止赋值
    /// FEAT: “不同结构体的域重名”或者“域和普通变量重名”均不报错
    insert_symbol(struct_name, struct_type, node->children[0]->lineno);
    struct_type->u.structure.members = visit_StructDefList(node->children[3]);
    return struct_type;
  } else if (node->child_count == 2) {
    // StructSpecifier: STRUCT Tag
    ASTNode* tag_node = node->children[1];
    // Tag: ID
    char* struct_name = tag_node->children[0]->val.str_val;
    Type* tag_type = lookup_symbol(struct_name);
    if (tag_type == NULL || tag_type->kind != TYPE_STRUCTURE) {
      print_semantic_error(ERR_UNDEFINED_STRUCT_TYPE, tag_node->lineno,
                           struct_name);
      return NULL;
    }
    return tag_type;
  }
  assert(0);
}

// StructSpecifier中的DefList和CompSt中的DefList虽然名字相同，但语义不同，因此分开处理
// StructDefList是结构体中定义的带有类型的变量列表
// 例如 struct P { int x; float y,z; }; 中的 int x; float y,z; 都在链表中
FieldList* visit_StructDefList(ASTNode* node) {
  // DefList: empty
  if (node == NULL || node->child_count == 0) return NULL;
  // DefList: Def DefList
  // 这里def和def_list都是单链表，需要连接并去重后返回
  FieldList* def = visit_StructDef(node->children[0]);
  FieldList* def_list = visit_StructDefList(node->children[1]);
  return merge_field_lists(def, def_list, ERR_REDEFINED_STRUCT_FIELD_OR_INIT);
}

// StructDef是一行的带类型的变量的列表
// 如上例，仅包含 int x; 或 float y,z;，由上层组装成完整的结构体定义
FieldList* visit_StructDef(ASTNode* node) {
  if (node == NULL) return NULL;
  // Def: Specifier DecList SEMI
  Type* base_type = visit_Specifier(node->children[0]);
  if (base_type == NULL) return NULL;
  return visit_StructDecList(node->children[1], base_type);
}

// 返回FieldList包裹的带类型变量构成的链表，由上层去重
// 如上例，它提取 y,z 这样的变量名字，利用上层传下来的base_type处理类型
FieldList* visit_StructDecList(ASTNode* node, Type* base_type) {
  if (node == NULL) return NULL;
  // DecList: Dec | Dec COMMA DecList
  FieldList* dec = visit_StructDec(node->children[0], base_type);
  FieldList* dec_list = NULL;
  if (node->child_count == 3) {
    dec_list = visit_StructDecList(node->children[2], base_type);
  }
  return merge_field_lists(dec, dec_list, ERR_REDEFINED_STRUCT_FIELD_OR_INIT);
}

// 返回FieldList包裹的带类型的单个变量的链表节点，由上层组装成链表
// 如上例，它提取单个的 y，然后利用上层传下来的base_type处理类型
// 用FieldList包裹返回单个的链表节点
FieldList* visit_StructDec(ASTNode* node, Type* base_type) {
  if (node == NULL) return NULL;
  FieldList* var_dec = visit_VarDec(node->children[0], base_type);
  if (var_dec == NULL) return NULL;
  if (node->child_count == 3) {
    // Dec: VarDec ASSIGNOP Exp
    // 这种情况应该被禁止，因为结构体定义中不允许对域进行初始化
    print_semantic_error(ERR_REDEFINED_STRUCT_FIELD_OR_INIT, node->lineno,
                         var_dec->name);
  }
  // Dec: VarDec
  return var_dec;
}

/* 返回FieldList包裹的变量信息，外层函数负责插入符号表和错误处理
   FieldList的type是变量类型，按fl->type->u.array.element_type
   逐层访问可以得到每一维的大小
   nxt部分留空给上层填写
*/
FieldList* visit_VarDec(ASTNode* node, Type* base_type) {
  if (node == NULL || base_type == NULL) return NULL;
  if (node->child_count == 1) {
    // VarDec: ID
    FieldList* fl = (FieldList*)malloc(sizeof(FieldList));
    fl->name = node->children[0]->val.str_val;
    fl->type = base_type;
    fl->nxt = NULL;
    fl->lineno = node->children[0]->lineno;
    node->val_type = base_type;
    node->children[0]->val_type = base_type;
    return fl;
  } else if (node->child_count == 4) {
    // VarDec: VarDec LB INT RB
    // 数组类型通过递归构造，每个维度用一个 TYPE_ARRAY 节点串联起来。
    // 例如 int a[2][3] 会返回一个元素类型为 int 的二维数组类型。
    /// BUG: 存在内存泄漏
    Type* array_type = (Type*)malloc(sizeof(Type));
    array_type->kind = TYPE_ARRAY;
    array_type->u.array.element_type = base_type;
    array_type->u.array.size = node->children[2]->val.int_val;
    node->val_type = array_type;
    return visit_VarDec(node->children[0], array_type);
  }
  assert(0);
}

// 在上层ExtDef注册函数名和函数类型，visit_FunDec负责处理函数头，
// 从下层VarList得到形参链表，然后去除重复参数，
// 接受上层解析的return_type，整合并包裹函数名和函数类型返回
FieldList* visit_FunDec(ASTNode* node, Type* return_type) {
  if (node == NULL) return NULL;
  char* func_name = node->children[0]->val.str_val;

  Type* func_type = (Type*)malloc(sizeof(Type));
  func_type->kind = TYPE_FUNCTION;
  func_type->u.function.ret_type = return_type;
  func_type->u.function.argc = 0;
  func_type->u.function.args = NULL;

  if (node->child_count == 4) {
    // FunDec: ID LP VarList RP
    FieldList* tmp = visit_VarList(node->children[2]);
    func_type->u.function.args =
        merge_field_lists(NULL, tmp, ERR_REDEFINED_VARIABLE_OR_STRUCT_NAME);
    FieldList* p = func_type->u.function.args;
    while (p != NULL) {
      func_type->u.function.argc++;
      p = p->nxt;
    }
  } else { /* FunDec: ID LP RP */
  }
  FieldList* res = (FieldList*)malloc(sizeof(FieldList));
  res->name = func_name;
  res->type = func_type;
  res->nxt = NULL;
  res->lineno = node->children[0]->lineno;
  return res;
}

// 处理函数头中的形参，返回FieldList包裹的形参链表，
// 上层visit_FunDec函数负责对链表去重
// 调用下层visit_ParamDec处理每个形参
FieldList* visit_VarList(ASTNode* node) {
  if (node == NULL) return NULL;
  // VarList: ParamDec COMMA VarList | ParamDec
  FieldList* fl = visit_ParamDec(node->children[0]);
  if (node->child_count == 3) {
    if (fl == NULL) return visit_VarList(node->children[2]);
    fl->nxt = visit_VarList(node->children[2]);
  }
  return fl;
}

FieldList* visit_ParamDec(ASTNode* node) {
  if (node == NULL) return NULL;
  // ParamDec: Specifier VarDec
  Type* base_type = visit_Specifier(node->children[0]);
  if (base_type == NULL) return NULL;
  return visit_VarDec(node->children[1], base_type);
}

void visit_CompSt(ASTNode* node, Type* return_type) {
  if (node == NULL) return;
  // CompSt: LC DefList StmtList RC
  FieldList* def_list = visit_DefList(node->children[1]);
  while (def_list != NULL) {
    FieldList* next = def_list->nxt;
    free(def_list);
    def_list = next;
  }
  visit_StmtList(node->children[2], return_type);
}

void visit_StmtList(ASTNode* node, Type* return_type) {
  if (node == NULL || node->child_count == 0) return;
  // StmtList: Stmt StmtList
  visit_Stmt(node->children[0], return_type);
  visit_StmtList(node->children[1], return_type);
}

void visit_Stmt(ASTNode* node, Type* return_type) {
  if (node == NULL) return;
  if (node->children[0]->kind == NODE_EXP) {
    // Stmt: Exp SEMI
    visit_Exp(node->children[0]);
  } else if (node->children[0]->kind == NODE_COMPST) {
    // Stmt: CompSt
    enter_scope();
    visit_CompSt(node->children[0], return_type);
    exit_scope();
  } else if (node->children[0]->kind == TOKEN_RETURN) {
    // Stmt: RETURN Exp SEMI
    Type* exp_type = visit_Exp(node->children[1]);
    if (exp_type && !compare_two_types(exp_type, return_type)) {
      print_semantic_error(
          ERR_RETURN_TYPE_MISMATCH, node->children[1]->lineno,
          current_parsing_function ? current_parsing_function : "unknown");
    }
  } else if (node->children[0]->kind == TOKEN_IF) {
    // Stmt: IF LP Exp RP Stmt
    Type* exp_type = visit_Exp(node->children[2]);
    if (exp_type && !compare_two_types(exp_type, &type_int)) {
      print_semantic_error(ERR_TYPE_MISMATCH_OPERATOR,
                           node->children[2]->lineno, node->children[2]->name);
    }
    visit_Stmt(node->children[4], return_type);
    if (node->child_count == 7) {
      // Stmt: IF LP Exp RP Stmt ELSE Stmt
      visit_Stmt(node->children[6], return_type);
    }
  } else if (node->children[0]->kind == TOKEN_WHILE) {
    // Stmt: WHILE LP Exp RP Stmt
    Type* exp_type = visit_Exp(node->children[2]);
    if (exp_type && !compare_two_types(exp_type, &type_int)) {
      print_semantic_error(ERR_TYPE_MISMATCH_OPERATOR,
                           node->children[2]->lineno, node->children[2]->name);
    }
    visit_Stmt(node->children[4], return_type);
  }
}

// DefList出现在CompSt以及StructSpecifier产生式的右边，它就是一串像
// int a; float b, c; int d[10];这样的变量定义Def
// 返回尚未去重的变量链表，又上层CompSt处理去重和符号表填写
FieldList* visit_DefList(ASTNode* node) {
  // DefList: empty
  if (node == NULL || node->child_count == 0) return NULL;
  // DefList: Def DefList
  // 这里def和def_list都是单链表，连接并返回
  FieldList* def = visit_Def(node->children[0]);
  FieldList* def_list = visit_DefList(node->children[1]);
  return merge_lists_nonunique(def, def_list);
}

FieldList* visit_Def(ASTNode* node) {
  if (node == NULL) return NULL;
  // Def: Specifier DecList SEMI
  Type* base_type = visit_Specifier(node->children[0]);
  // 当 base_type==NULL 时，说明类型本身就有错误了，忽略之后的全部变量定义
  if (base_type == NULL) return NULL;
  return visit_DecList(node->children[1], base_type);
}

FieldList* visit_DecList(ASTNode* node, Type* base_type) {
  if (node == NULL || base_type == NULL) return NULL;
  // DecList: Dec | Dec COMMA DecList
  FieldList* dec = visit_Dec(node->children[0], base_type);
  FieldList* dec_list = NULL;
  if (node->child_count == 3) {
    dec_list = visit_DecList(node->children[2], base_type);
  }
  return merge_lists_nonunique(dec, dec_list);
}

FieldList* visit_Dec(ASTNode* node, Type* base_type) {
  if (node == NULL || base_type == NULL) return NULL;
  FieldList* var_dec = visit_VarDec(node->children[0], base_type);
  if (var_dec == NULL) return NULL;
  if (!insert_symbol(var_dec->name, var_dec->type, var_dec->lineno)) {
    print_semantic_error(ERR_REDEFINED_VARIABLE_OR_STRUCT_NAME, var_dec->lineno,
                         var_dec->name);
    free(var_dec);
    return NULL;
  }
  ASTNode* id_node = get_id_node_from_vardec(node->children[0]);
  id_node->ir_val_id = lookup_symbol_id(var_dec->name);
  /// FEAT: 假如Exp出错，仅移除Exp，不影响VarDec部分
  if (node->child_count == 3) {
    // Dec: VarDec ASSIGNOP Exp
    Type* exp_type = visit_Exp(node->children[2]);
    if (!compare_two_types(var_dec->type, exp_type)) {
      print_semantic_error(ERR_TYPE_MISMATCH_ASSIGNMENT, var_dec->lineno,
                           var_dec->name);
    }
  }
  // Dec: VarDec
  return var_dec;
}

/// FEAT: 考虑Exp报错之后，上层会拿到null，但就不再报错

#ifdef STAGE_TWO_REQ_ONE
#define HASH_TABLE_SIZE 0x3fff
// 在整个语义分析结束之后，扫描一遍符号表检查是否有声明但没有定义的函数
void scan_function_declared_but_not_defined() {
  for (int i = 0; i < HASH_TABLE_SIZE; i++) {
    SymbolNode* tmp = hash_table[i];
    while (tmp != NULL) {
      if (tmp->type->kind == TYPE_FUNCTION &&
          tmp->type->u.function.is_defined == 0) {
        print_semantic_error(ERR_FUNCTION_DECLARED_BUT_NOT_DEFINED, tmp->lineno,
                             tmp->name);
      }
      tmp = tmp->hash_nxt;
    }
  }
}
#endif