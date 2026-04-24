#include "symbol_table.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

// 符号表采用散列表 + 链地址法存储符号，同时支持可选的作用域栈。
// hash_table 用于快速全局查找名字，scope_stack_top
// 用于维护当前作用域中新增符号。 在阶段二开启 STAGE_TWO_REQ_TWO
// 时才会维护嵌套作用域，退出作用域时会回收当前层符号。

StackNode* scope_stack_top = NULL;
SymbolNode* hash_table[HASH_TABLE_SIZE + 1];

int global_var_count = 0;

int insert_symbol(const char* name, const Type* type, const int lineno) {
  const unsigned int ind = gen_hash(name);
  assert(scope_stack_top != NULL);
  // 插入符号时先检查当前作用域是否已经存在同名符号。
  // 如果开启了
  // STAGE_TWO_REQ_TWO，则仅检查当前作用域链；否则直接检查所有同名符号。
#ifndef STAGE_TWO_REQ_TWO
  // 虽然下面的循环在不开启嵌套栈时也能正确地检查当前作用域，但为了效率起见，直接遍历hash表的链表就行了
  SymbolNode* cur = hash_table[ind];
  while (cur != NULL) {
    if (strcmp(cur->name, name) == 0) {
      return 0;
    }
    cur = cur->hash_nxt;
  }
#else
  // 检查当前作用域
  SymbolNode* cur = scope_stack_top->symbol_head;
  while (cur != NULL) {
    if (strcmp(cur->name, name) == 0) {
      return 0;
    }
    cur = cur->scope_nxt;
  }
  // 检查全局结构体名冲突
  SymbolNode* cur_hash = hash_table[ind];
  while (cur_hash != NULL) {
    if (strcmp(cur_hash->name, name) == 0) {
      // 判断即将插入的符号是否是结构体定义 (判定依据: 符号名 == 结构体类型名)
      int is_new_struct_def =
          (type->kind == TYPE_STRUCTURE && type->u.structure.name != NULL &&
           strcmp(name, type->u.structure.name) == 0);

      // 判断哈希表中冲突的符号是否是结构体定义
      int is_old_struct_def =
          (cur_hash->type->kind == TYPE_STRUCTURE &&
           cur_hash->type->u.structure.name != NULL &&
           strcmp(cur_hash->name, cur_hash->type->u.structure.name) == 0);

      // 只有当其中一个是"结构体定义"时，才不允许跨作用域重名。
      // 如果两者都是普通变量（即使一个是结构体变量，一个是int变量），允许局部遮蔽。
      if (is_new_struct_def || is_old_struct_def) {
        return 0;
      }
    }
    cur_hash = cur_hash->hash_nxt;
  }
#endif
  SymbolNode* new_node = (SymbolNode*)malloc(sizeof(SymbolNode));
  new_node->name = name;
  new_node->type = type;
  new_node->depth = scope_stack_top->depth;
  new_node->lineno = lineno;
  new_node->ir_var_id = ++global_var_count;
  new_node->is_param = 0;

  new_node->hash_nxt = hash_table[ind];
  hash_table[ind] = new_node;

  new_node->scope_nxt = scope_stack_top->symbol_head;
  scope_stack_top->symbol_head = new_node;
  return 1;
}

// helper function: 查找名字对应的symbol node
SymbolNode* lookup_symbol_node(const char* name) {
  const unsigned int ind = gen_hash(name);
  SymbolNode* cur = hash_table[ind];
  while (cur != NULL) {
    if (strcmp(cur->name, name) == 0) {
      return cur;
    }
    cur = cur->hash_nxt;
  }
  return NULL;
}

Type* lookup_symbol(const char* name) {
  SymbolNode* node = lookup_symbol_node(name);
  if (node == NULL) return NULL;
  return node->type;
}

int lookup_symbol_id(const char* name) {
  SymbolNode* node = lookup_symbol_node(name);
  if (node == NULL) return -1;
  return node->ir_var_id;
}

void enter_scope() {
  // 进入新的作用域时，创建一个新的栈节点，后续插入的符号会记录到该栈节点中。
#ifdef STAGE_TWO_REQ_TWO
  StackNode* new_scope = (StackNode*)malloc(sizeof(StackNode));
  new_scope->depth = (scope_stack_top == NULL ? 0 : scope_stack_top->depth + 1);
  new_scope->nxt = scope_stack_top;
  new_scope->symbol_head = NULL;
  scope_stack_top = new_scope;
#else
  if (scope_stack_top == NULL) {
    scope_stack_top = (StackNode*)malloc(sizeof(StackNode));
    scope_stack_top->depth = 0;
    scope_stack_top->nxt = NULL;
    scope_stack_top->symbol_head = NULL;
  }
#endif
}

void exit_scope() {
  assert(scope_stack_top != NULL);
#ifdef STAGE_TWO_REQ_TWO
  // 退出当前作用域时，从hash表中移除当前层定义的符号节点，
  // 并释放这些节点所占的内存。
  SymbolNode* cur = scope_stack_top->symbol_head;
  while (cur != NULL) {
    SymbolNode* to_delete = cur;
    const unsigned int ind = gen_hash(to_delete->name);
    SymbolNode* hash_head = hash_table[ind];
    assert(hash_head != NULL);
    if (hash_head == to_delete) {
      hash_table[ind] = to_delete->hash_nxt;
    } else {
      while (hash_head->hash_nxt != to_delete) {
        hash_head = hash_head->hash_nxt;
      }
      hash_head->hash_nxt = to_delete->hash_nxt;
    }
    free(to_delete);
    cur = cur->scope_nxt;
  }
  StackNode* to_delete = scope_stack_top;
  scope_stack_top = scope_stack_top->nxt;
  free(to_delete);
#endif
}