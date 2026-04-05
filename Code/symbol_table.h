#ifndef SYMBOL_TABLE_H
#define SYMBOL_TABLE_H

#include "config.h"
#include "type.h"

unsigned int gen_hash(const char* name);

typedef struct SymbolNode {
  const char* name;
  Type* type;
  int depth;
  int lineno;

  struct SymbolNode* scope_nxt;
  struct SymbolNode* hash_nxt;  // 开链法的下一个hash相同的符号
} SymbolNode;

typedef struct StackNode {
  int depth;  // 当不开启嵌套栈时，depth始终为0；当开启嵌套栈时，depth表示当前作用域的深度
  struct StackNode* nxt;
  SymbolNode* symbol_head;
} StackNode;

// 作用域栈是一个只允许在头部插入的单向链表
extern StackNode* scope_stack_top;
extern SymbolNode* hash_table[];

int insert_symbol(const char* name, const Type* type, const int lineno);
Type* lookup_symbol(const char* name);

void enter_scope();
void exit_scope();

#endif