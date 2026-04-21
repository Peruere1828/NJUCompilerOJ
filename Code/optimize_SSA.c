#include "optimize_SSA.h"

#include <assert.h>
#include <stdlib.h>

#include "IRbuilder.h"

void optimize_SSA(IRModule* ir_module) {
  if (ir_module == NULL) return;

  Value* cur_func = ir_module->func_list;
  while (cur_func != NULL) {
    int changed = 1;
    const int max_iterations = 10;
    int iter = 0;

    while (changed && iter < max_iterations) {
      changed = 0;

      changed |= pass_constant_propagation(cur_func);
      /// TODO: 未来可扩展其它局部窥孔优化
      // changed |= pass_local_peephole(cur_func);
      iter++;
    }
    cur_func = cur_func->u.func.next_func;
  }
}

static void propagate_through_uselist(Value* inst, Value* new_const) {
  Use* cur_use = inst->use_list;
  while (cur_use) {
    Use* nxt_use = cur_use->nxt;
    Value* user = cur_use->user;
    int op_index = cur_use->op_index;

    user->u.inst.ops[op_index] = new_const;

    free(cur_use);
    cur_use = nxt_use;
  }
  inst->use_list = NULL;
  inst->u.inst.opcode = OP_NOP;
}

static void replace_inst_with_const_int(Value* inst, int val) {
  Value* new_const = build_const_int(val);
  propagate_through_uselist(inst, new_const);
}

static void replace_inst_with_const_float(Value* inst, float val) {
  Value* new_const = build_const_float(val);
  propagate_through_uselist(inst, new_const);
}

int pass_constant_propagation(Value* func) {
  if (func == NULL) return 0;
  int changed = 0;

  for (Value* bb = func->u.func.bb_head; bb != NULL; bb = bb->u.bb.next_bb) {
    for (Value* inst = bb->u.bb.inst_head; inst != NULL;
         inst = inst->u.inst.nxt) {
      Opcode op = inst->u.inst.opcode;
      if (op == OP_I_ADD || op == OP_I_SUB || op == OP_I_MUL ||
          op == OP_I_DIV) {
        Value* arg1 = inst->u.inst.ops[0];
        Value* arg2 = inst->u.inst.ops[1];
        if (arg1->vk == VK_CONST_INT && arg2->vk == VK_CONST_INT) {
          int res = 0;
          int val1 = arg1->u.int_val, val2 = arg2->u.int_val;
          if (op == OP_I_ADD) {
            res = val1 + val2;
          } else if (op == OP_I_SUB) {
            res = val1 - val2;
          } else if (op == OP_I_MUL) {
            res = val1 * val2;
          } else {  // op == OP_I_DIV
            // 我们忽略除零情况
            if (val2 == 0) continue;
            res = val1 / val2;
            /// FEAT: 遵循irsim的行为，向负无穷取整
            int rem = val1 % val2;
            // 如果余数不为 0，且两个操作数异号，结果需要再减 1
            if (rem != 0 && ((val1 < 0) ^ (val2 < 0))) {
              res -= 1;
            }
          }
          replace_inst_with_const_int(inst, res);
          changed = 1;
        }
      } else if (op == OP_F_ADD || op == OP_F_SUB || op == OP_F_MUL ||
                 op == OP_F_DIV) {
        Value* arg1 = inst->u.inst.ops[0];
        Value* arg2 = inst->u.inst.ops[1];
        if (arg1->vk == VK_CONST_FLOAT && arg2->vk == VK_CONST_FLOAT) {
          float res = 0;
          float val1 = arg1->u.float_val, val2 = arg2->u.float_val;
          if (op == OP_F_ADD) {
            res = val1 + val2;
          } else if (op == OP_F_SUB) {
            res = val1 - val2;
          } else if (op == OP_F_MUL) {
            res = val1 * val2;
          } else {  // op == OP_F_DIV
            // 我们忽略除零情况
            if (val2 == 0.0) continue;
            res = val1 / val2;
          }
          replace_inst_with_const_float(inst, res);
          changed = 1;
        }
      } else if (op == OP_ASSIGN) {
        Value* arg1 = inst->u.inst.ops[0];
        if (arg1->vk == VK_CONST_INT) {
          replace_inst_with_const_int(inst, arg1->u.int_val);
          changed = 1;
        } else if (arg1->vk == VK_CONST_FLOAT) {
          replace_inst_with_const_float(inst, arg1->u.float_val);
          changed = 1;
        }
      } else if (op == OP_IF_GOTO) {
        // IF arg1 rk arg2 THEN GOTO label
        Value* arg1 = inst->u.inst.ops[0];
        Value* arg2 = inst->u.inst.ops[1];
        Value* label = inst->u.inst.ops[2];

        // 仅当两个操作数都是常量时，进行控制流折叠
        if ((arg1->vk == VK_CONST_INT && arg2->vk == VK_CONST_INT) ||
            (arg1->vk == VK_CONST_FLOAT && arg2->vk == VK_CONST_FLOAT)) {
          int is_true = 0;
          RelopKind rk = inst->u.inst.rk;

          // 在编译器中计算条件真假
          if (arg1->vk == VK_CONST_INT) {
            int v1 = arg1->u.int_val;
            int v2 = arg2->u.int_val;
            if (rk == RELOP_EQ)
              is_true = (v1 == v2);
            else if (rk == RELOP_NEQ)
              is_true = (v1 != v2);
            else if (rk == RELOP_GT)
              is_true = (v1 > v2);
            else if (rk == RELOP_LT)
              is_true = (v1 < v2);
            else if (rk == RELOP_GEQ)
              is_true = (v1 >= v2);
            else if (rk == RELOP_LEQ)
              is_true = (v1 <= v2);
          } else {
            float v1 = arg1->u.float_val;
            float v2 = arg2->u.float_val;
            if (rk == RELOP_EQ)
              is_true = (v1 == v2);
            else if (rk == RELOP_NEQ)
              is_true = (v1 != v2);
            else if (rk == RELOP_GT)
              is_true = (v1 > v2);
            else if (rk == RELOP_LT)
              is_true = (v1 < v2);
            else if (rk == RELOP_GEQ)
              is_true = (v1 >= v2);
            else if (rk == RELOP_LEQ)
              is_true = (v1 <= v2);
          }

          // 清理旧的 use_list 依赖 (无论真假，原操作数都不再被使用了)
          remove_use(arg1, inst);
          remove_use(arg2, inst);
          remove_use(label, inst);
          // 先断开 label，下面如果需要 GOTO 再重新以 index 0 挂载

          if (is_true) {
            // 条件恒为真，蜕变为无条件 GOTO
            inst->u.inst.opcode = OP_GOTO;
            inst->u.inst.num_ops = 1;
            inst->u.inst.ops[0] = label;

            // 重新挂载 label 的 use_list，因为它的 op_index 从 2 变成了 0
            add_use(label, inst, 0);
          } else {
            // 条件恒为假，这条跳转是死代码，直接作废
            inst->u.inst.opcode = OP_NOP;
            inst->u.inst.num_ops = 0;
          }

          changed = 1;
        }
      }
    }
  }

  return changed;
}