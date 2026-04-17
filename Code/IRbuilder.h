#ifndef IRBUILDER_H
#define IRBUILDER_H

#include "IR.h"
#include "type.h"

#define MAX_ID 0x3fff

// --- 构建器上下文 ---
typedef struct IRBuilder {
  IRModule* current_module;
  Value* current_func;  // 当前正在插入的函数 (VK_FUNC)
  Value* insert_block;  // 当前正在插入指令的基本块 (VK_BB)
  Value* var_values[MAX_ID + 1];  // 存储对应VK_VAR的Value*
} IRBuilder;

// 将生成的指令插入到线性 TAC 链表中
void append_inst(IRBuilder* builder, Value* inst);

// --- 环境初始化与游标控制 ---
void init_builder(IRBuilder* builder, IRModule* module);
void builder_set_function(IRBuilder* builder, Value* func);
void builder_set_insert_point(IRBuilder* builder, Value* bb);

// --- 基础实体创建 (不涉及指令插入) ---
Value* build_const_int(unsigned long val);
Value* build_const_float(float val);
Value* build_new_block(Value* parent_func);  // 创建基本块
Value* map_declare_var(IRBuilder* builder, int frontend_id, Type* tp);
Value* map_lookup_var(IRBuilder* builder, int frontend_id);
Value* create_temp_var(Type* tp);
Value* get_or_create_func(IRModule* module, const char* name, Type* ret_type);

// --- 指令生成 (自动插入到 insert_block 并维护 Use) ---
// 1. 运算类
Value* build_binary_op(IRBuilder* builder, Opcode op, Value* lhs, Value* rhs);
Value* build_assign(IRBuilder* builder, Value* dest, Value* src);

// 2. 内存类
Value* build_dec(IRBuilder* builder, Value* var, int size);
Value* build_get_addr(IRBuilder* builder, Value* src);
Value* build_load(IRBuilder* builder, Value* src_addr, Type* tp);
Value* build_store(IRBuilder* builder, Value* dest_addr, Value* src);

// 3. 控制流类
Value* build_goto(IRBuilder* builder, Value* target_bb);
Value* build_if_goto(IRBuilder* builder, Value* lhs, RelopKind rk, Value* rhs,
                     Value* label_val);
Value* build_return(IRBuilder* builder, Value* ret_val);

// 4. 函数调用与 IO
Value* build_param(IRBuilder* builder, Value* param_val);
Value* build_arg(IRBuilder* builder, Value* arg_val);
Value* build_call(IRBuilder* builder, Value* func_val);
Value* build_read(IRBuilder* builder, Type* tp);
Value* build_write(IRBuilder* builder, Value* src);

#endif