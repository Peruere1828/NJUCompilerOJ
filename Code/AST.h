#ifndef AST_H
#define AST_H

#include <stdarg.h>

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

typedef enum {
  RELOP_EQ = '=' + '=',
  RELOP_NEQ = '!' + '=',
  RELOP_LEQ = '<' + '=',
  RELOP_LT = '<',
  RELOP_GEQ = '>' + '=',
  RELOP_GT = '>',
} RelopKind;

// AST上的一个节点，根据kind区分是代表语法单元还是词法单元
typedef struct ASTNode {
  NodeKind kind;
  char* name;
  int lineno;
  union {
    unsigned long int_val;  // 针对 TOKEN_INT
    float float_val;        // 针对 TOKEN_FLOAT
    char* str_val;          // 针对 TOKEN_ID 和 TOKEN_TYPE
    RelopKind relop_val;    // 针对 RELOP
  } val;

  struct ASTNode** children;
  int child_count;
} ASTNode;

// 将字符串复制到新建立的空间
char* strdup(const char* src_str);

// 创建一个语法单元的节点
ASTNode* create_AST_node(NodeKind kind, const char* name, int child_count, ...);

// 创建一个词法单元的节点，注意不会填写val部分
ASTNode* create_token_node(NodeKind kind, const char* name, int lineno);

// 先序遍历，打印信息，缩进 depth*2 个空格
void print_AST(ASTNode* node, int depth);

#endif