#include "AST.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"

char* strdup(const char* src_str) {
  int len = strlen(src_str);
  char* dst_str = (char*)malloc(len * sizeof(char));
  strcpy(dst_str, src_str);
  return dst_str;
}

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
  node->lineno = -1;
  for (int i = 0; i < child_count; i++) {
    if (node->children[i] != NULL) {
      node->lineno = node->children[i]->lineno;
      break;
    }
  }
  return node;
}

ASTNode* create_token_node(NodeKind kind, const char* name, int lineno) {
  ASTNode* node = (ASTNode*)malloc(sizeof(ASTNode));
  node->kind = kind;
  node->name = strdup(name);
  node->lineno = lineno;
  node->child_count = 0;
  node->children = NULL;
  return node;
}

void print_AST(ASTNode* node, int depth) {
  if (node == NULL) return;
  // 是语法单元且产生空串
  if (node->kind < TOKEN_INT && node->child_count == 0) return;

  for (int i = 0; i < depth; i++) {
    printf("  ");
  }
  if (node->kind < TOKEN_INT) {
    // 语法单元
    printf("%s (%d)\n", node->name, node->lineno);
  } else {
    // 词法单元
    printf("%s", node->name);
    if (node->kind == TOKEN_ID || node->kind == TOKEN_TYPE) {
      printf(": %s", node->val.str_val);
    } else if (node->kind == TOKEN_INT) {
      printf(": %lu", node->val.int_val);
    } else if (node->kind == TOKEN_FLOAT) {
      printf(": %f", node->val.float_val);
    }
    printf("\n");
  }

  for (int i = 0; i < node->child_count; i++) {
    print_AST(node->children[i], depth + 1);
  }
}