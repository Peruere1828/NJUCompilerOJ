#include "optimize_SSA.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

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
      changed |= pass_CSE(cur_func);
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
    add_use(new_const, user, op_index);

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

// 全局CSE仅对四则运算和LOAD生效，STORE和CALL会对LOAD有副作用，导致LOAD直接失效

// 比较两个操作数，如果 a > b 返回 1，用于交换律规范化
static int compare_value(Value* a, Value* b) {
  if (a->vk != b->vk) {
    return a->vk - b->vk;
  }
  if (a->vk == VK_VAR || a->vk == VK_INST) {
    return a->id - b->id;
  }
  if (a->vk == VK_CONST_INT) {
    return a->u.int_val - b->u.int_val;
  }
  if (a->vk == VK_CONST_FLOAT) {
    return a->u.float_val - b->u.float_val;
  }
  return 0;
}

#define MAX_EXPRS 16384
Value* expr_table[MAX_EXPRS];
int expr_mem_ver[MAX_EXPRS];
int expr_count = 0;
int current_mem_version = 0;

static Value* check_trivial_phi(Value* inst) {
  if (inst == NULL || inst->u.inst.opcode != OP_PHI) return NULL;
  Value* uniform_val = NULL;
  for (int i = 0; i < inst->u.inst.num_ops; i += 2) {
    Value* val = inst->u.inst.ops[i];
    if (val == inst) continue;
    if (uniform_val == NULL) {
      uniform_val = val;
    } else if (uniform_val != val) {
      return NULL;
    }
  }
  // 如果 uniform_val 依然是 NULL，说明所有的参数都是自引用 (死代码)
  // 这种情况下直接返回 NULL 交给 DCE 清理即可
  return uniform_val;
}

static int is_identical_ops(Value* inst, Value* cached) {
  if (inst->u.inst.num_ops != cached->u.inst.num_ops) {
    return 0;
  }
  Opcode op = inst->u.inst.opcode;
  int is_commutative =
      (op == OP_I_ADD || op == OP_F_ADD || op == OP_I_MUL || op == OP_F_MUL);
  if (is_commutative && inst->u.inst.num_ops == 2) {
    Value* i0 = inst->u.inst.ops[0];
    Value* i1 = inst->u.inst.ops[1];
    Value* c0 = cached->u.inst.ops[0];
    Value* c1 = cached->u.inst.ops[1];

    if ((i0 == c0 && i1 == c1) || (i0 == c1 && i1 == c0)) {
      return 1;
    }
    return 0;
  }
  for (int i = 0; i < inst->u.inst.num_ops; i++) {
    if (inst->u.inst.ops[i] != cached->u.inst.ops[i]) {
      return 0;
    }
  }
  // 仅比较二元运算相关的指令，不比较if goto等
  return 1;
}

int gcse_bb(Value* bb) {
  int changed = 0;
  int saved_expr_count = expr_count;

  Value* inst = bb->u.bb.inst_head;
  while (inst != NULL) {
    Opcode op = inst->u.inst.opcode;

    if (op == OP_STORE || op == OP_CALL) {
      current_mem_version++;
    }

    // canonicalize_inst(inst);

    if (op == OP_PHI) {
      Value* uniform_val = check_trivial_phi(inst);
      if (uniform_val != NULL) {
        propagate_through_uselist(inst, uniform_val);
        inst = inst->u.inst.nxt;
        changed = 1;
        continue;
      }
    }

    int is_matched = 0;
    int is_computational =
        (op == OP_I_ADD || op == OP_F_ADD || op == OP_I_SUB || op == OP_F_SUB ||
         op == OP_I_MUL || op == OP_F_MUL || op == OP_I_DIV || op == OP_F_DIV ||
         op == OP_LOAD || op == OP_PHI);

    if (is_computational) {
      for (int i = 0; i < expr_count; i++) {
        Value* cached = expr_table[i];
        if (cached->u.inst.opcode != op) continue;

        // 内存版本检查：如果是 LOAD，必须是 LOAD 相同的内存版本
        if (op == OP_LOAD && expr_mem_ver[i] != current_mem_version) continue;

        if (is_identical_ops(inst, cached)) {
          propagate_through_uselist(inst, cached);
          is_matched = 1;
          changed = 1;
          break;
        }
      }
    }

    if (!is_matched && is_computational) {
      expr_table[expr_count] = inst;
      expr_mem_ver[expr_count] = current_mem_version;
      expr_count++;
    }

    inst = inst->u.inst.nxt;
  }

  for (int i = 0; i < bb->u.bb.num_idom_kids; i++) {
    changed |= gcse_bb(bb->u.bb.idom_kids[i]);
  }

  expr_count = saved_expr_count;
  return changed;
}

int pass_CSE(Value* func) {
  memset(expr_table, 0, sizeof(expr_table));
  memset(expr_mem_ver, 0, sizeof(expr_mem_ver));
  expr_count = current_mem_version = 0;
  return gcse_bb(func->u.func.bb_head);
}