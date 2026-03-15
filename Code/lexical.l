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
        #define RET(token) return token;
    #endif
#else
    #define RET(token) return token;
#endif

#define CREATE_AND_RET(name) \
    do { \
        yylval.node = create_token_node(TOKEN_##name, #name, yylineno); \
        RET(name); \
    } while(0)

int LEX_ERROR = 0; // lexer 遇到的错误字符数
%}

/* 开启行号追踪，方便报错 */
%option yylineno
%option noyywrap

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
[0-9]+          {
    yylval.node = create_token_node(TOKEN_INT, "INT", yylineno);
    yylval.node->val.int_val = atoi(yytext);
    RET(INT);
}
[0-9]+\.[0-9]+  {
    yylval.node = create_token_node(TOKEN_FLOAT, "FLOAT", yylineno);
    yylval.node->val.float_val = atof(yytext);
    RET(FLOAT);
}
"//".*          { /* 忽略单行注释 */ }
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
.               {
    LEX_ERROR++;
    printf("Error type A at Line %d: Mysterious character \"%s\".\n", yylineno, yytext);
}
%%