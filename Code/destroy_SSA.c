#include "destroy_SSA.h"

#include <assert.h>
#include <stdlib.h>

#include "IRbuilder.h"

extern int global_inst_counter;

static void split_critical_edges(Value* func);
static void remove_phi_nodes(Value* func);

// 仅将SSA转为TAC，不优化
void destroy_SSA(IRModule* ir_module) {
  Value* cur = ir_module->func_list;
  while (cur) {
    split_critical_edges(cur);
    remove_phi_nodes(cur);
    cur = cur->u.func.next_func;
  }
}

// 对于一条CFG边u->v，如果u的后继>=2且v的前驱>=2，此时phi节点的还原成赋值
// 可能错误覆盖原有的值，因此要加入中间块，变为u->temp->v
static void split_critical_edges(Value* func) {
  Value* cur_bb = func->u.func.bb_head;
  while (cur_bb != NULL) {
    if (cur_bb->u.bb.num_succs >= 2) {
      for (int i = 0; i < cur_bb->u.bb.num_succs; i++) {
        Value* succ_bb = cur_bb->u.bb.succs[i];

        if (succ_bb->u.bb.num_preds >= 2) {
          Value* mid_bb = build_new_block(func);

          Value* goto_inst = create_value(VK_INST, NULL);
          goto_inst->u.inst.opcode = OP_GOTO;
          goto_inst->u.inst.num_ops = 1;
          goto_inst->u.inst.ops = (Value**)malloc(sizeof(Value*) * 1);
          goto_inst->u.inst.ops[0] = succ_bb;
          goto_inst->u.inst.parent_bb = mid_bb;

          mid_bb->u.bb.inst_head = goto_inst;
          mid_bb->u.bb.inst_tail = goto_inst;
          add_use(succ_bb, goto_inst,0);

          Value* tail = cur_bb->u.bb.inst_tail;
          if (tail != NULL && tail->u.inst.opcode == OP_GOTO &&
              tail->u.inst.ops[0] == succ_bb) {
            tail->u.inst.ops[0] = mid_bb;
          } else if (tail != NULL && tail->u.inst.pre != NULL &&
                     tail->u.inst.pre->u.inst.opcode == OP_IF_GOTO &&
                     tail->u.inst.pre->u.inst.ops[2] == succ_bb) {
            tail->u.inst.pre->u.inst.ops[2] = mid_bb;
          }

          // 更新 succ_bb 中所有 Phi 节点的参数，将来源由 cur_bb 替换为 mid_bb
          Value* phi = succ_bb->u.bb.inst_head;
          while (phi != NULL && phi->u.inst.opcode == OP_PHI) {
            for (int j = 1; j < phi->u.inst.num_ops; j += 2) {
              if (phi->u.inst.ops[j] == cur_bb) {
                phi->u.inst.ops[j] = mid_bb;
              }
            }
            phi = phi->u.inst.nxt;
          }
        }
      }
    }
    cur_bb = cur_bb->u.bb.next_bb;
  }

  build_CFG(func);
}

extern int global_var_counter;  // 使用全局计数器分配 tmp

// 辅助函数：将 new_inst 插入到 target_inst 的前面
static void insert_inst_before(Value* bb, Value* target_inst, Value* new_inst) {
  new_inst->u.inst.nxt = target_inst;
  new_inst->u.inst.pre = target_inst->u.inst.pre;
  if (target_inst->u.inst.pre) {
    target_inst->u.inst.pre->u.inst.nxt = new_inst;
  } else {
    bb->u.bb.inst_head = new_inst;
  }
  target_inst->u.inst.pre = new_inst;
}

static void remove_phi_nodes(Value* func) {
  Value* bb = func->u.func.bb_head;
  while (bb) {
    int phi_count = 0;
    Value* phi_insts[256];
    Value* inst = bb->u.bb.inst_head;

    while (inst != NULL && inst->u.inst.opcode == OP_PHI) {
      phi_insts[phi_count++] = inst;
      inst = inst->u.inst.nxt;
    }

    // phi节点事实上是一个并行拷贝，为了避免脏数据，先统一拷贝到临时变量，再覆写
    if (phi_count > 0) {
      for (int p = 0; p < bb->u.bb.num_preds; ++p) {
        Value* pred_bb = bb->u.bb.preds[p];

        // 既然没有关键边，前驱块的结尾必然是 GOTO
        Value* tail = pred_bb->u.bb.inst_tail;
        assert(tail != NULL && tail->u.inst.opcode == OP_GOTO);

        // 收集所有 Phi 的 src_val
        Value* srcs[256];
        Value* safe_srcs[256];

        for (int i = 0; i < phi_count; ++i) {
          Value* phi = phi_insts[i];
          Value* src_val = NULL;
          for (int j = 0; j < phi->u.inst.num_ops; j += 2) {
            if (phi->u.inst.ops[j + 1] == pred_bb) {
              src_val = phi->u.inst.ops[j];
              break;
            }
          }
          assert(src_val != NULL);
          srcs[i] = src_val;
        }

        for (int i = 0; i < phi_count; ++i) {
          Value* src_val = srcs[i];
          int is_overwritten = 0;

          // 检查这个 src_val 是否会被本批次内的任何一个 Phi 节点覆写
          for (int k = 0; k < phi_count; ++k) {
            if (src_val == phi_insts[k]) {
              is_overwritten = 1;
              break;
            }
          }

          if (is_overwritten) {
            // src_val稍后会被覆盖，必须先复制到临时变量
            Value* tmp_var = create_value(VK_VAR, phi_insts[i]->tp);
            tmp_var->id = ++global_var_counter;

            Value* copy_to_tmp = create_value(VK_INST, phi_insts[i]->tp);
            copy_to_tmp->u.inst.opcode = OP_ASSIGN;
            copy_to_tmp->u.inst.num_ops = 2;
            copy_to_tmp->u.inst.ops = (Value**)malloc(sizeof(Value*) * 2);
            copy_to_tmp->u.inst.ops[0] = tmp_var;
            copy_to_tmp->u.inst.ops[1] = src_val;
            copy_to_tmp->u.inst.parent_bb = pred_bb;

            add_use(src_val, copy_to_tmp,1);
            insert_inst_before(pred_bb, tail, copy_to_tmp);

            safe_srcs[i] = tmp_var;
          } else {
            // 不会被覆盖，直接原样使用，省去临时变量
            safe_srcs[i] = src_val;
          }
        }

        // 因为所有危险的读取都已经在阶段一完成了快照，现在无论怎么顺序写入都是安全的
        for (int i = 0; i < phi_count; ++i) {
          Value* dest = phi_insts[i];

          Value* final_assign = create_value(VK_INST, phi_insts[i]->tp);
          final_assign->u.inst.opcode = OP_ASSIGN;
          final_assign->u.inst.num_ops = 2;
          final_assign->u.inst.ops = (Value**)malloc(sizeof(Value*) * 2);
          final_assign->u.inst.ops[0] = dest;
          final_assign->u.inst.ops[1] = safe_srcs[i];
          final_assign->u.inst.parent_bb = pred_bb;

          add_use(safe_srcs[i], final_assign,1);
          insert_inst_before(pred_bb, tail, final_assign);
        }
      }

      // 将这些 Phi 节点从当前块删除
      for (int i = 0; i < phi_count; ++i) {
        Value* phi = phi_insts[i];
        if (phi->u.inst.pre)
          phi->u.inst.pre->u.inst.nxt = phi->u.inst.nxt;
        else
          bb->u.bb.inst_head = phi->u.inst.nxt;

        if (phi->u.inst.nxt)
          phi->u.inst.nxt->u.inst.pre = phi->u.inst.pre;
        else
          bb->u.bb.inst_tail = phi->u.inst.pre;
      }
    }
    bb = bb->u.bb.next_bb;
  }
}