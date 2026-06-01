/**
 * destroy_SSA.c — SSA 销毁 (SSA Destruction)
 *
 * ============================================================================
 * 背景
 * ============================================================================
 * 中间的 IR 在优化阶段被转换为 SSA (Static Single Assignment) 形式，
 * 即每个变量只被赋值一次。这大大简化了数据流分析和优化，但在生成机器代码
 * 之前必须将 SSA 形式销毁，转换回普通的三地址码 (TAC)。
 *
 * SSA 销毁的核心难题是 phi 节点 (φ-node)：
 *   phi 节点在控制流汇合点定义了多个可能的来源值，具体取哪一个取决于
 *   实际执行时来自哪个前驱基本块。
 *
 *   例如：
 *     if (cond)
 *       x1 = 1;
 *     else
 *       x2 = 2;
 *     x3 = phi(x1 from then, x2 from else);  ← φ 节点
 *     write(x3);
 *
 *   销毁后应变为：
 *     if (cond) goto L1 else goto L2
 *   L1:
 *     x = 1; goto L3
 *   L2:
 *     x = 2; goto L3
 *   L3:
 *     write(x);
 *
 * ============================================================================
 * 算法步骤
 * ============================================================================
 *
 * Step 1: cleanup_dead_insts_and_rebuild_cfg
 *   移除死代码（不可达的基本块内指令），重建 CFG（控制流图）。
 *   确保每个块都有明确的终结指令（GOTO 或 RETURN）。
 *
 * Step 2: split_critical_edges
 *   关键边：一条从多后继块 (num_succs≥2) 到多前驱块 (num_preds≥2) 的边。
 *   关键边会导致 phi 销毁时的拷贝错误（多个 phi 同时执行时互相覆盖）。
 *   解决方法：在关键边中间插入一个空块，使得每个 phi 拷贝串行化。
 *
 * Step 3: remove_phi_nodes
 *   核心步骤。将每个 phi 节点替换为一组 COPY 指令，放在对应前驱块的末尾。
 *   实现时使用两阶段策略（避免 phi 之间的写入-读取冲突）：
 *
 *   阶段 1 — 快照 (Snapshot)：
 *     对前驱块中的每个 phi 源值，检查它是否会在此批次中被其他 phi 覆写。
 *     如果是 → 先复制到临时变量。
 *     如果否 → 直接使用原值。
 *
 *   阶段 2 — 写入 (Commit)：
 *     将所有临时变量（或原值）COPY 到对应的 phi 目标变量。
 *     此阶段是安全的，因为所有危险的读取都已在阶段 1 完成。
 *
 *   例：x=phi(y,z), y=phi(x,w)
 *     如果直接 COPY: y→x 然后 x→y → 错误！
 *     两阶段: tmp1=y, tmp2=x; x=tmp1, y=tmp2 → 正确。
 *
 * ============================================================================
 */

#include "destroy_SSA.h"

#include <assert.h>
#include <stdlib.h>

#include "IRbuilder.h"

extern int global_inst_counter;

static void split_critical_edges(Value* func);
static void remove_phi_nodes(Value* func);
static void cleanup_dead_insts_and_rebuild_cfg(Value* func);

/* 将 SSA 转换为 TAC：清理死代码 → 拆分关键边 → 移除 phi 节点 */
void destroy_SSA(IRModule* ir_module) {
  Value* cur = ir_module->func_list;
  while (cur) {
    cleanup_dead_insts_and_rebuild_cfg(cur);
    split_critical_edges(cur);
    remove_phi_nodes(cur);
    cur = cur->u.func.next_func;
  }
}

/* ---- 清理死代码并重建 CFG ---- */
static void cleanup_dead_insts_and_rebuild_cfg(Value* func) {
  Value* bb = func->u.func.bb_head;
  while (bb != NULL) {
    Value* inst = bb->u.bb.inst_head;
    int dead_zone = 0;
    while (inst != NULL) {
      Value* nxt = inst->u.inst.nxt;
      if (dead_zone) {
        // 从基本块中剔除指令
        if (inst->u.inst.pre)
          inst->u.inst.pre->u.inst.nxt = inst->u.inst.nxt;
        else
          bb->u.bb.inst_head = inst->u.inst.nxt;
        if (inst->u.inst.nxt)
          inst->u.inst.nxt->u.inst.pre = inst->u.inst.pre;
        else
          bb->u.bb.inst_tail = inst->u.inst.pre;

        // 释放其持有的 Use，防止内存泄漏或干扰后续的 DCE
        for (int i = 0; i < inst->u.inst.num_ops; i++) {
          if (inst->u.inst.opcode == OP_ASSIGN && i == 0) continue;
          Value* op = inst->u.inst.ops[i];
          if (op != NULL && (op->vk == VK_VAR || op->vk == VK_INST)) {
            remove_use(op, inst);
          }
        }
      } else {
        Opcode op = inst->u.inst.opcode;
        // 遇到无条件跳转或返回后，后面的指令进入 dead_zone
        if (op == OP_GOTO || op == OP_RETURN) {
          dead_zone = 1;
        }
      }
      inst = nxt;
    }
    bb = bb->u.bb.next_bb;
  }
  // 清理完虚假的 tail 指令后，重新构建准确的 CFG
  build_CFG(func);

  /* After dead-code removal, some blocks may lack terminators
     (when an IF_GOTO was folded to NOP and the following GOTO
     was in dead zone).  Add explicit GOTOs so the CFG has no
     implicit fall-through edges — this prevents phi/predecessor
     mismatches in remove_phi_nodes. */
  bb = func->u.func.bb_head;
  while (bb != NULL) {
    Value* tail = bb->u.bb.inst_tail;
    if (!tail ||
        (tail->u.inst.opcode != OP_GOTO && tail->u.inst.opcode != OP_RETURN)) {
      Value* next = bb->u.bb.next_bb;
      if (next) {
        Value* g = create_value(VK_INST, NULL);
        g->u.inst.opcode = OP_GOTO;
        g->u.inst.num_ops = 1;
        g->u.inst.ops = (Value**)malloc(sizeof(Value*));
        g->u.inst.ops[0] = next;
        g->u.inst.parent_bb = bb;
        g->u.inst.pre = bb->u.bb.inst_tail;
        g->u.inst.nxt = NULL;
        if (bb->u.bb.inst_tail)
          bb->u.bb.inst_tail->u.inst.nxt = g;
        else
          bb->u.bb.inst_head = g;
        bb->u.bb.inst_tail = g;
        add_use(next, g, 0);
      }
    }
    bb = bb->u.bb.next_bb;
  }
  /* Rebuild CFG again to pick up the new explicit edges */
  build_CFG(func);
}

/* 拆分关键边 (Critical Edge Splitting)
 *
 * 关键边定义：一条 CFG 边 u→v，其中 u 的出度 ≥ 2 且 v 的入度 ≥ 2。
 *
 * 为什么要拆分？phi 节点销毁时，我们在前驱块末尾插入 COPY 指令。
 * 如果前驱块有多个后继，这些 COPY 会影响其他后继块中的 phi 节点。
 * 插入中间空块 (u→mid→v) 后，COPY 放在 mid 中，只影响 v。
 *
 * 例：拆分前
 *        if (x) goto A else goto B
 *      A:  ← 含 phi 节点
 *      B:  ← 含 phi 节点
 * 拆分后
 *        if (x) goto mid_A else goto mid_B
 *      mid_A: goto A
 *      mid_B: goto B
 *      A:  ← 含 phi 节点
 *      B:  ← 含 phi 节点
 *
 * 现在 COPY 放在 mid_A/mid_B 中，各自只影响一个后继。 */
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
          add_use(succ_bb, goto_inst, 0);

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
extern int global_inst_counter;

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

/* 移除 phi 节点 — 两阶段并行拷贝算法
 *
 * 阶段 1 (快照)：对每个前驱块，收集 phi 源值。
 *   如果源值会被本批次的另一个 phi 覆写，则先复制到临时变量。
 * 阶段 2 (写入)：将所有值 COPY 到对应的 phi 目标，顺序安全。 */
static void remove_phi_nodes(Value* func) {
  Value* bb = func->u.func.bb_head;
  while (bb) {
    int phi_count = 0;
    Value* phi_insts[512];
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
        Value* srcs[512];
        Value* safe_srcs[512];

        for (int i = 0; i < phi_count; ++i) {
          Value* phi = phi_insts[i];
          Value* src_val = NULL;
          for (int j = 0; j < phi->u.inst.num_ops; j += 2) {
            if (phi->u.inst.ops[j + 1] == pred_bb) {
              src_val = phi->u.inst.ops[j];
              break;
            }
          }
          /* If a phi node lacks an entry for this predecessor, the predecessor
             was added to the CFG after phi insertion (e.g. during critical-edge
             splitting).  Use the phi's own result as a safe fallback — this
             preserves the existing value along the new edge rather than
             crashing the compiler. */
          assert(src_val != NULL); /* CFG terminator fix should prevent this */
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
            copy_to_tmp->id = ++global_inst_counter;
            copy_to_tmp->u.inst.opcode = OP_ASSIGN;
            copy_to_tmp->u.inst.num_ops = 2;
            copy_to_tmp->u.inst.ops = (Value**)malloc(sizeof(Value*) * 2);
            copy_to_tmp->u.inst.ops[0] = tmp_var;
            copy_to_tmp->u.inst.ops[1] = src_val;
            copy_to_tmp->u.inst.parent_bb = pred_bb;

            add_use(src_val, copy_to_tmp, 1);
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
          final_assign->id = ++global_inst_counter;
          final_assign->u.inst.opcode = OP_ASSIGN;
          final_assign->u.inst.num_ops = 2;
          final_assign->u.inst.ops = (Value**)malloc(sizeof(Value*) * 2);
          final_assign->u.inst.ops[0] = dest;
          final_assign->u.inst.ops[1] = safe_srcs[i];
          final_assign->u.inst.parent_bb = pred_bb;

          add_use(safe_srcs[i], final_assign, 1);
          insert_inst_before(pred_bb, tail, final_assign);
        }
      }

      // 将这些 Phi 节点从当前块删除
      for (int i = 0; i < phi_count; ++i) {
        Value* phi = phi_insts[i];
        for (int j = 0; j < phi->u.inst.num_ops; j += 2) {
          remove_use(phi->u.inst.ops[j], phi);
        }

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