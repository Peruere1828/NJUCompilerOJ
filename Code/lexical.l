%{
#include <stdio.h>
#include <stdlib.h>

#include "AST.h"
#include "config.h"
#include "syntax.tab.h"

#ifdef STAGE_ONE
    #ifdef DEBUG
        #define RET(token) do { \
            printf("[LEX] Line %3d: %-10s -> '%s'\n", yylineno, #token, yytext); \
            return token; \
        } while(0)
    #else
        #define RET(token) return token
    #endif
#else
    #define RET(token) return token
#endif

#define CREATE_AND_RET(name) \
    do { \
        yylval.node = create_token_node(TOKEN_##name, #name, yylineno); \
        RET(name); \
    } while(0)

int LEX_ERROR = 0; // lexer 遇到的错误字符数

static void handle_error(const char *);
%}

/* 开启行号追踪，方便报错 */
%option yylineno
%option noyywrap

DEC_INT     0|[1-9][0-9]*
OCT_INT     0[0-7]+
HEX_INT     0[xX][0-9a-fA-F]+

NORMAL_FLOAT [0-9]+\.[0-9]+
SCI_FLOAT    ([0-9]+\.[0-9]*|\.[0-9]+)[eE][+-]?[0-9]+

SINGLE_COMMENT  "//".*
/* 用状态机实现跨行注释 */
%x BLOCK_COMMENT

%%
"int"|"float"   {
    yylval.node = create_token_node(TOKEN_TYPE, "TYPE", yylineno);
    yylval.node->val.str_val = strdup(yytext);
    RET(TYPE);
}
"struct"        { CREATE_AND_RET(STRUCT); }
"return"        { CREATE_AND_RET(RETURN); }
"if"            { CREATE_AND_RET(IF); }
"else"          { CREATE_AND_RET(ELSE); }
"while"         { CREATE_AND_RET(WHILE); }

[a-zA-Z_][a-zA-Z0-9_]* {
    yylval.node = create_token_node(TOKEN_ID, "ID", yylineno);
    yylval.node->val.str_val = strdup(yytext);
    RET(ID);
}

{DEC_INT}       {
    yylval.node = create_token_node(TOKEN_INT, "INT", yylineno);
    yylval.node->val.int_val = (unsigned long)strtol(yytext, NULL, 10);
    RET(INT);
}

{OCT_INT}       {
#ifdef STAGE_ONE_REQ_ONE
    yylval.node = create_token_node(TOKEN_INT, "INT", yylineno);
    yylval.node->val.int_val = (unsigned long)strtol(yytext, NULL, 8);
    RET(INT);
#else
    yyless(1);
    yylval.node = create_token_node(TOKEN_INT, "INT", yylineno);
    yylval.node->val.int_val = 0;
    RET(INT);
#endif
}

{HEX_INT}       {
#ifdef STAGE_ONE_REQ_ONE
    yylval.node = create_token_node(TOKEN_INT, "INT", yylineno);
    yylval.node->val.int_val = (unsigned long)strtol(yytext, NULL, 16);
    RET(INT);
#else
    yyless(1);
    yylval.node = create_token_node(TOKEN_INT, "INT", yylineno);
    yylval.node->val.int_val = 0;
    RET(INT);
#endif
}

{NORMAL_FLOAT}  {
    yylval.node = create_token_node(TOKEN_FLOAT, "FLOAT", yylineno);
    yylval.node->val.float_val = atof(yytext);
    RET(FLOAT);
}

{SCI_FLOAT}     {
#ifdef STAGE_ONE_REQ_TWO
    yylval.node = create_token_node(TOKEN_FLOAT, "FLOAT", yylineno);
    yylval.node->val.float_val = atof(yytext);
    RET(FLOAT);
#else
    for (int i = 0; i < yyleng; i++) {
        if (yytext[i] == 'e' || yytext[i] == 'E') {
            char tmp = yytext[i];
            yytext[i] = '\0';
            yylval.node = create_token_node(TOKEN_FLOAT, "FLOAT", yylineno);
            yylval.node->val.float_val = atof(yytext);
            yytext[i] = tmp;
            yyless(i);
            break;
        } 
    }
    RET(FLOAT);
#endif
}

{SINGLE_COMMENT}    {
#ifdef STAGE_ONE_REQ_THREE 
    /* 忽略单行注释 */
#else
    yyless(1);
    CREATE_AND_RET(DIV);
#endif
}

"/*"                {
#ifdef STAGE_ONE_REQ_THREE 
    BEGIN(BLOCK_COMMENT);
#else
    yyless(1);
    CREATE_AND_RET(DIV);
#endif    
}

<BLOCK_COMMENT>{
    "*/"    { BEGIN(INITIAL); }
    .|\n    { /* 忽略注释 */ }
}

";"             { CREATE_AND_RET(SEMI); }
","             { CREATE_AND_RET(COMMA); }
"="             { CREATE_AND_RET(ASSIGNOP); }
">"|"<"|">="|"<="|"=="|"!=" { CREATE_AND_RET(RELOP); }
"+"             { CREATE_AND_RET(PLUS); }
"-"             { CREATE_AND_RET(MINUS); }
"*"             { CREATE_AND_RET(STAR); }
"/"             { CREATE_AND_RET(DIV); }
"&&"            { CREATE_AND_RET(AND); }
"||"            { CREATE_AND_RET(OR); }
"."             { CREATE_AND_RET(DOT); }
"!"             { CREATE_AND_RET(NOT); }
"("             { CREATE_AND_RET(LP); }
")"             { CREATE_AND_RET(RP); }
"["             { CREATE_AND_RET(LB); }
"]"             { CREATE_AND_RET(RB); }
"{"             { CREATE_AND_RET(LC); }
"}"             { CREATE_AND_RET(RC); }
[ \t\r\n]+      { /* 忽略空白字符和换行 */ }
.               { handle_error(yytext); }
%%

static void handle_error(const char *error_str) {
    LEX_ERROR++;
    printf("Error type A at Line %d: Mysterious character \"%s\".\n", yylineno, error_str);
}