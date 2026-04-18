#include "optimize_TAC.h"

#include <stdlib.h>
#include <string.h>

#include "IR.h"
#include "IRbuilder.h"

void optimize_TAC(IRModule* ir_module) {
  if (ir_module == NULL) return;

  Value* cur_func = ir_module->func_list;
  while (cur_func != NULL) {
    int changed = 1;
    const int max_iterations = 10;
    int iter = 0;

    while (changed && iter < max_iterations) {
      changed = 0;

      changed |= pass_simplify_CFG(cur_func);
      changed |= pass_dce(cur_func);
      /// TODO: 未来可扩展其它局部窥孔优化
      // changed |= pass_local_peephole(cur_func);
      iter++;
    }
    pass_simplify_CFG(cur_func);
    cur_func = cur_func->u.func.next_func;
  }
}

static int vis_bb[MAX_ID];

static void compute_reachability(Value* cur_bb) {
  if (!cur_bb || vis_bb[cur_bb->u.bb.bb_id]) return;
  vis_bb[cur_bb->u.bb.bb_id] = 1;
  for (int i = 0; i < cur_bb->u.bb.num_succs; i++) {
    compute_reachability(cur_bb->u.bb.succs[i]);
  }
}

static Value* get_actual_target(Value* bb) {
  if (!bb) return NULL;
  if (vis_bb[bb->u.bb.bb_id]) return NULL;  // 防止遇到 L1: GOTO L1 形成的死循环

  Value* inst = bb->u.bb.inst_head;
  Value* jump_target = NULL;
  int has_side_effect = 0;

  while (inst) {
    Opcode op = inst->u.inst.opcode;
    // 如果块内有除了 LABEL 和 GOTO 以外的指令，说明它不是空块，有副作用
    if (op != OP_LABEL && op != OP_GOTO) {
      has_side_effect = 1;
      break;
    }
    if (op == OP_GOTO) {
      jump_target = inst->u.inst.ops[0];
    }
    inst = inst->u.inst.nxt;
  }

  // 如果这是一个纯纯的空块，并且有明确的跳出目标
  if (!has_side_effect && jump_target) {
    vis_bb[bb->u.bb.bb_id] = 1;
    Value* final_target = get_actual_target(jump_target);
    vis_bb[bb->u.bb.bb_id] = 0;
    return final_target ? final_target : jump_target;
  }
  return NULL;
}

static void optimize_jumps(Value* func) {
  Value* cur_bb = func->u.func.bb_head;
  while (cur_bb) {
    Value* inst = cur_bb->u.bb.inst_head;
    while (inst) {
      if (inst->u.inst.opcode == OP_GOTO) {
        memset(vis_bb, 0, sizeof(vis_bb));
        Value* new_target = get_actual_target(inst->u.inst.ops[0]);
        if (new_target) inst->u.inst.ops[0] = new_target;
      } else if (inst->u.inst.opcode == OP_IF_GOTO) {
        memset(vis_bb, 0, sizeof(vis_bb));
        Value* new_target = get_actual_target(inst->u.inst.ops[2]);
        if (new_target) inst->u.inst.ops[2] = new_target;
      }
      inst = inst->u.inst.nxt;
    }
    cur_bb = cur_bb->u.bb.next_bb;
  }
}

static int remove_dead_blocks(Value* func) {
  Value* cur = func->u.func.bb_head;
  Value* prev = NULL;
  int changed = 0;
  while (cur) {
    if (!vis_bb[cur->u.bb.bb_id]) {
      // 从链表中彻底摘除这个未被访问到的死块 (比如 return 之后的 label)
      Value* dead = cur;
      if (prev) {
        prev->u.bb.next_bb = cur->u.bb.next_bb;
      } else {
        func->u.func.bb_head = cur->u.bb.next_bb;
      }
      if (func->u.func.bb_tail == dead) {
        func->u.func.bb_tail = prev;
      }
      cur = cur->u.bb.next_bb;
      changed = 1;
      /// BUG: 考虑到玩具编译器，放弃free
    } else {
      prev = cur;
      cur = cur->u.bb.next_bb;
    }
  }
  return changed;
}

// 专门的控制流优化 Pass：清理冗余跳转和死块
int pass_simplify_CFG(Value* func) {
  int changed = 0;
  optimize_jumps(func);
  build_CFG(func);
  memset(vis_bb, 0, sizeof(vis_bb));
  compute_reachability(func->u.func.bb_head);
  changed |= remove_dead_blocks(func);
  build_CFG(func);
  return changed;
}

// 从 def 的 use_list 中，移除特定的 user
static void remove_use(Value* def, Value* user) {
  if (def == NULL || def->use_list == NULL) return;
  Use* cur = def->use_list;
  while (cur != NULL) {
    if (cur->user == user) {
      if (cur->pre)
        cur->pre->nxt = cur->nxt;
      else
        def->use_list = cur->nxt;

      if (cur->nxt) cur->nxt->pre = cur->pre;
      free(cur);
      return;
    }
    cur = cur->nxt;
  }
}

// 删除一条指令，并清理它的数据依赖
static void delete_inst(Value* inst) {
  Value* bb = inst->u.inst.parent_bb;

  // 从基本块的指令双向链表中摘除
  if (inst->u.inst.pre)
    inst->u.inst.pre->u.inst.nxt = inst->u.inst.nxt;
  else
    bb->u.bb.inst_head = inst->u.inst.nxt;

  if (inst->u.inst.nxt)
    inst->u.inst.nxt->u.inst.pre = inst->u.inst.pre;
  else
    bb->u.bb.inst_tail = inst->u.inst.pre;

  // 释放它对别的变量的 Use
  for (int i = 0; i < inst->u.inst.num_ops; i++) {
    // 如果是赋值指令的左值 (ops[0])，它不是在使用数据，而是在定义数据，跳过
    if (inst->u.inst.opcode == OP_ASSIGN && i == 0) continue;

    Value* op = inst->u.inst.ops[i];
    if (op != NULL && (op->vk == VK_VAR || op->vk == VK_INST)) {
      remove_use(op, inst);
    }
  }
}

int pass_dce(Value* func) {
  int changed = 0;
  Value* bb = func->u.func.bb_head;

  while (bb != NULL) {
    Value* inst = bb->u.bb.inst_head;
    while (inst != NULL) {
      Value* nxt_inst = inst->u.inst.nxt;
      Opcode op = inst->u.inst.opcode;

      int has_side_effect =
          (op == OP_CALL || op == OP_WRITE || op == OP_READ ||
           op == OP_RETURN || op == OP_STORE || op == OP_GOTO ||
           op == OP_IF_GOTO || op == OP_PARAM);

      if (!has_side_effect) {
        Value* dest_val = NULL;
        if (op == OP_ASSIGN || op == OP_DEC || op == OP_GET_ADDR ||
            op == OP_LOAD) {
          dest_val = inst->u.inst.ops[0];
        } else if (op == OP_I_ADD || op == OP_I_SUB || op == OP_I_MUL ||
                   op == OP_I_DIV || op == OP_F_ADD || op == OP_F_SUB ||
                   op == OP_F_MUL || op == OP_F_DIV) {
          dest_val = inst;
        }

        // 如果这个结果值有效，且没有任何下游指令在使用它，则删除
        if (dest_val != NULL && dest_val->use_list == NULL) {
          delete_inst(inst);
          changed = 1;
        }
      }
      inst = nxt_inst;
    }
    bb = bb->u.bb.next_bb;
  }
  return changed;
}