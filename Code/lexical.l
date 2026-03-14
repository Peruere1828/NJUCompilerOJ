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
#endif

int LEX_ERROR = 0; // lexer 遇到的错误字符数
%}

/* 开启行号追踪，方便报错 */
%option yylineno
%option noyywrap

%%
"int"|"float"   { RET(TYPE); }
"struct"        { RET(STRUCT); }
"return"        { RET(RETURN); }
"if"            { RET(IF); }
"else"          { RET(ELSE); }
"while"         { RET(WHILE); }
[a-zA-Z_][a-zA-Z0-9_]* { RET(ID); }
[0-9]+          { RET(INT); }
[0-9]+\.[0-9]+  { RET(FLOAT); }
"//".*          { /* 忽略单行注释 */ }
";"             { RET(SEMI); }
","             { RET(COMMA); }
"="             { RET(ASSIGNOP); }
">"|"<"|">="|"<="|"=="|"!=" { RET(RELOP); }
"+"             { RET(PLUS); }
"-"             { RET(MINUS); }
"*"             { RET(STAR); }
"/"             { RET(DIV); }
"&&"            { RET(AND); }
"||"            { RET(OR); }
"."             { RET(DOT); }
"!"             { RET(NOT); }
"("             { RET(LP); }
")"             { RET(RP); }
"["             { RET(LB); }
"]"             { RET(RB); }
"{"             { RET(LC); }
"}"             { RET(RC); }
[ \t\r\n]+      { /* 忽略空白字符和换行 */ }
.               {
    LEX_ERROR++;
    printf("Error type A at Line %d: Mysterious character \"%s\".\n", yylineno, yytext);
}
%%