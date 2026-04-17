#include "translate.h"

#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "semantic.h"

// 预留给编译器临时变量的 ID 分配器
static int temp_var_id_counter = MAX_ID;
int MIDEND_ERROR = 0;

IRModule* translate_program(ASTNode* root) {
  if (root == NULL) return NULL;
  IRModule* module = (IRModule*)malloc(sizeof(IRModule));
  module->func_list = NULL;
  module->global_var_list = NULL;
  module->struct_list = NULL;

  IRBuilder builder;
  init_builder(&builder, module);

  translate_ExtDefList(&builder, root->children[0]);
  return module;
}

void translate_ExtDefList(IRBuilder* builder, ASTNode* node) {
  if (node == NULL) return;
  // ExtDefList: ExtDef ExtDefList
  translate_ExtDef(builder, node->children[0]);
  translate_ExtDefList(builder, node->children[1]);
}

void translate_ExtDef(IRBuilder* builder, ASTNode* node) {
  if (node == NULL) return;

  // 我们目前的核心焦点：函数定义
  // ExtDef: Specifier FunDec CompSt
  if (node->child_count == 3 && node->children[1]->kind == NODE_FUNDEC &&
      node->children[2]->kind == NODE_COMPST) {
    // node->children[0]->val_type 已经在语义分析时记录了类型
    translate_FunDec(builder, node->children[1], node->children[0]->val_type);

    // 进入函数体解析
    translate_CompSt(builder, node->children[2]);
  }
  // 全局变量定义
  // ExtDef: Specifier ExtDecList SEMI
  else if (node->child_count == 3 &&
           node->children[1]->kind == NODE_EXTDECLIST) {
    // TODO: 预留给全局变量的降级
  }
  // 结构体定义
  // ExtDef: Specifier SEMI
  else if (node->child_count == 2) {
    // TODO: 如果需要将 AST 中的结构体同步到 IRModule->struct_list，在这里处理
  }
}

void translate_FunDec(IRBuilder* builder, ASTNode* node, Type* ret_type) {
  if (node == NULL) return;
  char* func_name = node->children[0]->val.str_val;
  Value* func_val =
      get_or_create_func(builder->current_module, func_name, ret_type);
  builder_set_function(builder, func_val);

  // 每个函数创建的时候有一个入口块
  Value* entry_bb = build_new_block(func_val);
  builder_set_insert_point(builder, entry_bb);

  // 形参
  if (node->child_count == 4) {
    translate_VarList_Params(builder, node->children[2]);
  }
}

void translate_VarList_Params(IRBuilder* builder, ASTNode* node) {
  if (node == NULL) return;
  // VarList: ParamDec COMMA VarList | ParamDec
  // ParamDec: Specifier VarDec
  ASTNode* param_dec = node->children[0];
  ASTNode* id_node = get_id_node_from_vardec(param_dec->children[1]);
  Value* param_var =
      map_declare_var(builder, id_node->ir_val_id, id_node->val_type);
  build_param(builder, param_var);
  /// FEAT: 这里变量用值传递，数组和结构体都是指针传递
  if (node->child_count == 3) {
    translate_VarList_Params(builder, node->children[2]);
  }
}

void translate_CompSt(IRBuilder* builder, ASTNode* node) {
  if (node == NULL) return;
  // CompSt: LC DefList StmtList RC

  translate_DefList(builder, node->children[1]);
  translate_StmtList(builder, node->children[2]);
}

void translate_DefList(IRBuilder* builder, ASTNode* node) {
  if (node == NULL) return;
  // DefList: Def DefList | /* empty */
  translate_Def(builder, node->children[0]);
  if (node->child_count == 2) {
    translate_DefList(builder, node->children[1]);
  }
}

void translate_Def(IRBuilder* builder, ASTNode* node) {
  if (node == NULL) return;
  // Def: Specifier DecList SEMI
  translate_DecList(builder, node->children[1]);
}

void translate_DecList(IRBuilder* builder, ASTNode* node) {
  if (node == NULL) return;
  // DecList: Dec | Dec COMMA DecList
  translate_Dec(builder, node->children[0]);
  if (node->child_count == 3) {
    translate_DecList(builder, node->children[2]);
  }
}

void translate_Dec(IRBuilder* builder, ASTNode* node) {
  if (node == NULL) return;
  // Dec: VarDec | VarDec ASSIGNOP Exp
  ASTNode* vardec_node = node->children[0];
  ASTNode* id_node = get_id_node_from_vardec(vardec_node);

  Value* var_val =
      map_declare_var(builder, id_node->ir_val_id, id_node->val_type);

  Type* var_type = id_node->val_type;
  if (var_type->kind == TYPE_ARRAY || var_type->kind == TYPE_STRUCTURE) {
    int size = calculate_type_size(var_type);
    build_dec(builder, var_val, size);
  }

  // 有初始化赋值 (Dec: VarDec ASSIGNOP Exp)
  // 语义分析之后保证了初始化都是合法的
  if (node->child_count == 3) {
    // 按照要求，我们保证了不存在ARRAY类型和STRUCTURE类型的相互直接赋值
    if (var_type->kind == TYPE_ARRAY || var_type->kind == TYPE_STRUCTURE) {
      /// FEAT: 如果真存在这种赋值，无视并记录为错误
      MIDEND_ERROR++;
    } else {
      Value* exp_val = translate_Exp(builder, node->children[2]);
      build_assign(builder, var_val, exp_val);
    }
  }
}

void translate_StmtList(IRBuilder* builder, ASTNode* node) {
  if (node == NULL || node->child_count == 0) return;
  // StmtList: Stmt StmtList
  translate_Stmt(builder, node->children[0]);
  translate_StmtList(builder, node->children[1]);
}

void translate_Stmt(IRBuilder* builder, ASTNode* node) {
  if (node == NULL) return;
  if (node->children[0]->kind == NODE_EXP) {
    // Stmt: Exp SEMI
    translate_Exp(builder, node->children[0]);
  } else if (node->children[0]->kind == NODE_COMPST) {
    // Stmt: CompSt
    translate_CompSt(builder, node->children[0]);
  } else if (node->children[0]->kind == TOKEN_RETURN) {
    // Stmt: RETURN Exp SEMI
    Value* exp_val = translate_Exp(builder, node->children[1]);
    build_return(builder, exp_val);
  } else if (node->children[0]->kind == TOKEN_IF) {
    // Stmt: IF LP Exp RP Stmt
    Value* parent_func = builder->current_func;
    Value* true_bb = build_new_block(parent_func);
    Value* false_bb = build_new_block(parent_func);
    Value* next_bb = build_new_block(parent_func);

    /*
    entry_bb:
      ...
      if <Exp> then goto true_bb
      goto false_bb
    true_bb:
      ...
      goto next_bb
    false_bb:
      ...
      goto next_bb
    next_bb:
      ...
    */

    translate_Cond(builder, node->children[2], true_bb, false_bb);

    // true分支
    builder_set_insert_point(builder, true_bb);
    translate_Stmt(builder, node->children[4]);
    build_goto(builder, next_bb);

    // false分支
    builder_set_insert_point(builder, false_bb);
    if (node->child_count == 7) {
      // Stmt: IF LP Exp RP Stmt ELSE Stmt
      translate_Stmt(builder, node->children[6]);
    }
    build_goto(builder, next_bb);

    builder_set_insert_point(builder, next_bb);
  } else if (node->children[0]->kind == TOKEN_WHILE) {
    // Stmt: WHILE LP Exp RP Stmt
    Value* parent_func = builder->current_func;

    Value* cond_bb = build_new_block(parent_func);
    Value* body_bb = build_new_block(parent_func);
    Value* next_bb = build_new_block(parent_func);

    /*
    在之后统一做loop rotation
    entry_bb:
      goto cond_bb
    cond_bb:
      ...
      if <exp> then goto body_bb
      goto next_bb
    body_bb:
      ...
      goto cond_bb
    next_bb:
      ...
    */

    build_goto(builder, cond_bb);

    builder_set_insert_point(builder, cond_bb);
    translate_Cond(builder, node->children[2], body_bb, next_bb);

    builder_set_insert_point(builder, body_bb);
    translate_Stmt(builder, node->children[4]);
    build_goto(builder, cond_bb);

    builder_set_insert_point(builder, next_bb);
  }
}

void translate_Cond(IRBuilder* builder, ASTNode* node, Value* true_bb,
                    Value* false_bb) {
  if (node == NULL || node->kind != NODE_EXP) return;
  // TODO: 补全表达式的处理
  // 1. 关系运算 (Exp RELOP Exp)
  if (node->child_count == 3 && node->children[1]->kind == TOKEN_RELOP) {
    Value* lhs = translate_Exp(builder, node->children[0]);
    Value* rhs = translate_Exp(builder, node->children[2]);
    build_if_goto(builder, lhs, node->children[1]->val.relop_val, rhs, true_bb);
    build_goto(builder, false_bb);
  }
  // 2. 逻辑与 (Exp AND Exp)
  else if (node->child_count == 3 && node->children[1]->kind == TOKEN_AND) {
    Value* mid_bb = build_new_block(builder->current_func);
    translate_Cond(builder, node->children[0], mid_bb, false_bb);
    builder_set_insert_point(builder, mid_bb);
    translate_Cond(builder, node->children[2], true_bb, false_bb);
  }
  // 3. 逻辑或 (Exp OR Exp)
  else if (node->child_count == 3 && node->children[1]->kind == TOKEN_OR) {
    Value* mid_bb = build_new_block(builder->current_func);
    translate_Cond(builder, node->children[0], true_bb, mid_bb);
    builder_set_insert_point(builder, mid_bb);
    translate_Cond(builder, node->children[2], true_bb, false_bb);
  }
  // 4. 括号包裹 (LP Exp RP)
  else if (node->child_count == 3 && node->children[0]->kind == TOKEN_LP) {
    translate_Cond(builder, node->children[1], true_bb, false_bb);
  } else if (node->child_count == 2 && node->children[0]->kind == TOKEN_NOT) {
    translate_Cond(builder, node->children[1], false_bb, true_bb);
  } else {
    // 隐式布尔转换
    Value* val = translate_Exp(builder, node);
    Value* zero = build_const_int(0);
    build_if_goto(builder, val, RELOP_NEQ, zero, true_bb);
    build_goto(builder, false_bb);
  }
}

// 辅助函数：专门用于计算左值（数组、结构体、变量）的内存地址
Value* translate_LVal_Addr(IRBuilder* builder, ASTNode* node) {
  if (node == NULL) return NULL;

  if (node->child_count == 1 && node->children[0]->kind == TOKEN_ID) {
    ASTNode* id_node = node->children[0];
    Value* var_val = map_lookup_var(builder, id_node->ir_val_id);

    TypeKind tk = id_node->val_type->kind;

    // 如果是参数，且类型是数组或结构体
    // 那么在 IR 层面，它传进来的就是一个地址，无需 GET_ADDR
    if (id_node->is_param && (tk == TYPE_ARRAY || tk == TYPE_STRUCTURE)) {
      return var_val;
    }
    // 否则（局部申请的数组/结构体），必须通过 GET_ADDR(&) 提取首地址
    else {
      return build_get_addr(builder, var_val);
    }
  } else if (node->child_count == 4 && node->children[1]->kind == TOKEN_LB) {
    // Exp: Exp1 LB Exp2 RB
    Value* base_addr = translate_LVal_Addr(builder, node->children[0]);
    Value* index_val = translate_Exp(builder, node->children[2]);

    // 计算偏移量：offset = index * sizeof(element)
    int elem_size =
        calculate_type_size(node->val_type);  // node->val_type 是元素的类型
    Value* size_val = build_const_int(elem_size);
    Value* offset = build_binary_op(builder, OP_I_MUL, index_val, size_val);

    // final_addr = base_addr + offset
    return build_binary_op(builder, OP_I_ADD, base_addr, offset);
  } else if (node->child_count == 3 && node->children[1]->kind == TOKEN_DOT) {
    // Exp: Exp DOT ID
    Value* base_addr = translate_LVal_Addr(builder, node->children[0]);

    int offset = calculate_struct_field_offset(node->children[0]->val_type,
                                               node->children[2]->val.str_val);
    if (offset == 0) return base_addr;
    Value* offset_val = build_const_int(offset);
    return build_binary_op(builder, OP_I_ADD, base_addr, offset_val);
  } else if (node->child_count >= 3 && node->children[0]->kind == TOKEN_ID &&
             node->children[1]->kind == TOKEN_LP) {
    // 函数类型
    return translate_Exp(builder, node);
  }
  return NULL;
}

static int min(int x, int y) { return (x < y ? x : y); }

Value* translate_Exp(IRBuilder* builder, ASTNode* node) {
  if (node == NULL) return NULL;
  // 常量
  if (node->child_count == 1 && node->children[0]->kind == TOKEN_INT) {
    return build_const_int(node->children[0]->val.int_val);
  }
  if (node->child_count == 1 && node->children[0]->kind == TOKEN_FLOAT) {
    return build_const_float(node->children[0]->val.float_val);
  }

  // 变量
  if (node->child_count == 1 && node->children[0]->kind == TOKEN_ID) {
    ASTNode* id_node = node->children[0];
    // 如果是结构体或数组标识符作为右值（比如传参），通常代表地址
    if (id_node->val_type->kind == TYPE_ARRAY ||
        id_node->val_type->kind == TYPE_STRUCTURE) {
      return translate_LVal_Addr(builder, node);
    }
    return map_lookup_var(builder, id_node->ir_val_id);
  }

  // 赋值
  if (node->child_count == 3 && node->children[1]->kind == TOKEN_ASSIGNOP) {
    Value* rhs = translate_Exp(builder, node->children[2]);
    ASTNode* lhs_node = node->children[0];
    TypeKind tk = lhs_node->val_type->kind;

    // 按照要求，我们保证了不存在ARRAY类型和STRUCTURE类型的相互直接赋值
    if (tk == TYPE_ARRAY || tk == TYPE_STRUCTURE) {
      /// FEAT: 如果真存在这种赋值，无视并记录为错误
      MIDEND_ERROR++;
      return rhs;
    } else {
      if (lhs_node->child_count == 1 &&
          lhs_node->children[0]->kind == TOKEN_ID) {
        Value* lhs_var =
            map_lookup_var(builder, lhs_node->children[0]->ir_val_id);
        build_assign(builder, lhs_var, rhs);
      }
      // 数组或结构体字段赋值：需要计算地址并 STORE
      else {
        Value* lhs_addr = translate_LVal_Addr(builder, lhs_node);
        build_store(builder, lhs_addr, rhs);
      }
      return rhs;  // 允许连续赋值 a = b = c
    }
  }

  // 算数运算
  if (node->child_count == 3 && (node->children[1]->kind == TOKEN_PLUS ||
                                 node->children[1]->kind == TOKEN_MINUS ||
                                 node->children[1]->kind == TOKEN_STAR ||
                                 node->children[1]->kind == TOKEN_DIV)) {
    Value* lhs = translate_Exp(builder, node->children[0]);
    Value* rhs = translate_Exp(builder, node->children[2]);

    int is_float = compare_two_types(node->val_type, &type_float);
    Opcode op;
    switch (node->children[1]->kind) {
      case TOKEN_PLUS:
        op = is_float ? OP_F_ADD : OP_I_ADD;
        break;
      case TOKEN_MINUS:
        op = is_float ? OP_F_SUB : OP_I_SUB;
        break;
      case TOKEN_STAR:
        op = is_float ? OP_F_MUL : OP_I_MUL;
        break;
      case TOKEN_DIV:
        op = is_float ? OP_F_DIV : OP_I_DIV;
        break;
      default:
        break;
    }
    return build_binary_op(builder, op, lhs, rhs);
  }

  // 单目减号
  if (node->child_count == 2 && node->children[0]->kind == TOKEN_MINUS) {
    Value* rhs = translate_Exp(builder, node->children[1]);
    int is_float = compare_two_types(node->children[1]->val_type, &type_float);
    Value* zero = is_float ? build_const_float(0.0f) : build_const_int(0);
    Opcode op = is_float ? OP_F_SUB : OP_I_SUB;
    return build_binary_op(builder, op, zero, rhs);
  }

  // 括号
  if (node->child_count == 3 && node->children[0]->kind == TOKEN_LP) {
    return translate_Exp(builder, node->children[1]);
  }

  // 函数调用
  if (node->child_count >= 3 && node->children[0]->kind == TOKEN_ID &&
      node->children[1]->kind == TOKEN_LP) {
    char* func_name = node->children[0]->val.str_val;

    // 处理特殊的内置 IO 函数
    // 其中 read 函数没有任何参数，返回值为 int 型（即读入的整数值），
    // write 函数包含一个 int 类型的参数（即要输出的整数值），
    // 返回值也为 int 型（固定返回 0）
    if (strcmp(func_name, "read") == 0) {
      return build_read(builder, &type_int);
    } else if (strcmp(func_name, "write") == 0) {
      // write(x) -> AST 中 args 对应的是 Exp
      Value* arg_val = translate_Exp(builder, node->children[2]->children[0]);

      if (arg_val->vk == VK_CONST_INT || arg_val->vk == VK_CONST_FLOAT) {
        Value* temp_var = create_temp_var(arg_val->tp);
        build_assign(builder, temp_var, arg_val);
        arg_val = temp_var;
      }
      build_write(builder, arg_val);
      return build_const_int(0);
    } else {
      // 常规函数调用；函数调用的type是返回值
      Value* func_val = get_or_create_func(builder->current_module, func_name,
                                           node->val_type);
      if (node->child_count == 4) {
        translate_Args(builder, node->children[2]);
      }
      // Call 会返回包含结果的 Instruction Value
      return build_call(builder, func_val);
    }
  }

  // 数组或结构体
  if ((node->child_count == 4 && node->children[1]->kind == TOKEN_LB) ||
      (node->child_count == 3 && node->children[1]->kind == TOKEN_DOT)) {
    Value* addr = translate_LVal_Addr(builder, node);

    // 如果访问的结果是基础类型，作为右值我们需要加载它的值（LOAD）
    if (node->val_type->kind == TYPE_BASIC) {
      return build_load(builder, addr, node->val_type);
    }
    // 如果结果依然是一个多维数组或结构体，它将衰减为指针（地址）直接传递
    return addr;
  }

  if ((node->child_count == 3 && (node->children[1]->kind == TOKEN_AND ||
                                  node->children[1]->kind == TOKEN_OR ||
                                  node->children[1]->kind == TOKEN_RELOP)) ||
      (node->child_count == 2 && node->children[0]->kind == TOKEN_NOT)) {
    Value* temp = create_temp_var(node->val_type);
    Value* true_bb = build_new_block(builder->current_func);
    Value* false_bb = build_new_block(builder->current_func);
    Value* next_bb = build_new_block(builder->current_func);

    build_assign(builder, temp, build_const_int(0));  // 默认赋 0
    translate_Cond(builder, node, true_bb, false_bb);

    builder_set_insert_point(builder, true_bb);
    build_assign(builder, temp, build_const_int(1));  // 走 true 分支赋 1
    build_goto(builder, next_bb);

    builder_set_insert_point(builder, false_bb);
    build_goto(builder, next_bb);

    builder_set_insert_point(builder, next_bb);
    return temp;
  }
  return NULL;
}

void translate_Args(IRBuilder* builder, ASTNode* node) {
  if (node == NULL) return;
  // Args: Exp COMMA Args | Exp
  if (node->child_count == 3) {
    translate_Args(builder, node->children[2]);
  }
  Value* arg_val = translate_Exp(builder, node->children[0]);
  build_arg(builder, arg_val);
}