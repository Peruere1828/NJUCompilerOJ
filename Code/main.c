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

#ifdef STAGE_ONE_REQ_THREE
extern void check_unclosed_comment();
#endif

extern int SEMANTIC_ERROR;
extern void semantic_analysis(ASTNode* rt);

#ifdef STAGE_TWO_REQ_ONE
extern void scan_function_declared_but_not_defined();
#endif

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

#ifdef STAGE_ONE_REQ_THREE
  check_unclosed_comment();
#endif

  if (LEX_ERROR == 0 && SYNTAX_ERROR == 0 && result == 0) {
#ifdef STAGE_ONE
    print_AST(root, 0);
#endif
  } else {
    goto Failed;
  }

  semantic_analysis(root);

  if (SEMANTIC_ERROR != 0) {
    goto Failed;
  }

Failed:
  fclose(f);
  return 0;
}