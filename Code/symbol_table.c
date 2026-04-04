#include "symbol_table.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define HASH_TABLE_SIZE 0x3fff

unsigned int gen_hash(const char* name) {
  unsigned int val = 0, i;
  for (; *name; ++name) {
    val = (val << 2) + *name;
    if (i = val & ~HASH_TABLE_SIZE) val = (val ^ (i >> 12)) & HASH_TABLE_SIZE;
  }
  return val;
}

StackNode* scope_stack_top = NULL;
SymbolNode* hash_table[HASH_TABLE_SIZE];

int insert_symbol(const char* name, const Type* type, const int lineno) {
  const unsigned int ind = gen_hash(name);
  assert(scope_stack_top != NULL);
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
      if (type->kind == TYPE_STRUCTURE || cur_hash->type->kind == TYPE_STRUCTURE) {
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

  new_node->hash_nxt = hash_table[ind];
  hash_table[ind] = new_node;

  new_node->scope_nxt = scope_stack_top->symbol_head;
  scope_stack_top->symbol_head = new_node;
  return 1;
}

Type* lookup_symbol(const char* name) {
  const unsigned int ind = gen_hash(name);
  SymbolNode* cur = hash_table[ind];
  while (cur != NULL) {
    if (strcmp(cur->name, name) == 0) {
      return cur->type;
    }
    cur = cur->hash_nxt;
  }
  return NULL;
}

void enter_scope() {
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