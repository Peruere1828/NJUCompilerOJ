%{
#include <stdio.h>
#include <stdlib.h>

#include "AST.h"
#include "config.h"

/* 声明由 Flex 提供和 Bison 需要的函数 */
int yylex();
void yyerror(const char *s);
extern int yylineno;
%}

/* 定义 Tokens */
%token INT
%token FLOAT
%token ID
%token SEMI COMMA ASSIGNOP RELOP
%token PLUS MINUS STAR DIV
%token AND OR DOT NOT
%token TYPE
%token LP RP LB RB LC RC
%token STRUCT RETURN IF ELSE WHILE

/* 定义结合性和优先级，从上到下优先级递增 */
%right ASSIGNOP
%left OR
%left AND
%left RELOP
%left PLUS MINUS
%left STAR DIV
%right NOT UMINUS  /* UMINUS 是虚拟 token，用于单目负号 */
%left LP RP LB RB DOT

/* if else 匹配 */
%nonassoc LOWER_THAN_ELSE
%nonassoc ELSE

/* Debug mode */
%define parse.error verbose
%debug

%%
/* --- 产生式规则 --- */

Program:
    ExtDefList
  ;

ExtDefList:
    ExtDef ExtDefList
  | /* empty */
  ;

ExtDef:
    Specifier ExtDecList SEMI
  | Specifier SEMI
  | Specifier FunDec CompSt
  ;

ExtDecList:
    VarDec
  | VarDec COMMA ExtDecList
  ;

Specifier:
    TYPE
  | StructSpecifier
  ;

StructSpecifier:
    STRUCT OptTag LC DefList RC
  | STRUCT Tag
  ;

OptTag:
    ID
  | /* empty */
  ;

Tag:
    ID
  ;

VarDec:
    ID
  | VarDec LB INT RB
  ;

FunDec:
    ID LP VarList RP
  | ID LP RP
  ;

VarList:
    ParamDec COMMA VarList
  | ParamDec
  ;

ParamDec:
    Specifier VarDec
  ;

CompSt:
    LC DefList StmtList RC
  ;

StmtList:
    Stmt StmtList
  | /* empty */
  ;

Stmt:
    Exp SEMI
  | CompSt
  | RETURN Exp SEMI
  | IF LP Exp RP Stmt %prec LOWER_THAN_ELSE
  | IF LP Exp RP Stmt ELSE Stmt
  | WHILE LP Exp RP Stmt
  ;

DefList:
    Def DefList
  | /* empty */
  ;

Def:
    Specifier DecList SEMI
  ;

DecList:
    Dec
  | Dec COMMA DecList
  ;

Dec:
    VarDec
  | VarDec ASSIGNOP Exp
  ;

Exp:
    Exp ASSIGNOP Exp
  | Exp AND Exp
  | Exp OR Exp
  | Exp RELOP Exp
  | Exp PLUS Exp
  | Exp MINUS Exp
  | Exp STAR Exp
  | Exp DIV Exp
  | LP Exp RP
  | MINUS Exp %prec UMINUS
  | NOT Exp
  | ID LP Args RP
  | ID LP RP
  | Exp LB Exp RB
  | Exp DOT ID
  | ID
  | INT
  | FLOAT
  ;

Args:
    Exp COMMA Args
  | Exp
  ;


%%
/* --- 第三部分：C 代码实现 --- */

#include "lex.yy.c"

/* 语法错误处理函数 */
void yyerror(const char *s) {
    printf("Error type B at Line %d: Missing \"%s\".\n", yylineno, s);
}