#ifndef AST_H
#define AST_H

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"

/* 语法树节点定义 */
typedef enum {
  // 语法单元
  NODE_PROGRAM,
  NODE_EXTDEFLIST,
  NODE_EXTDEF,
  NODE_EXTDECLIST,
  NODE_SPECIFIER,
  NODE_STRUCTSPECIFIER,
  NODE_OPTTAG,
  NODE_TAG,
  NODE_VARDEC,
  NODE_FUNDEC,
  NODE_VARLIST,
  NODE_PARAMDEC,
  NODE_COMPST,
  NODE_STMTLIST,
  NODE_STMT,
  NODE_DEFLIST,
  NODE_DEF,
  NODE_DECLIST,
  NODE_DEC,
  NODE_EXP,
  NODE_ARGS,

  // 词法单元
  TOKEN_INT,
  TOKEN_FLOAT,
  TOKEN_ID,
  TOKEN_SEMI,
  TOKEN_COMMA,
  TOKEN_ASSIGNOP,
  TOKEN_RELOP,
  TOKEN_PLUS,
  TOKEN_MINUS,
  TOKEN_STAR,
  TOKEN_DIV,
  TOKEN_AND,
  TOKEN_OR,
  TOKEN_DOT,
  TOKEN_NOT,
  TOKEN_TYPE,
  TOKEN_LP,
  TOKEN_RP,
  TOKEN_LB,
  TOKEN_RB,
  TOKEN_LC,
  TOKEN_RC,
  TOKEN_STRUCT,
  TOKEN_RETURN,
  TOKEN_IF,
  TOKEN_ELSE,
  TOKEN_WHILE
} NodeKind;

// AST上的一个节点，根据kind区分是代表语法单元还是词法单元
typedef struct ASTNode {
  NodeKind kind;
  char* name;
  int lineno;
  union {
    int int_val;      // 针对 TOKEN_INT
    float float_val;  // 针对 TOKEN_FLOAT
    char* str_val;    // 针对 TOKEN_ID 和 TOKEN_TYPE
  } val;

  struct ASTNode** children;
  int child_count;
} ASTNode;

// 创建一个语法单元的节点
ASTNode* create_AST_node(NodeKind kind, const char* name, int child_count,
                         ...) {
  ASTNode* node = (ASTNode*)malloc(sizeof(ASTNode));
  node->kind = kind;
  node->name = strdup(name);
  node->child_count = child_count;
  if (child_count > 0) {
    node->children = (ASTNode**)malloc(sizeof(ASTNode*) * child_count);
  } else {
    node->children = NULL;
  }
  va_list args;
  va_start(args, child_count);
  for (int i = 0; i < child_count; i++) {
    node->children[i] = va_arg(args, ASTNode*);
  }
  va_end(args);

  // 取第一个儿子的lineno作为当前语法单元的lineno
  if (child_count > 0 && node->children[0] != NULL) {
    node->lineno = node->children[0]->lineno;
  } else {
    node->lineno = -1;
  }
  return node;
}

// 创建一个词法单元的节点，注意不会填写val部分
ASTNode* create_token_node(NodeKind kind, const char* name, int lineno) {
  ASTNode* node = (ASTNode*)malloc(sizeof(ASTNode));
  node->kind = kind;
  node->name = strdup(name);
  node->lineno = lineno;
  node->child_count = 0;
  node->children = NULL;
  return node;
}

// 先序遍历，打印信息，缩进 depth*2 个空格
void print_AST(ASTNode* node, int depth) {
  if (node == NULL) return;
  // 是语法单元且产生空串
  if (node->kind < NodeKind::TOKEN_INT && node->child_count == 0) return;

  for (int i = 0; i < depth; i++) {
    printf("  ");
  }
  if (node->kind < NodeKind::TOKEN_INT) {
    // 语法单元
    printf("%s (%d)\n", node->name, node->lineno);
  } else {
    // 词法单元
    printf("%s", node->name);
    if (node->kind == NodeKind::TOKEN_ID ||
        node->kind == NodeKind::TOKEN_TYPE) {
      printf(": %s", node->val.str_val);
    } else if (node->kind == TOKEN_INT) {
      printf(": %d", node->val.int_val);
    } else if (node->kind == TOKEN_FLOAT) {
      printf(": %f", node->val.float_val);
    }
    printf("\n");
  }

  for (int i = 0; i < node->child_count; i++) {
    print_AST(node->children[i], depth + 1);
  }
}

#endif