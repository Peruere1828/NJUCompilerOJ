#include <stdio.h>

#include "AST.h"
#include "config.h"

extern FILE* yyin;
extern int yylineno;
extern char* yytext;
extern int yylex();
extern const char* get_token_name(int);
extern int yyparse();
extern int LEX_ERROR;
extern int SYNTAX_ERROR;
extern int yydebug;
extern ASTNode* root;

int main(int argc, char** argv) {
  if (argc <= 1) {
    printf("Usage: %s <filename>\n", argv[0]);
    return 1;
  }

  // 打开 Makefile 传入的测试文件
  FILE* f = fopen(argv[1], "r");
  if (!f) {
    perror(argv[1]);
    return 1;
  }

  // 将 Flex 的输入流重定向为该文件
  yyin = f;

#ifdef DEBUG
  printf("========== DEBUG MODE ON ==========\n");
  yydebug = 1;  // 让 Bison 输出 Reduce/Shift 的推导 Trace
#else
  yydebug = 0;
#endif

  int result = yyparse();

  if (LEX_ERROR == 0 && SYNTAX_ERROR == 0 && result == 0) {
    // printf("Parsing SUCCESS!\n");
    print_AST(root, 0);
  } else {
    // printf("Parsing FAILED\n");
  }
  fclose(f);
  return 0;
}