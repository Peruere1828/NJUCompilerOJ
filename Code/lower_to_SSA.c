#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "IR.h"
#include "IRbuilder.h"

extern int global_inst_counter;

// 仅将TAC翻译为SSA，不优化
void lower_to_SSA(IRModule* ir_module) {
  Value* cur = ir_module->func_list;
  while (cur) {
    build_CFG(cur);
    build_IDomTree(cur);
    insert_phi_nodes(cur);
    rename_variables(cur);
    cur = cur->u.func.next_func;
  }
}

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
  dfs_bb(func->u.func.bb_head);
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

// 将基本块 target 加入到 cur_bb 的 df 集合中
static void add_to_df(Value* cur_bb, Value* target) {
  if (!cur_bb || !target) return;
  // 去重
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

typedef struct BBNode {
  Value* bb;
  struct BBNode* next;
} BBNode;

// def_blocks[v_id] 存储了所有对 v_id 进行了赋值的基本块链表
static BBNode* def_blocks[MAX_ID + 1];

static void add_def_block(int v_id, Value* bb) {
  BBNode* cur = def_blocks[v_id];
  // 去重
  while (cur != NULL) {
    if (cur->bb == bb) return;
    cur = cur->next;
  }

  BBNode* node = (BBNode*)malloc(sizeof(BBNode));
  node->bb = bb;
  node->next = def_blocks[v_id];
  def_blocks[v_id] = node;
}

static int in_worklist[MAX_ID + 1];
static int has_phi_inserted[MAX_ID + 1];

// 收集可晋升变量及其定义点
static void collect_def_sites(Value* func) {
  // 清空def_blocks
  for (int i = 0; i <= MAX_ID; ++i) {
    BBNode* cur = def_blocks[i];
    while (cur != NULL) {
      BBNode* tmp = cur->next;
      free(cur);
      cur = tmp;
    }
    def_blocks[i] = NULL;
  }

  Value* cur_bb = func->u.func.bb_head;
  while (cur_bb != NULL) {
    Value* inst = cur_bb->u.bb.inst_head;

    while (inst != NULL) {
      Opcode op = inst->u.inst.opcode;

      // 改变变量值的指令
      // param相当于把外界变量赋值到函数内部
      if (op == OP_ASSIGN || op == OP_PARAM) {
        Value* dest = inst->u.inst.ops[0];

        // 只有基础类型的 VK_VAR 可以晋升为 SSA
        if (dest != NULL && dest->vk == VK_VAR && dest->tp &&
            dest->tp->kind == TYPE_BASIC) {
          add_def_block(dest->id, cur_bb);
        }
      }
      inst = inst->u.inst.nxt;
    }
    cur_bb = cur_bb->u.bb.next_bb;
  }
}

// 在bb这个块开头插入一个为v_id变量服务的phi节点
static void insert_phi_at_head(Value* bb, int v_id) {
  Value* phi_value = create_value(VK_INST, NULL);
  phi_value->id = ++global_inst_counter;
  phi_value->u.inst.rk = v_id;  // 暂且把原始id放在rk里
  phi_value->u.inst.opcode = OP_PHI;
  phi_value->u.inst.parent_bb = bb;
  if (bb->u.bb.inst_tail == NULL) {
    bb->u.bb.inst_head = phi_value;
    bb->u.bb.inst_tail = phi_value;
    phi_value->u.inst.pre = NULL;
    phi_value->u.inst.nxt = NULL;
  } else {
    phi_value->u.inst.nxt = bb->u.bb.inst_head;
    phi_value->u.inst.pre = NULL;
    bb->u.bb.inst_head->u.inst.pre = phi_value;
    bb->u.bb.inst_head = phi_value;
  }
}

void insert_phi_nodes(Value* func) {
  // 由于不存在全局变量，所有的当前函数需要用的变量都应该在当前函数内
  collect_def_sites(func);

  for (int v_id = 0; v_id <= MAX_ID; ++v_id) {
    if (def_blocks[v_id] == NULL) continue;

    memset(in_worklist, 0, sizeof(in_worklist));
    memset(has_phi_inserted, 0, sizeof(has_phi_inserted));

    // phi节点也是一次定义，可能会让后续块也要插入phi
    // 因此用一个工作队列确保所有后续的块都能正确插入phi
    // 同时需要保证一个变量的phi在一个块开头只能插入一次
    Value* worklist[1000];
    int head = 0, tail = 0;

    BBNode* cur = def_blocks[v_id];
    while (cur != NULL) {
      worklist[tail++] = cur->bb;
      in_worklist[cur->bb->u.bb.bb_id] = 1;
      cur = cur->next;
    }

    while (head < tail) {
      Value* x = worklist[head++];

      for (int j = 0; j < x->u.bb.num_df; ++j) {
        Value* y = x->u.bb.df[j];
        int y_id = y->u.bb.bb_id;

        if (!has_phi_inserted[y_id]) {
          insert_phi_at_head(y, v_id);
          has_phi_inserted[y_id] = 1;
          if (!in_worklist[y_id]) {
            in_worklist[y_id] = 1;
            worklist[tail++] = y;
          }
        }
      }
    }
  }
}

// 定义栈，封装成context使用
typedef struct VersionNode {
  Value* val;
  struct VersionNode* next;
} VersionNode;
typedef struct {
  VersionNode* stack_head[MAX_ID + 1];
} RenameContext;
static void push_value(RenameContext* ctx, int v_id, Value* val) {
  VersionNode* node = (VersionNode*)malloc(sizeof(VersionNode));
  node->val = val;
  node->next = ctx->stack_head[v_id];
  ctx->stack_head[v_id] = node;
}
static Value* top_value(RenameContext* ctx, int v_id) {
  if (ctx->stack_head[v_id] == NULL) return NULL;
  return ctx->stack_head[v_id]->val;
}
static void pop_value(RenameContext* ctx, int v_id) {
  VersionNode* node = ctx->stack_head[v_id];
  if (node != NULL) {
    ctx->stack_head[v_id] = node->next;
    free(node);
  }
}

/// BUG: 可能应该添加专门的default作为未初始化
static Value* get_zero_value(Type* tp) {
  if (tp && tp->kind == TYPE_BASIC && tp->u.basic == BASIC_FLOAT) {
    Value* zero = create_value(VK_CONST_FLOAT, &type_float);
    zero->u.float_val = 0.0f;
    return zero;
  } else {
    // 默认回退到整数 0
    Value* zero = create_value(VK_CONST_INT, &type_int);
    zero->u.int_val = 0;
    return zero;
  }
}

static void rename_dfs(RenameContext* ctx, Value* cur_bb) {
  if (cur_bb == NULL || cur_bb->vk != VK_BB) return;
  // 当前块中值的更新
  int pushed_vids[MAX_ID + 1];
  int push_count = 0;

  // 遍历所有指令
  Value* inst = cur_bb->u.bb.inst_head;
  while (inst != NULL) {
    Opcode opcode = inst->u.inst.opcode;
    Value* nxt_inst = inst->u.inst.nxt;

    // 更新use链
    if (opcode != OP_PHI) {
      for (int i = 0; i < inst->u.inst.num_ops; ++i) {
        // 跳过 OP_ASSIGN 和 OP_PARAM 的左值
        if (inst->u.inst.opcode == OP_ASSIGN && i == 0) continue;
        if (inst->u.inst.opcode == OP_PARAM && i == 0) continue;

        Value* op = inst->u.inst.ops[i];
        if (op != NULL && op->vk == VK_VAR && op->tp &&
            op->tp->kind == TYPE_BASIC) {
          Value* latest = top_value(ctx, op->id);
          if (latest == NULL) {
            // 默认给未初始化的变量赋 0
            latest = get_zero_value(op->tp);
          }
          if (latest != op) {
            remove_use(op, inst);
            inst->u.inst.ops[i] = latest;
            add_use(latest, inst, i);  // 建立 SSA 下的 Def-Use 链
          }
        }
      }
    }

    if (opcode == OP_PHI) {
      int v_id = inst->u.inst.rk;
      push_value(ctx, v_id, inst);
      pushed_vids[++push_count] = v_id;
    } else if (opcode == OP_ASSIGN) {
      Value* dest = inst->u.inst.ops[0];
      if (dest != NULL && dest->vk == VK_VAR && dest->tp &&
          dest->tp->kind == TYPE_BASIC) {
        int v_id = dest->id;
        Value* src = inst->u.inst.ops[1];  // src 已经被重命名了
        push_value(ctx, v_id, src);
        pushed_vids[++push_count] = v_id;
        // 之后事实上就不需要这个赋值了（直接视为src对应的t_xx）
        // 但在这里不消除原始的v_，留到DCE的时候消除
      }
    } else if (opcode == OP_PARAM) {
      Value* dest = inst->u.inst.ops[0];
      if (dest != NULL && dest->vk == VK_VAR && dest->tp &&
          dest->tp->kind == TYPE_BASIC) {
        int v_id = dest->id;
        push_value(ctx, v_id, dest);
        pushed_vids[++push_count] = v_id;
      }
    }

    inst = nxt_inst;
  }
  // 填充phi节点
  for (int i = 0; i < cur_bb->u.bb.num_succs; ++i) {
    Value* succ_bb = cur_bb->u.bb.succs[i];
    Value* p = succ_bb->u.bb.inst_head;
    while (p != NULL && p->u.inst.opcode == OP_PHI) {
      int v_id = p->u.inst.rk;
      Value* latest = top_value(ctx, v_id);
      if (latest == NULL) {
        latest = get_zero_value(p->tp);
      }
      p->u.inst.ops = (Value**)realloc(
          p->u.inst.ops, sizeof(Value*) * (p->u.inst.num_ops + 2));
      p->u.inst.ops[p->u.inst.num_ops] = latest;
      p->u.inst.ops[p->u.inst.num_ops + 1] = cur_bb;
      add_use(latest, p, p->u.inst.num_ops);
      p->u.inst.num_ops += 2;
      p = p->u.inst.nxt;
    }
  }
  // 递归
  for (int i = 0; i < cur_bb->u.bb.num_idom_kids; ++i) {
    rename_dfs(ctx, cur_bb->u.bb.idom_kids[i]);
  }
  // 出栈
  for (int i = 1; i <= push_count; ++i) {
    pop_value(ctx, pushed_vids[i]);
  }
}

void rename_variables(Value* func) {
  if (func == NULL || func->vk != VK_FUNCTION) return;

  RenameContext ctx;
  memset(ctx.stack_head, 0, sizeof(ctx.stack_head));

  rename_dfs(&ctx, func->u.func.bb_head);
}