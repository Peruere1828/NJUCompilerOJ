#ifndef SEMANTIC_H
#define SEMANTIC_H

#include "AST.h"
#include "type.h"

// 语义分析入口
void semantic_analysis(ASTNode* root);

// 访问程序节点
void visit_Program(ASTNode* node);
void visit_ExtDefList(ASTNode* node);
void visit_ExtDef(ASTNode* node);
void visit_ExtDecList(ASTNode* node, Type* base_type);
Type* visit_Specifier(ASTNode* node);
Type* visit_StructSpecifier(ASTNode* node);
FieldList* visit_StructDefList(ASTNode* node);
FieldList* visit_StructDef(ASTNode* node);
FieldList* visit_StructDecList(ASTNode* node, Type* base_type);
FieldList* visit_StructDec(ASTNode* node, Type* base_type);
FieldList* visit_VarDec(ASTNode* node, Type* base_type);
FieldList* visit_FunDec(ASTNode* node, Type* return_type);
FieldList* visit_VarList(ASTNode* node);
FieldList* visit_ParamDec(ASTNode* node);
void visit_CompSt(ASTNode* node, Type* return_type);
void visit_StmtList(ASTNode* node, Type* return_type);
void visit_Stmt(ASTNode* node, Type* return_type);
FieldList* visit_DefList(ASTNode* node);
FieldList* visit_Def(ASTNode* node);
FieldList* visit_DecList(ASTNode* node, Type* base_type);
FieldList* visit_Dec(ASTNode* node, Type* base_type);
Type* visit_Exp(ASTNode* node);

ASTNode* get_id_node_from_vardec(ASTNode* node);

#ifdef STAGE_TWO_REQ_ONE
void scan_function_declared_but_not_defined();
#endif

#endif