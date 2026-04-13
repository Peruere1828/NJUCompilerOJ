#include "IRbuilder.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"

int global_var_counter = 0;   // 专门给 VK_VAR 分配 v_id
int global_inst_counter = 0;  // 专门给 VK_INST 分配 t_id

void init_builder(IRBuilder* builder, IRModule* module) {
  builder->current_module = module;
  builder->current_func = NULL;
  builder->insert_block = NULL;
  for (int i = 0; i < MAX_ID; ++i) {
    builder->var_values[i] = NULL;
  }
}

void builder_set_function(IRBuilder* builder, Value* func) {
  builder->current_func = func;
}

void builder_set_insert_point(IRBuilder* builder, Value* bb) {
  builder->insert_block = bb;
}

void append_inst(IRBuilder* builder, Value* inst) {
  Value* bb = builder->insert_block;
  // 如果没有基本块接收指令，说明逻辑有误，直接报错
  assert(bb != NULL && bb->vk == VK_BB &&
         "No active basic block to insert instruction!");

  inst->u.inst.parent_bb = bb;

  if (bb->u.bb.inst_tail == NULL) {
    // 块内的第一条指令
    bb->u.bb.inst_head = inst;
    bb->u.bb.inst_tail = inst;
    inst->u.inst.pre = NULL;
    inst->u.inst.nxt = NULL;
  } else {
    // 追加到块的末尾
    inst->u.inst.pre = bb->u.bb.inst_tail;
    inst->u.inst.nxt = NULL;
    bb->u.bb.inst_tail->u.inst.nxt = inst;
    bb->u.bb.inst_tail = inst;
  }
}

// 对前端得到的稀疏的全局唯一ir_val_id离散化映射到Value
Value* map_declare_var(IRBuilder* builder, int frontend_id, Type* tp) {
  Value* var = create_value(VK_VAR, tp);
  var->id = ++global_var_counter;

  builder->var_values[frontend_id] = var;
  return var;
}

Value* map_lookup_var(IRBuilder* builder, int frontend_id) {
  Value* var = builder->var_values[frontend_id];
  assert(var != NULL);
  return var;
}

// 创建临时变量，临时变量不在var_values表里
Value* create_temp_var(Type* tp) {
  Value* var = create_value(VK_VAR, tp);
  var->id = ++global_var_counter;
  return var;
}

Value* get_or_create_func(IRModule* module, const char* name, Type* ret_type) {
  // 遍历当前模块的函数链表，查找是否已经存在
  Value* curr = module->func_list;
  while (curr != NULL) {
    if (strcmp(curr->u.func.name, name) == 0) {
      return curr;  // 找到了，直接返回
    }
    curr = curr->u.func.next_func;
  }

  // 没找到，创建新的函数实体
  Value* func_val = create_value(VK_FUNCTION, ret_type);
  func_val->u.func.name = strdup(name);  // 必须 strdup 保留字符串所有权
  func_val->u.func.parent_module = module;

  // 头插法挂载到 Module 的全局函数链表上
  func_val->u.func.next_func = module->func_list;
  module->func_list = func_val;

  return func_val;
}

Value* build_new_block(Value* parent_func) {
  static int bb_count = 0;  // 内部递增 ID
  Value* bb_val = create_value(VK_BB, NULL);
  bb_val->u.bb.bb_id = ++bb_count;
  bb_val->u.bb.parent_func = parent_func;

  // 将这个新建的基本块挂载到对应函数的块链表上
  if (parent_func != NULL) {
    if (parent_func->u.func.bb_tail == NULL) {
      parent_func->u.func.bb_head = bb_val;
      parent_func->u.func.bb_tail = bb_val;
    } else {
      parent_func->u.func.bb_tail->u.bb.next_bb = bb_val;
      parent_func->u.func.bb_tail = bb_val;
    }
  }

  return bb_val;
}

Value* build_const_int(unsigned long val) {
  Value* new_value = create_value(VK_CONST_INT, &type_int);
  new_value->u.int_val = val;
  return new_value;
}

Value* build_const_float(float val) {
  Value* new_value = create_value(VK_CONST_FLOAT, &type_int);
  new_value->u.float_val = val;
  return new_value;
}

Value* build_binary_op(IRBuilder* builder, Opcode op, Value* lhs, Value* rhs) {
  Value* inst = create_value(VK_INST, lhs->tp);
  inst->u.inst.opcode = op;
  inst->u.inst.num_ops = 2;
  inst->u.inst.ops = (Value**)malloc(sizeof(Value*) * 2);
  inst->id = ++global_inst_counter;

  inst->u.inst.ops[0] = lhs;
  inst->u.inst.ops[1] = rhs;

  add_use(lhs, inst);
  add_use(rhs, inst);
  append_inst(builder, inst);
  return inst;
}

Value* build_assign(IRBuilder* builder, Value* dest, Value* src) {
  Value* inst = create_value(VK_INST, dest->tp);
  inst->u.inst.opcode = OP_ASSIGN;
  inst->u.inst.num_ops = 2;
  inst->u.inst.ops = (Value**)malloc(sizeof(Value*) * 2);

  inst->u.inst.ops[0] = dest;
  inst->u.inst.ops[1] = src;

  add_use(dest, inst);
  add_use(src, inst);
  append_inst(builder, inst);
  return inst;
}

Value* build_goto(IRBuilder* builder, Value* label_val) {
  Value* inst = create_value(VK_INST, NULL);
  inst->u.inst.opcode = OP_GOTO;
  inst->u.inst.num_ops = 1;
  inst->u.inst.ops = (Value**)malloc(sizeof(Value*) * 1);
  inst->u.inst.ops[0] = label_val;

  add_use(label_val, inst);
  append_inst(builder, inst);
  return label_val;
}

// IF lhs rk rhs THEN GOTO label_val
Value* build_if_goto(IRBuilder* builder, Value* lhs, RelopKind rk, Value* rhs,
                     Value* label_val) {
  Value* inst = create_value(VK_INST, NULL);
  inst->u.inst.opcode = OP_IF_GOTO;
  inst->u.inst.rk = rk;
  inst->u.inst.num_ops = 3;
  inst->u.inst.ops = (Value**)malloc(sizeof(Value*) * 3);

  inst->u.inst.ops[0] = lhs;
  inst->u.inst.ops[1] = rhs;
  inst->u.inst.ops[2] = label_val;

  add_use(lhs, inst);
  add_use(rhs, inst);
  add_use(label_val, inst);

  append_inst(builder, inst);
  return inst;
}

Value* build_return(IRBuilder* builder, Value* ret_val) {
  Value* inst = create_value(VK_INST, NULL);
  inst->u.inst.opcode = OP_RETURN;
  inst->u.inst.num_ops = 1;
  inst->u.inst.ops = (Value**)malloc(sizeof(Value*) * 1);
  inst->u.inst.ops[0] = ret_val;

  add_use(ret_val, inst);
  append_inst(builder, inst);
  return inst;
}

Value* build_dec(IRBuilder* builder, Value* var_val, int size) {
  Value* inst = create_value(VK_INST, NULL);
  inst->u.inst.opcode = OP_DEC;
  inst->u.inst.num_ops = 2;
  inst->u.inst.ops = (Value**)malloc(sizeof(Value*) * 2);

  inst->u.inst.ops[0] = var_val;  // 需要开辟空间的变量 (通常是 VK_VAR)
  inst->u.inst.ops[1] = build_const_int(size);

  add_use(var_val, inst);
  append_inst(builder, inst);
  return inst;
}

// 取地址: x := &y
Value* build_get_addr(IRBuilder* builder, Value* dest, Value* src) {
  Value* inst = create_value(VK_INST, dest->tp);
  inst->u.inst.opcode = OP_GET_ADDR;
  inst->u.inst.num_ops = 2;
  inst->u.inst.ops = (Value**)malloc(sizeof(Value*) * 2);

  inst->u.inst.ops[0] = dest;
  inst->u.inst.ops[1] = src;  // 被取址的变量

  add_use(dest, inst);
  add_use(src, inst);
  append_inst(builder, inst);
  return inst;
}

// 解引用读取: x := *y (从地址 y 读取数据到 x)
Value* build_load(IRBuilder* builder, Value* dest, Value* src_addr) {
  Value* inst = create_value(VK_INST, dest->tp);
  inst->u.inst.opcode = OP_LOAD;
  inst->u.inst.num_ops = 2;
  inst->u.inst.ops = (Value**)malloc(sizeof(Value*) * 2);

  inst->u.inst.ops[0] = dest;
  inst->u.inst.ops[1] = src_addr;  // 作为地址的变量

  add_use(dest, inst);
  add_use(src_addr, inst);
  append_inst(builder, inst);
  return inst;
}

// 解引用写入: *x := y (将 y 的值写入到 x 表示的地址中)
Value* build_store(IRBuilder* builder, Value* dest_addr, Value* src) {
  Value* inst = create_value(VK_INST, NULL);
  inst->u.inst.opcode = OP_STORE;
  inst->u.inst.num_ops = 2;
  inst->u.inst.ops = (Value**)malloc(sizeof(Value*) * 2);

  inst->u.inst.ops[0] = dest_addr;  // 作为目标地址的变量
  inst->u.inst.ops[1] = src;        // 被写入的数据源

  add_use(dest_addr, inst);
  add_use(src, inst);
  append_inst(builder, inst);
  return inst;
}

// 函数形参声明: PARAM x (用在函数体开头)
Value* build_param(IRBuilder* builder, Value* param_val) {
  Value* inst = create_value(VK_INST, NULL);
  inst->u.inst.opcode = OP_PARAM;
  inst->u.inst.num_ops = 1;
  inst->u.inst.ops = (Value**)malloc(sizeof(Value*) * 1);
  inst->u.inst.ops[0] = param_val;

  add_use(param_val, inst);
  append_inst(builder, inst);
  return inst;
}

// 传实参: ARG x
Value* build_arg(IRBuilder* builder, Value* arg_val) {
  Value* inst = create_value(VK_INST, NULL);
  inst->u.inst.opcode = OP_ARG;
  inst->u.inst.num_ops = 1;
  inst->u.inst.ops = (Value**)malloc(sizeof(Value*) * 1);
  inst->u.inst.ops[0] = arg_val;

  add_use(arg_val, inst);
  append_inst(builder, inst);
  return inst;
}

// 生成: t_n := CALL f
// 注意：这个函数只生成 CALL 指令本身，不负责赋值给局部变量
Value* build_call(IRBuilder* builder, Value* func_val) {
  Type* ret_type = func_val->tp->u.function.ret_type;

  Value* inst = create_value(VK_INST, ret_type);
  inst->u.inst.opcode = OP_CALL;
  inst->u.inst.num_ops = 1;
  inst->id = ++global_inst_counter;
  inst->u.inst.ops = (Value**)malloc(sizeof(Value*) * 1);

  inst->u.inst.ops[0] = func_val;

  add_use(func_val, inst);
  append_inst(builder, inst);
  return inst;
}

// 从控制台读取: READ x
Value* build_read(IRBuilder* builder, Value* dest) {
  Value* inst = create_value(VK_INST, dest->tp);
  inst->u.inst.opcode = OP_READ;
  inst->u.inst.num_ops = 1;
  inst->u.inst.ops = (Value**)malloc(sizeof(Value*) * 1);
  inst->u.inst.ops[0] = dest;

  add_use(dest, inst);
  append_inst(builder, inst);
  return inst;
}

// 向控制台打印: WRITE x
Value* build_write(IRBuilder* builder, Value* src) {
  Value* inst = create_value(VK_INST, NULL);
  inst->u.inst.opcode = OP_WRITE;
  inst->u.inst.num_ops = 1;
  inst->u.inst.ops = (Value**)malloc(sizeof(Value*) * 1);
  inst->u.inst.ops[0] = src;

  add_use(src, inst);
  append_inst(builder, inst);
  return inst;
}