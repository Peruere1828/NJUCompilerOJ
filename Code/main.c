#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "AST.h"
#include "config.h"
#include "destroy_SSA.h"
#include "optimize_SSA.h"
#include "optimize_TAC.h"
#include "semantic.h"
#include "semantic_error.h"
#include "translate.h"
#ifdef STAGE_FOUR
#include "codegen.h"
#endif

extern FILE* yyin;
extern int yylineno;
extern char* yytext;
extern int yylex();
extern int yyparse();
extern int LEX_ERROR;
extern int SYNTAX_ERROR;
extern int yydebug;
extern ASTNode* root;

#ifdef STAGE_ONE_REQ_THREE
extern void check_unclosed_comment();
#endif

int main(int argc, char** argv) {
#if defined(STAGE_FOUR)
  if (argc <= 2) {
    printf("Usage: %s <filename> <output_s> [--phase=1|2] [--dump-ir]\n", argv[0]);
    return 1;
  }
#elif defined(STAGE_ONE) || defined(STAGE_TWO)
  if (argc <= 1) {
    printf("Usage: %s <filename>\n", argv[0]);
    return 1;
  }
#elif defined(STAGE_THREE)
  if (argc <= 2) {
    printf("Usage: %s <filename> <output_ir>\n", argv[0]);
    return 1;
  }
#endif

  // 打开 Makefile 传入的测试文件
  FILE* f = fopen(argv[1], "r");
  if (!f) {
    perror(argv[1]);
    return 1;
  }

  // 将 Flex 的输入流重定向为该文件
  yyin = f;

#ifdef STAGE_ONE
#ifdef DEBUG
  printf("========== DEBUG MODE ON ==========\n");
  yydebug = 1;  // 让 Bison 输出 Reduce/Shift 的推导 Trace
#else
  yydebug = 0;
#endif
#endif

  int result = yyparse();
  fclose(f);

#ifdef STAGE_ONE_REQ_THREE
  check_unclosed_comment();
#endif

  if (LEX_ERROR != 0 || SYNTAX_ERROR != 0 || result != 0) {
    goto Failed;
  }
#ifdef STAGE_ONE
  print_AST(root, 0);
  goto End;
#endif

  semantic_analysis(root);

#ifdef STAGE_TWO
  goto End;
#endif

  IRModule* ir_module = translate_program(root);
  if (MIDEND_ERROR != 0) {
    printf("ERROR: There are statements that we do not support.\n");
#ifndef STAGE_THREE_REQ_ONE
    printf("ERROR: There are structure variables or structure params.\n");
#endif
#ifndef STAGE_THREE_REQ_TWO
    printf("ERROR: There are array params or high order variables.\n");
#endif
    goto Failed;
  }
  FILE* out = fopen(argv[2], "w");
  if (!out) {
    perror(argv[2]);
    return 1;
  }
  lower_to_SSA(ir_module);
  optimize_SSA(ir_module);
  destroy_SSA(ir_module);
  optimize_TAC(ir_module);

#ifdef STAGE_FOUR
  if (ir_module != NULL) {
    int phase = 2;  /* 默认使用图着色寄存器分配 */
    int dump_ir = 0; /* 是否输出中间 IR 用于调试 */

    /* 解析额外命令行选项 */
    for (int i = 3; i < argc; i++) {
      if (strncmp(argv[i], "--phase=", 8) == 0)
        phase = atoi(argv[i] + 8);  /* --phase=1 栈式, --phase=2 图着色 */
      else if (strcmp(argv[i], "--dump-ir") == 0)
        dump_ir = 1;  /* 编译后输出 .ir 文件用于调试 */
    }

    /* 调试模式：输出中间 IR 到 .ir 文件 */
    if (dump_ir) {
      char ir_path[512];
      snprintf(ir_path, sizeof(ir_path), "%s.ir", argv[2]);
      FILE* ir_out = fopen(ir_path, "w");
      if (ir_out) {
        print_module(ir_module, ir_out);
        fclose(ir_out);
      }
    }

    generate_mips(ir_module, out, phase);
  }
  fclose(out);
  goto End;
#endif

  if (ir_module != NULL) {
    print_module(ir_module, out);
  }
  fclose(out);

#ifdef STAGE_THREE
  goto End;
#endif

Failed:
End:
  return 0;
}