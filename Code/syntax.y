%{
#include <stdio.h>
#include <stdlib.h>

#include "AST.h"
#include "config.h"

/* 声明由 Flex 提供和 Bison 需要的函数 */
int yylex();
void yyerror(const char *s);
extern int yylineno;

ASTNode* root;  // 用于存储语法树的根节点

int SYNTAX_ERROR = 0; // 语法错误计数器
%}

%union {
  ASTNode* node;
}

%type <node> Program ExtDefList ExtDef ExtDecList Specifier StructSpecifier OptTag Tag VarDec FunDec VarList ParamDec CompSt StmtList Stmt DefList Def DecList Dec Exp Args

/* 定义 Tokens */
%token <node> INT
%token <node> FLOAT
%token <node> ID
%token <node> SEMI COMMA ASSIGNOP RELOP
%token <node> PLUS MINUS STAR DIV
%token <node> AND OR DOT NOT
%token <node> TYPE
%token <node> LP RP LB RB LC RC
%token <node> STRUCT RETURN IF ELSE WHILE

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
    ExtDefList {
      root = $$ = create_AST_node(NODE_PROGRAM, "Program", 1, $1);
    }
  ;

ExtDefList:
    ExtDef ExtDefList {
      $$ = create_AST_node(NODE_EXTDEFLIST, "ExtDefList", 2, $1, $2);
    }
  | /* empty */ { $$ = NULL;}
  ;

ExtDef:
    Specifier ExtDecList SEMI {
      $$ = create_AST_node(NODE_EXTDEF, "ExtDef", 3, $1, $2, $3);
    }
  | Specifier SEMI {
      $$ = create_AST_node(NODE_EXTDEF, "ExtDef", 2, $1, $2);
    }
  | Specifier FunDec CompSt {
      $$ = create_AST_node(NODE_EXTDEF, "ExtDef", 3, $1, $2, $3);
    }
  | error SEMI { $$ = NULL; }
  | error CompSt { $$ = NULL; }
  ;

ExtDecList:
    VarDec {
      $$ = create_AST_node(NODE_EXTDECLIST, "ExtDecList", 1, $1);
    }
  | VarDec COMMA ExtDecList {
      $$ = create_AST_node(NODE_EXTDECLIST, "ExtDecList", 3, $1, $2, $3);
    }
  ;

Specifier:
    TYPE {
      $$ = create_AST_node(NODE_SPECIFIER, "Specifier", 1, $1);
    }
  | StructSpecifier {
      $$ = create_AST_node(NODE_SPECIFIER, "Specifier", 1, $1);
    }
  ;

StructSpecifier:
    STRUCT OptTag LC DefList RC {
      $$ = create_AST_node(NODE_STRUCTSPECIFIER, "StructSpecifier", 5, $1, $2, $3, $4, $5);
    }
  | STRUCT Tag {
      $$ = create_AST_node(NODE_STRUCTSPECIFIER, "StructSpecifier", 2, $1, $2);
    }
  | STRUCT OptTag LC error RC {
      $$ = NULL;
    }
  ;

OptTag:
    ID {
      $$ = create_AST_node(NODE_OPTTAG, "OptTag", 1, $1);
    }
  | /* empty */ { $$ = NULL;}
  ;

Tag:
    ID {
      $$ = create_AST_node(NODE_TAG, "Tag", 1, $1);
    }
  ;

VarDec:
    ID {
      $$ = create_AST_node(NODE_VARDEC, "VarDec", 1, $1);
    }
  | VarDec LB INT RB {
      $$ = create_AST_node(NODE_VARDEC, "VarDec", 4, $1, $2, $3, $4);
    }
  | VarDec LB error RB {
      $$ = NULL;
    }
  ;

FunDec:
    ID LP VarList RP {
      $$ = create_AST_node(NODE_FUNDEC, "FunDec", 4, $1, $2, $3, $4);
    }
  | ID LP RP {
      $$ = create_AST_node(NODE_FUNDEC, "FunDec", 3, $1, $2, $3);
    }
  | error RP {
      $$ = NULL;
    }
  ;

VarList:
    ParamDec COMMA VarList {
      $$ = create_AST_node(NODE_VARLIST, "VarList", 3, $1, $2, $3);
    }
  | ParamDec {
      $$ = create_AST_node(NODE_VARLIST, "VarList", 1, $1);
    }
  ;

ParamDec:
    Specifier VarDec {
      $$ = create_AST_node(NODE_PARAMDEC, "ParamDec", 2, $1, $2);
    }
  ;

CompSt:
    LC DefList StmtList RC {
      $$ = create_AST_node(NODE_COMPST, "CompSt", 4, $1, $2, $3, $4);
    }
  | LC error RC {
      $$ = NULL;
    }
  ;

StmtList:
    Stmt StmtList {
      $$ = create_AST_node(NODE_STMTLIST, "StmtList", 2, $1, $2);
    }
  | /* empty */ { $$ = NULL;}
  ;

Stmt:
    Exp SEMI {
      $$ = create_AST_node(NODE_STMT, "Stmt", 2, $1, $2);
    }
  | CompSt {
      $$ = create_AST_node(NODE_STMT, "Stmt", 1, $1);
    }
  | RETURN Exp SEMI {
      $$ = create_AST_node(NODE_STMT, "Stmt", 3, $1, $2, $3);
    }
  | IF LP Exp RP Stmt %prec LOWER_THAN_ELSE {
      $$ = create_AST_node(NODE_STMT, "Stmt", 5, $1, $2, $3, $4, $5);
    }
  | IF LP Exp RP Stmt ELSE Stmt {
      $$ = create_AST_node(NODE_STMT, "Stmt", 7, $1, $2, $3, $4, $5, $6, $7);
    }
  | WHILE LP Exp RP Stmt {
      $$ = create_AST_node(NODE_STMT, "Stmt", 5, $1, $2, $3, $4, $5);
    }
  | IF LP error RP Stmt %prec LOWER_THAN_ELSE {
      $$ = NULL;
    }
  | IF LP error RP Stmt ELSE Stmt {
      $$ = NULL;
    }
  | WHILE LP error RP Stmt {
      $$ = NULL;
    }
  | error SEMI {
      $$ = NULL;
    }
  | error CompSt { $$ = NULL; }
  ;

DefList:
    Def DefList {
      $$ = create_AST_node(NODE_DEFLIST, "DefList", 2, $1, $2);
    }
  | /* empty */ { $$ = NULL;}
  ;

Def:
    Specifier DecList SEMI {
      $$ = create_AST_node(NODE_DEF, "Def", 3, $1, $2, $3);
    }
  | error SEMI {
      $$ = NULL;
    }
  ;

DecList:
    Dec {
      $$ = create_AST_node(NODE_DECLIST, "DecList", 1, $1);
    }
  | Dec COMMA DecList {
      $$ = create_AST_node(NODE_DECLIST, "DecList", 3, $1, $2, $3);
    }
  ;

Dec:
    VarDec {
      $$ = create_AST_node(NODE_DEC, "Dec", 1, $1);
    }
  | VarDec ASSIGNOP Exp {
      $$ = create_AST_node(NODE_DEC, "Dec", 3, $1, $2, $3);
    }
  ;

Exp:
    Exp ASSIGNOP Exp {
      $$ = create_AST_node(NODE_EXP, "Exp", 3, $1, $2, $3);
    }
  | Exp AND Exp {
      $$ = create_AST_node(NODE_EXP, "Exp", 3, $1, $2, $3);
    }
  | Exp OR Exp {
      $$ = create_AST_node(NODE_EXP, "Exp", 3, $1, $2, $3);
    }
  | Exp RELOP Exp {
      $$ = create_AST_node(NODE_EXP, "Exp", 3, $1, $2, $3);
    }
  | Exp PLUS Exp {
      $$ = create_AST_node(NODE_EXP, "Exp", 3, $1, $2, $3);
    }
  | Exp MINUS Exp {
      $$ = create_AST_node(NODE_EXP, "Exp", 3, $1, $2, $3);
    }
  | Exp STAR Exp {
      $$ = create_AST_node(NODE_EXP, "Exp", 3, $1, $2, $3);
    }
  | Exp DIV Exp {
      $$ = create_AST_node(NODE_EXP, "Exp", 3, $1, $2, $3);
    }
  | LP Exp RP {
      $$ = create_AST_node(NODE_EXP, "Exp", 3, $1, $2, $3);
    }
  | MINUS Exp %prec UMINUS {
      $$ = create_AST_node(NODE_EXP, "Exp", 2, $1, $2);
    }
  | NOT Exp {
      $$ = create_AST_node(NODE_EXP, "Exp", 2, $1, $2);
    }
  | ID LP Args RP {
      $$ = create_AST_node(NODE_EXP, "Exp", 4, $1, $2, $3, $4);
    }
  | ID LP RP {
      $$ = create_AST_node(NODE_EXP, "Exp", 3, $1, $2, $3);
    }
  | Exp LB Exp RB {
      $$ = create_AST_node(NODE_EXP, "Exp", 4, $1, $2, $3, $4);
    }
  | Exp DOT ID {
      $$ = create_AST_node(NODE_EXP, "Exp", 3, $1, $2, $3);
    }
  | ID {
      $$ = create_AST_node(NODE_EXP, "Exp", 1, $1);
    }
  | INT {
      $$ = create_AST_node(NODE_EXP, "Exp", 1, $1);
    }
  | FLOAT {
      $$ = create_AST_node(NODE_EXP, "Exp", 1, $1);
    }
  | LP error RP {
      $$ = NULL; 
    }
  | ID LP error RP {
      $$ = NULL;
    }
  | Exp LB error RB {
      $$ = NULL;
    }
  ;

Args:
    Exp COMMA Args {
      $$ = create_AST_node(NODE_ARGS, "Args", 3, $1, $2, $3);
    }
  | Exp {
      $$ = create_AST_node(NODE_ARGS, "Args", 1, $1);
    }
  ;


%%
/* --- 第三部分：C 代码实现 --- */

#include "lex.yy.c"

/* 语法错误处理函数 */
void yyerror(const char *s) {
    SYNTAX_ERROR++;
    printf("Error type B at Line %d: %s.\n", yylineno, s);
}