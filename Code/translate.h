#ifndef TRANSLATE_H
#define TRANSLATE_H

#include "AST.h"
#include "IRbuilder.h"
#include <stdio.h>

// --- 顶层入口 ---
void print_module(IRModule* module, FILE* out);
// 返回生成好的 IR 模块，作为后续 CFG 和 SSA 优化的起点
IRModule* translate_program(ASTNode* root);

// --- AST 遍历分发 ---
void translate_ExtDefList(IRBuilder* builder, ASTNode* node);
void translate_ExtDef(IRBuilder* builder, ASTNode* node);
void translate_FunDec(IRBuilder* builder, ASTNode* node, Type* ret_type);
void translate_VarList_Params(IRBuilder* builder, ASTNode* node);

void translate_CompSt(IRBuilder* builder, ASTNode* node);
void translate_StmtList(IRBuilder* builder, ASTNode* node);
void translate_Stmt(IRBuilder* builder, ASTNode* node);
void translate_DefList(IRBuilder* builder, ASTNode* node);
void translate_Def(IRBuilder* builder, ASTNode* node);
void translate_DecList(IRBuilder* builder, ASTNode* node);
void translate_Dec(IRBuilder* builder, ASTNode* node);

// 表达式翻译返回 Value*，以便作为上一层指令的操作数
Value* translate_Exp(IRBuilder* builder, ASTNode* node);

// 处理 if/while 中带有短路求值 (AND/OR) 的条件表达式
void translate_Cond(IRBuilder* builder, ASTNode* node, Value* true_bb,
                    Value* false_bb);
// 逆序处理实参并生成 ARG 指令
void translate_Args(IRBuilder* builder, ASTNode* node);

#endif