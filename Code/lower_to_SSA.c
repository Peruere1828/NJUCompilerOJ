#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "IR.h"
#include "IRbuilder.h"

static int vis_bb[MAX_ID];

static void add_cfg_edge(Value* pred, Value* succ) {
  if (!pred || !succ) return;

  pred->u.bb.succs = (Value**)realloc(
      pred->u.bb.succs, sizeof(Value*) * (pred->u.bb.num_succs + 1));
  pred->u.bb.succs[pred->u.bb.num_succs++] = succ;

  succ->u.bb.preds = (Value**)realloc(
      succ->u.bb.preds, sizeof(Value*) * (succ->u.bb.num_preds + 1));
  succ->u.bb.preds[succ->u.bb.num_preds++] = pred;
}

static void dfs_bb(Value* cur_bb) {
  if (!cur_bb || vis_bb[cur_bb->u.bb.bb_id]) return;
  vis_bb[cur_bb->u.bb.bb_id] = 1;

  Value* cur_inst = cur_bb->u.bb.inst_head;
  int has_unconditional_jump = 0;

  while (cur_inst != NULL) {
    if (cur_inst->u.inst.opcode == OP_GOTO) {
      Value* succ = cur_inst->u.inst.ops[0];
      add_cfg_edge(cur_bb, succ);
      dfs_bb(succ);
      has_unconditional_jump = 1;
      break;  // GOTO 是无条件跳转，其后面的块内指令属于死代码，停止处理
    } else if (cur_inst->u.inst.opcode == OP_IF_GOTO) {
      Value* succ = cur_inst->u.inst.ops[2];
      add_cfg_edge(cur_bb, succ);
      dfs_bb(succ);
      // IF_GOTO 只是分叉，依然要继续看下一条指令
    } else if (cur_inst->u.inst.opcode == OP_RETURN) {
      has_unconditional_jump = 1;
      break;  // RETURN 同样截断流向
    }
    cur_inst = cur_inst->u.inst.nxt;
  }

  // 如果一个块执行到底也没有 GOTO 或 RETURN，它会自动执行到物理上的下一个块
  if (!has_unconditional_jump && cur_bb->u.bb.next_bb) {
    add_cfg_edge(cur_bb, cur_bb->u.bb.next_bb);
    dfs_bb(cur_bb->u.bb.next_bb);
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

static void remove_dead_blocks(Value* func) {
  Value* cur = func->u.func.bb_head;
  Value* prev = NULL;
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
      /// BUG: 考虑到玩具编译器，放弃free
    } else {
      prev = cur;
      cur = cur->u.bb.next_bb;
    }
  }
}

void build_CFG(Value* func) {
  memset(vis_bb, 0, sizeof(vis_bb));

  Value* bb = func->u.func.bb_head;
  while (bb) {
    bb->u.bb.num_preds = 0;
    bb->u.bb.num_succs = 0;
    if (bb->u.bb.preds) {
      free(bb->u.bb.preds);
      bb->u.bb.preds = NULL;
    }
    if (bb->u.bb.succs) {
      free(bb->u.bb.succs);
      bb->u.bb.succs = NULL;
    }
    bb = bb->u.bb.next_bb;
  }
  optimize_jumps(func);

  memset(vis_bb, 0, sizeof(vis_bb));
  dfs_bb(func->u.func.bb_head);

  remove_dead_blocks(func);
}

static Value* rpo_dfs_ptr[MAX_ID];
static int dfn;

static void rpo_dfs(Value* cur_bb) {
  if (!cur_bb || vis_bb[cur_bb->u.bb.bb_id]) return;
  vis_bb[cur_bb->u.bb.bb_id] = 1;
  for (int i = 0; i < cur_bb->u.bb.num_succs; ++i) {
    rpo_dfs(cur_bb->u.bb.succs[i]);
  }
  rpo_dfs_ptr[++dfn] = cur_bb;
}

// 数组原地反转 [l, r] 区间
static void reverse(Value* arr[], int l, int r) {
  while (l < r) {
    Value* tmp = arr[l];
    arr[l] = arr[r];
    arr[r] = tmp;
    l++;
    r--;
  }
}

// 辅助函数：在支配树上向上回溯，寻找两个基本块的最近公共祖先
static Value* intersect(Value* b1, Value* b2) {
  Value* finger1 = b1;
  Value* finger2 = b2;

  while (finger1 != finger2) {
    // 谁的 RPO 编号更大（说明在图里越靠后/树里越靠下），谁就往上爬
    while (finger1->u.bb.rpo_idx > finger2->u.bb.rpo_idx) {
      finger1 = finger1->u.bb.idom;
    }
    while (finger2->u.bb.rpo_idx > finger1->u.bb.rpo_idx) {
      finger2 = finger2->u.bb.idom;
    }
  }
  return finger1;
}

// 将基本块 target 加入到 cur_bb 的 df 集合中（保证不重复）
static void add_to_df(Value* cur_bb, Value* target) {
  if (!cur_bb || !target) return;
  // 检查是否已经存在
  for (int i = 0; i < cur_bb->u.bb.num_df; ++i) {
    if (cur_bb->u.bb.df[i] == target) {
      return;
    }
  }

  cur_bb->u.bb.df = (Value**)realloc(
      cur_bb->u.bb.df, sizeof(Value*) * (cur_bb->u.bb.num_df + 1));
  cur_bb->u.bb.df[cur_bb->u.bb.num_df++] = target;
}

void build_IDomTree(Value* func) {
  // 计算逆后序
  memset(vis_bb, 0, sizeof(vis_bb));
  dfn = 0;
  rpo_dfs(func->u.func.bb_head);
  reverse(rpo_dfs_ptr, 1, dfn);
  for (int i = 1; i <= dfn; ++i) {
    rpo_dfs_ptr[i]->u.bb.rpo_idx = i;
    rpo_dfs_ptr[i]->u.bb.idom = NULL;
  }

  Value* entry_bb = rpo_dfs_ptr[1];
  entry_bb->u.bb.idom = entry_bb;

  int changed = 1;
  while (changed) {
    changed = 0;
    for (int i = 2; i <= dfn; ++i) {
      Value* b = rpo_dfs_ptr[i];
      Value* new_idom = NULL;
      int first_processed_idx = -1;
      for (int j = 0; j < b->u.bb.num_preds; ++j) {  //初始参照
        Value* p = b->u.bb.preds[j];
        if (p->u.bb.idom != NULL) {
          new_idom = p;
          first_processed_idx = j;
          break;
        }
      }

      assert(new_idom != NULL);

      for (int j = 0; j < b->u.bb.num_preds; ++j) {
        if (j == first_processed_idx) continue;

        Value* p = b->u.bb.preds[j];
        if (p->u.bb.idom != NULL) {
          new_idom = intersect(p, new_idom);
        }
      }

      if (b->u.bb.idom != new_idom) {
        b->u.bb.idom = new_idom;
        changed = 1;
      }
    }
  }

  // 构建正向支配树边 idom_kids
  for (int i = 2; i <= dfn; ++i) {
    Value* b = rpo_dfs_ptr[i];
    Value* parent = b->u.bb.idom;

    parent->u.bb.idom_kids =
        (Value**)realloc(parent->u.bb.idom_kids,
                         sizeof(Value*) * (parent->u.bb.num_idom_kids + 1));
    parent->u.bb.idom_kids[parent->u.bb.num_idom_kids++] = b;
  }

  // 计算 DF
  memset(vis_bb, 0, sizeof(vis_bb));
  // 对于每条p->b的CFG边，向上跳p，直到遇到idom[b]
  // 反证法可以证明不会出现idom[b]和p在不同分支的情况
  for (int i = 1; i <= dfn; ++i) {
    Value* b = rpo_dfs_ptr[i];
    // 剪枝：只有拥有多个前驱的块，才可能是别人的支配边界
    if (b->u.bb.num_preds >= 2) {
      for (int j = 0; j < b->u.bb.num_preds; ++j) {
        Value* p = b->u.bb.preds[j];
        Value* cur = p;
        while (cur != NULL && cur != b->u.bb.idom) {
          add_to_df(cur, b);
          cur = cur->u.bb.idom;
        }
      }
    }
  }
}

void build_phi(Value* func) {
  memset(vis_bb, 0, sizeof(vis_bb));
  //
}