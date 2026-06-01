/**
 * reg_alloc.c — 基于图着色的寄存器分配器 (Briggs 乐观着色算法)
 *
 * ============================================================================
 * 算法背景
 * ============================================================================
 * 寄存器分配是将无限多的虚拟寄存器（IR 中的临时变量）映射到有限物理寄存器
 * 的过程。这是一个 NP 完全问题，实践中使用图着色近似求解。
 *
 * Chaitin (1981) 首次提出将寄存器分配建模为图着色问题：
 *   - 节点 = 活跃期（live range）
 *   - 边   = 两个活跃期重叠（同时活跃），即它们不能共享同一寄存器
 *
 * Briggs 在 Chaitin 基础上的改进（"乐观着色"）：
 *   - Chaitin 的溢出策略是悲观的：一旦节点度数 < K，立即标记溢出
 *   - Briggs 延迟溢出决策到实际着色失败时才标记（select 阶段）
 *   - 优点：某些高度数节点在邻居被着色后仍可获得颜色
 *
 * ============================================================================
 * 算法流程（7 个步骤）
 * ============================================================================
 *
 *   Step 1: 值索引 (Value Indexing)
 *     - 遍历函数中所有 IR 指令，收集所有需要寄存器的活跃值
 *     - 建立全局 Value->id 到稠密局部索引的映射
 *     - 追踪的值包括：VK_VAR（变量）和 VK_INST（指令结果）
 *
 *   Step 2: 活跃性分析 (Liveness Analysis)
 *     - 经典迭代数据流分析：计算每个基本块的 gen/kill/live_in/live_out
 *       gen[B]   = 在 B 中被使用且之前未被定义的变量集合
 *       kill[B]  = 在 B 中被定义的变量集合
 *       迭代求解: live_out[B] = ∪_{S∈succ(B)} live_in[S]
 *                 live_in[B]  = gen[B] ∪ (live_out[B] - kill[B])
 *     - 同时统计 use_count/def_count 用于后续溢出代价估算
 *     - 估算循环深度（用回边数量近似）作为溢出代价的权重
 *
 *   Step 3: 构造冲突图 (Interference Graph)
 *     - 逐条指令模拟活跃集合的变化
 *     - 当一条指令定义变量 d 时，d 与当前活跃集合中的所有变量冲突
 *     - 同时为 live_out/live_in 的边界添加保守冲突（跨基本块）
 *     - 图以邻接矩阵 (adjacency matrix) 存储，相邻节点的度数双向递增
 *
 *   Step 4: 简化 (Simplify)
 *     - 反复寻找度数 < K 的节点（K = 可用寄存器数）
 *     - 将找到的节点压入栈中，从图中移除
 *     - 移除节点时递减所有邻居的度数
 *     - 此步骤基于图论中的 Kempe 定理：度数 < K 的节点总是可着色的
 *
 *   Step 5: 溢出 (Spill) — 仅在简化阶段找不到低度数节点时触发
 *     - 启发式选择溢出代价最小的节点：
 *       spill_cost(v) = (use_count + def_count) × (loop_depth × 10 + 1)
 *                        / (degree + 1)
 *     - 标记为溢出，从图中移除，继续简化
 *     - 注意：这里标记的是"可溢出"，真正是否需要溢出在 select 阶段决定
 *       （Briggs 乐观着色）
 *
 *   Step 6: 选择颜色 (Select)
 *     - 从栈顶到栈底（逆序）弹出节点
 *     - 对每个节点，检查所有已着色邻居的颜色
 *     - 分配第一个可用的颜色（在 [colour_base, colour_base+K) 范围内）
 *     - 如果找不到可用颜色 → 标记为溢出
 *     - 活跃节点按此方式逐步恢复
 *
 *   Step 7: 构建结果 (Build Result)
 *     - 将着色结果和溢出信息打包为 RegAllocResult
 *     - 记录哪些 callee-saved 寄存器被使用（供序言/尾声使用）
 *     - 计算溢出区域的总大小
 *
 * ============================================================================
 * MIPS32 寄存器分配策略
 * ============================================================================
 *   - 无函数调用时：使用 $t0-$t7 + $s0-$s7 = 16 个寄存器（K=16）
 *   - 有函数调用时：仅用 $s0-$s7 = 8 个寄存器（K=8）
 *     原因：$t0-$t7 是 caller-saved，跨调用会被破坏，必须避免
 *     简化：我们直接限制分配池而非在调用点保存/恢复 caller-saved 寄存器
 *   - 保留寄存器：$t8,$t9 (指令选择暂存), $v0,$v1 (返回值),
 *                 $a0-$a3 (参数传递), $sp,$fp,$ra (栈/帧/返回)
 */

#include "reg_alloc.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "IRbuilder.h" /* MAX_ID */

/* ================================================================== */
/*  LiveSet — 位向量 (bit vector)，支持最多 512 个值                       */
/*                                                                      */
/*  使用 16 个 64 位字 (unsigned long long) 实现，共 512 位。              */
/*  每个位对应一个局部索引的活跃状态：1=活跃，0=不活跃。                     */
/*  位向量操作（置位、清除、测试、并集、差集）全部内联，避免函数调用开销。   */
/*                                                                      */
/*  设计选择：位向量 而非 链表/哈希表，因为：                              */
/*    1. 操作 O(1) 时间（单条位运算指令）                                  */
/*    2. 迭代数据流分析中需要频繁计算并集、差集、相等比较                    */
/*    3. 512 位 = 128 字节，远小于 L1 缓存行                              */
/* ================================================================== */
#define LIVE_WORDS 16               /* 16 个 64 位字 */
#define LIVE_BITS (LIVE_WORDS * 64) /* 共 512 位 */

typedef struct {
  unsigned long long w[LIVE_WORDS];
} LiveSet;

static inline void ls_clear(LiveSet* s) { memset(s->w, 0, sizeof(s->w)); }
static inline bool ls_test(LiveSet* s, int i) {
  return (s->w[i >> 6] >> (i & 63)) & 1;
}
static inline void ls_set(LiveSet* s, int i) {
  s->w[i >> 6] |= (1ULL << (i & 63));
}
static inline void ls_clr(LiveSet* s, int i) {
  s->w[i >> 6] &= ~(1ULL << (i & 63));
}
static inline void ls_copy(LiveSet* d, LiveSet* s) {
  memcpy(d->w, s->w, sizeof(s->w));
}
static bool ls_eq(LiveSet* a, LiveSet* b) {
  return memcmp(a->w, b->w, sizeof(a->w)) == 0;
}
static inline void ls_union(LiveSet* d, LiveSet* a, LiveSet* b) {
  for (int i = 0; i < LIVE_WORDS; i++) d->w[i] = a->w[i] | b->w[i];
}
static inline void ls_diff(LiveSet* d, LiveSet* a, LiveSet* b) {
  for (int i = 0; i < LIVE_WORDS; i++) d->w[i] = a->w[i] & ~b->w[i];
}
static int ls_popcount(LiveSet* s) {
  int c = 0;
  for (int i = 0; i < LIVE_WORDS; i++) c += __builtin_popcountll(s->w[i]);
  return c;
}

/* ================================================================== */
/*  ValueIndex — 值索引映射                                             */
/*                                                                      */
/*  将全局的 Value->id（稀疏，可能很大）映射为局部的稠密索引 [0..count-1]。 */
/*  这样做的好处：                                                        */
/*    1. 内部数组大小 = 活跃值的数量（通常 < 100），而非 MAX_ID (4096+)     */
/*    2. 邻接矩阵大小为 n_vals²，稠密索引显著减少内存占用                   */
/*    3. 位向量的索引也使用局部索引                                       */
/*                                                                      */
/*  双向映射：                                                           */
/*    global_to_local[global_id] → local_id  (查表 O(1))                */
/*    local_to_value[local_id]   → Value*    (反向查 O(1))              */
/* ================================================================== */
#define MAX_VALS 1024

typedef struct {
  int count;
  int global_to_local[MAX_ID +
                      1]; /* global Value->id → local idx, -1 = not tracked */
  Value* local_to_value[MAX_VALS];
} ValueIndex;

static void vx_init(ValueIndex* vx) {
  memset(vx->global_to_local, -1, sizeof(vx->global_to_local));
  vx->count = 0;
}

static int vx_add(ValueIndex* vx, Value* val) {
  int gid = val->id;
  if (gid < 0 || gid > MAX_ID) return -1; /* safety: out of range */
  if (vx->global_to_local[gid] >= 0)
    return vx->global_to_local[gid]; /* already tracked */
  int lid = vx->count;
  if (lid >= MAX_VALS) return -1; /* too many values */
  vx->global_to_local[gid] = lid;
  vx->local_to_value[lid] = val;
  vx->count++;
  return lid;
}

static int vx_lookup(ValueIndex* vx, Value* val) {
  if (!val) return -1;
  return vx->global_to_local[val->id];
}

/* ================================================================== */
/*  RAState — 寄存器分配状态                                            */
/*                                                                      */
/*  保存整个寄存器分配过程的所有中间数据：                               */
/*    - vx: 值索引映射                                                  */
/*    - bbs[]: 基本块扁平数组                                           */
/*    - gen/kill/live_in/live_out: 每个基本块的活跃信息                  */
/*    - adj/degree: 冲突图的邻接矩阵和度数                               */
/*    - use_count/def_count/loop_depth: 溢出代价估算参数                 */
/*    - colour[]: 最终着色 (-2=未处理, -1=溢出, >=0=物理寄存器号)         */
/*    - simplify_stack: 简化阶段使用的栈                                 */
/* ==================================================================  */

typedef struct {
  Value* func;
  ValueIndex vx;
  int n_vals;

  /* Flat BB array and count */
  Value** bbs;
  int n_bb;

  /* Per-BB liveness sets */
  LiveSet* gen;
  LiveSet* kill;
  LiveSet* live_in;
  LiveSet* live_out;

  /* Interference graph */
  int degree[MAX_VALS];
  bool* adj; /* flat bool array: adj[a*nv + b] */

  /* Spill cost heuristic */
  int use_count[MAX_VALS];
  int def_count[MAX_VALS];
  int loop_depth[MAX_VALS];

  /* Colouring */
  int colour[MAX_VALS]; /* -1 = uncoloured, -2 = spilled */
  int simplify_stack[MAX_VALS];
  int simplify_top;
} RAState;

/* ---- 扁平化基本块链表为数组 ---- */
static int count_bbs(Value* func) {
  int n = 0;
  Value* bb = func->u.func.bb_head;
  while (bb) {
    n++;
    bb = bb->u.bb.next_bb;
  }
  return n;
}

static Value** flatten_bbs(Value* func, int* out_n) {
  int n = count_bbs(func);
  *out_n = n;
  Value** arr = (Value**)malloc(sizeof(Value*) * n);
  Value* bb = func->u.func.bb_head;
  for (int i = 0; i < n; i++) {
    arr[i] = bb;
    bb = bb->u.bb.next_bb;
  }
  return arr;
}

/* ---- 收集函数中所有活跃值（VK_VAR 和 VK_INST） ---- */
static void collect_values(RAState* rs) {
  vx_init(&rs->vx);

  for (int bi = 0; bi < rs->n_bb; bi++) {
    Value* bb = rs->bbs[bi];
    Value* inst = bb->u.bb.inst_head;
    while (inst) {
      /* Track all VAR and INST operands (after SSA destruction,
         variables are referenced as VK_INST, not VK_VAR). */
      for (int i = 0; i < inst->u.inst.num_ops; i++) {
        Value* op = inst->u.inst.ops[i];
        if (op && (op->vk == VK_VAR || op->vk == VK_INST)) vx_add(&rs->vx, op);
      }
      /* Track the instruction itself if it produces a result */
      Opcode op = inst->u.inst.opcode;
      if ((op >= OP_I_ADD && op <= OP_I_DIV) || op == OP_GET_ADDR ||
          op == OP_LOAD || op == OP_CALL || op == OP_READ || op == OP_ASSIGN) {
        vx_add(&rs->vx, inst);
      }
      inst = inst->u.inst.nxt;
    }
  }
  rs->n_vals = rs->vx.count;
}

/* ---- 判断指令是否定义 (def) 新值 ---- */
static int inst_def(RAState* rs, Value* inst) {
  Opcode op = inst->u.inst.opcode;
  /* These opcodes produce a new value */
  if ((op >= OP_I_ADD && op <= OP_I_DIV) || op == OP_GET_ADDR ||
      op == OP_LOAD || op == OP_CALL || op == OP_READ) {
    return vx_lookup(&rs->vx, inst);
  }
  /* OP_ASSIGN defines ops[0] (the destination) */
  if (op == OP_ASSIGN) {
    Value* dest = inst->u.inst.ops[0];
    return vx_lookup(&rs->vx, dest);
  }
  return -1;
}

/* ---- 提取指令使用的 (use) 所有值 ---- */
static int inst_uses(RAState* rs, Value* inst, int* uses, int max_uses) {
  int n = 0;
  Opcode op = inst->u.inst.opcode;

  /* OP_ASSIGN: ops[1] is the source */
  if (op == OP_ASSIGN) {
    if (n < max_uses) {
      int u = vx_lookup(&rs->vx, inst->u.inst.ops[1]);
      if (u >= 0) uses[n++] = u;
    }
  } else if (op == OP_STORE) {
    /* ops[0]=addr, ops[1]=src — both used */
    for (int i = 0; i < inst->u.inst.num_ops && n < max_uses; i++) {
      int u = vx_lookup(&rs->vx, inst->u.inst.ops[i]);
      if (u >= 0) uses[n++] = u;
    }
  } else {
    /* All other instructions: all operands are uses */
    for (int i = 0; i < inst->u.inst.num_ops && n < max_uses; i++) {
      Value* op = inst->u.inst.ops[i];
      /* Skip VK_BB operands (target labels) */
      if (op && op->vk != VK_BB) {
        int u = vx_lookup(&rs->vx, op);
        if (u >= 0) uses[n++] = u;
      }
    }
  }
  return n;
}

/* ================================================================== */
/*  活跃性分析 (Liveness Analysis)                                     */
/*                                                                      */
/*  使用迭代数据流分析计算每个基本块的 gen/kill/live_in/live_out。        */
/*                                                                      */
/*  公式回顾：                                                          */
/*    gen[B]    = { v | v 在 B 中被使用，且在此使用之前未被 B 中指令定义 } */
/*    kill[B]   = { v | v 在 B 中被定义 }                               */
/*    live_out[B] = ∪_{S∈succ(B)} live_in[S]                           */
/*    live_in[B]  = gen[B] ∪ (live_out[B] - kill[B])                   */
/*                                                                      */
/*  迭代直到不动点：逆序处理基本块以加速收敛。                            */
/*                                                                      */
/*  同时收集每个值的 use_count、def_count 和循环深度，                  */
/*  用于后续的溢出代价估算。                                            */
/* ================================================================== */
static void compute_gen_kill(RAState* rs) {
  int nv = rs->n_vals;
  int nb = rs->n_bb;

  rs->gen = (LiveSet*)calloc(nb, sizeof(LiveSet));
  rs->kill = (LiveSet*)calloc(nb, sizeof(LiveSet));

  for (int bi = 0; bi < nb; bi++) {
    Value* bb = rs->bbs[bi];
    Value* inst = bb->u.bb.inst_head;

    ls_clear(&rs->gen[bi]);
    ls_clear(&rs->kill[bi]);

    while (inst) {
      /* uses first, then defs */
      int uses[8];
      int nu = inst_uses(rs, inst, uses, 8);
      int d = inst_def(rs, inst);

      /* gen = gen ∪ (uses - kill) */
      for (int u = 0; u < nu; u++) {
        if (!ls_test(&rs->kill[bi], uses[u])) ls_set(&rs->gen[bi], uses[u]);
        rs->use_count[uses[u]]++;
      }

      /* kill = kill ∪ {def} */
      if (d >= 0) {
        ls_set(&rs->kill[bi], d);
        rs->def_count[d]++;
      }

      inst = inst->u.inst.nxt;
    }
  }
}

/* ---- 回边检测：估算循环深度（用于溢出代价权重） ---- */
static int count_back_edges(Value* bb) {
  /* Simple heuristic: count incoming edges as a proxy for loop depth.
     A node in a loop body has its loop header as a predecessor. */
  return bb->u.bb.num_preds - 1;
}

/* ---- 迭代数据流求解：计算 live_in / live_out ---- */
static void compute_liveness(RAState* rs) {
  int nb = rs->n_bb;

  rs->live_in = (LiveSet*)calloc(nb, sizeof(LiveSet));
  rs->live_out = (LiveSet*)calloc(nb, sizeof(LiveSet));

  /* Estimate loop depth for spill cost */
  memset(rs->loop_depth, 0, sizeof(rs->loop_depth));
  for (int bi = 0; bi < nb; bi++) {
    Value* bb = rs->bbs[bi];
    Value* inst = bb->u.bb.inst_head;
    while (inst) {
      int d = inst_def(rs, inst);
      if (d >= 0) {
        int depth = count_back_edges(bb);
        if (depth > rs->loop_depth[d]) rs->loop_depth[d] = depth;
      }
      inst = inst->u.inst.nxt;
    }
  }

  /* Iterative data-flow: live_out[B] = ∪_{S∈succ(B)} live_in[S]
                         live_in[B]  = gen[B] ∪ (live_out[B] - kill[B]) */
  bool changed = true;
  while (changed) {
    changed = false;
    /* Process in reverse order for faster convergence */
    for (int bi = nb - 1; bi >= 0; bi--) {
      Value* bb = rs->bbs[bi];
      LiveSet new_out;
      ls_clear(&new_out);

      /* new_out = ∪ of live_in of successors */
      for (int s = 0; s < bb->u.bb.num_succs; s++) {
        int si = -1;
        for (int j = 0; j < nb; j++) {
          if (rs->bbs[j] == bb->u.bb.succs[s]) {
            si = j;
            break;
          }
        }
        if (si >= 0) {
          for (int w = 0; w < LIVE_WORDS; w++)
            new_out.w[w] |= rs->live_in[si].w[w];
        }
      }

      if (!ls_eq(&new_out, &rs->live_out[bi])) {
        ls_copy(&rs->live_out[bi], &new_out);
        changed = true;
      }

      /* live_in = gen ∪ (live_out - kill) */
      LiveSet tmp;
      ls_diff(&tmp, &rs->live_out[bi], &rs->kill[bi]);
      ls_union(&rs->live_in[bi], &rs->gen[bi], &tmp);
    }
  }
}

/* ================================================================== */
/*  冲突图 (Interference Graph) 的构建                                  */
/*                                                                      */
/*  两个值相互冲突 (interfere) 当且仅当它们在程序的某个点同时活跃。        */
/*  冲突的变量不能分配同一个寄存器。                                     */
/*                                                                      */
/*  构建方法：                                                           */
/*    1. 对每个基本块，从 live_in 开始逐指令模拟活跃集合的变化            */
/*    2. 当遇到定义 d 时，d 与当前活跃集合中的所有值冲突                 */
/*    3. 先移除旧实例（如果存在），然后添加新定义                       */
/*    4. 在基本块边界，live_out 和 live_in 中的所有值全互连             */
/*       （保守处理跨基本块的活跃信息）                                   */
/*                                                                      */
/*  冲突图使用邻接矩阵 (adjacency matrix) 存储：                         */
/*    adj[a * n_vals + b] = true  ⇔  a 和 b 冲突                       */
/*  同时维护 degree[a] = a 的邻居数，用于简化阶段的度数判定。           */
/* ================================================================== */

static inline bool* adj_ptr(RAState* rs, int a, int b) {
  return &rs->adj[a * rs->n_vals + b];
}

static void add_interf(RAState* rs, int a, int b) {
  if (a < 0 || b < 0 || a == b) return;
  bool* p = adj_ptr(rs, a, b);
  if (!*p) {
    *p = true;
    *adj_ptr(rs, b, a) = true;
    rs->degree[a]++;
    rs->degree[b]++;
  }
}

static void build_interference(RAState* rs) {
  int nv = rs->n_vals;
  memset(rs->degree, 0, sizeof(rs->degree));
  memset(rs->adj, 0, nv * nv * sizeof(bool));

  /* Per-instruction liveness within each BB for fine-grained
     interference: a def interferes with all values live at that point. */
  for (int bi = 0; bi < rs->n_bb; bi++) {
    Value* bb = rs->bbs[bi];
    LiveSet cur;
    ls_copy(&cur, &rs->live_in[bi]);

    Value* inst = bb->u.bb.inst_head;
    while (inst) {
      int uses[8];
      int nu = inst_uses(rs, inst, uses, 8);
      int d = inst_def(rs, inst);

      /* Def interferes with all currently-live values */
      if (d >= 0) {
        for (int v = 0; v < nv; v++) {
          if (ls_test(&cur, v)) add_interf(rs, d, v);
        }
        /* Remove old instance from cur (if present) */
        ls_clr(&cur, d);
      }

      /* Uses become live */
      for (int u = 0; u < nu; u++) ls_set(&cur, uses[u]);

      /* Def becomes live after the instruction */
      if (d >= 0) ls_set(&cur, d);

      inst = inst->u.inst.nxt;
    }
  }

  /* Also conservatively add interferences for live_out ∩ live_in
     (inter-BB liveness at BB boundaries). */
  for (int bi = 0; bi < rs->n_bb; bi++) {
    int live_buf[MAX_VALS];
    int count = 0;
    for (int v = 0; v < nv; v++) {
      if (ls_test(&rs->live_out[bi], v) || ls_test(&rs->live_in[bi], v)) {
        live_buf[count++] = v;
      }
    }
    for (int i = 0; i < count; i++)
      for (int j = i + 1; j < count; j++)
        add_interf(rs, live_buf[i], live_buf[j]);
  }
}

/* ================================================================== */
/*  Briggs 乐观图着色算法                                               */
/*                                                                      */
/*  核心思想：                                                          */
/*    1. 度数 < K 的节点总是可着色的（Kempe 定理），直接压栈移除         */
/*    2. 度数 ≥ K 的节点"可能"需要溢出，但推迟决策到 select 阶段         */
/*    3. 在 select 阶段，如果邻居已用满了 K 种颜色，才真正溢出           */
/*                                                                      */
/*  这种"乐观"策略相比 Chaitin 的悲观溢出，能显著减少不必要的溢出。      */
/*                                                                      */
/*  K 的值：                                                             */
/*    - 无调用函数：K = 16（$t0-$t7 + $s0-$s7）                          */
/*    - 有调用函数：K = 8（仅 $s0-$s7，因为 $t 寄存器跨调用不保留）      */
/*                                                                      */
/*  colour_base：                                                        */
/*    无调用时从 0 开始（$t0），有调用时从 8 开始（$s0）。               */
/*    这样做的好处是：有调用时所有分配都在 $s 寄存器中，                  */
/*    无需在调用点保存/恢复 caller-saved 寄存器。                        */
/* ================================================================== */
#define K NUM_ALLOC_REGS

/* ---- 计算溢出代价 ---- */
static int spill_cost(RAState* rs, int v) {
  return (rs->use_count[v] + rs->def_count[v]) * (rs->loop_depth[v] * 10 + 1);
}

/* ---- 简化阶段：反复移除度数 < K 的节点 ---- */
static void simplify(RAState* rs, int K_eff) {
  int nv = rs->n_vals;
  rs->simplify_top = 0;
  bool removed[MAX_VALS];
  memset(removed, 0, sizeof(removed));

  for (int i = 0; i < nv; i++) {
    /* Find a node with degree < K_eff */
    int best = -1;
    for (int v = 0; v < nv; v++) {
      if (!removed[v] && rs->colour[v] >= 0 && rs->degree[v] < K_eff) {
        best = v;
        break;
      }
    }

    if (best >= 0) {
      /* Push it onto the stack and remove from graph */
      rs->simplify_stack[rs->simplify_top++] = best;
      removed[best] = true;

      /* Decrease degree of neighbours */
      for (int u = 0; u < nv; u++) {
        if (!removed[u] && *adj_ptr(rs, best, u)) rs->degree[u]--;
      }
    } else {
      /* No node with degree < K — need to spill */
      int spill = -1;
      int min_cost = 1 << 30;
      for (int v = 0; v < nv; v++) {
        if (!removed[v] && rs->colour[v] >= 0) {
          int cost = spill_cost(rs, v) / (rs->degree[v] + 1);
          if (cost < min_cost) {
            min_cost = cost;
            spill = v;
          }
        }
      }

      if (spill >= 0) {
        /* Mark as spilled and remove from graph */
        rs->colour[spill] = -1;
        rs->simplify_stack[rs->simplify_top++] = spill;
        removed[spill] = true;

        for (int u = 0; u < nv; u++) {
          if (!removed[u] && *adj_ptr(rs, spill, u)) rs->degree[u]--;
        }
      } else {
        break; /* all nodes processed */
      }
    }
  }
}

/* ---- 选择阶段：逆序弹出节点，分配第一个可用颜色 ---- */
static void select_colours(RAState* rs, int K_eff, int colour_base) {
  int nv = rs->n_vals;

  /* Restore adjacency for all non-spilled nodes */
  bool active[MAX_VALS];
  memset(active, 0, sizeof(active));
  for (int i = 0; i < nv; i++) {
    if (rs->colour[i] >= 0) active[i] = true;
  }

  /* Process stack in reverse (last pushed = first assigned) */
  for (int i = rs->simplify_top - 1; i >= 0; i--) {
    int v = rs->simplify_stack[i];

    if (rs->colour[v] < 0) {
      /* Already spilled */
      continue;
    }

    /* Find neighbours' colours */
    bool used[K];
    memset(used, 0, sizeof(used));
    for (int u = 0; u < nv; u++) {
      if (active[u] && *adj_ptr(rs, v, u) && rs->colour[u] >= 0)
        used[rs->colour[u]] = true;
    }

    /* Assign first free colour in [colour_base, colour_base+K_eff) */
    int c = colour_base;
    int limit = colour_base + K_eff;
    while (c < limit && used[c]) c++;

    if (c < limit) {
      rs->colour[v] = c;
      active[v] = true;
    } else {
      /* Could not colour — spill */
      rs->colour[v] = -1;
      active[v] = false;
    }
  }
}

/* ================================================================== */
/*  构建最终结果                                                        */
/*                                                                      */
/*  将着色信息打包为 RegAllocResult：                                   */
/*    - 每个值映射到物理寄存器号（0..15）或标记为溢出                     */
/*    - 溢出的值分配栈上偏移（spill_off）                               */
/*    - 记录哪些 callee-saved 寄存器被使用（callee_map）                */
/*                                                                      */
/*  callee_map 是位掩码：bit i 表示 $s_i 被分配了值。                    */
/*  序言/尾声据此保存和恢复对应的 $s 寄存器。                            */
/* ================================================================== */

static RegAllocResult* build_result(RAState* rs) {
  RegAllocResult* r = (RegAllocResult*)malloc(sizeof(RegAllocResult));
  int nv = rs->n_vals;

  r->n_vals = nv;
  r->value_id = (int*)malloc(sizeof(int) * nv);
  r->phys_reg = (int*)malloc(sizeof(int) * nv);
  r->spilled = (char*)malloc(sizeof(char) * nv);
  r->stack_offset = (int*)malloc(sizeof(int) * nv);
  r->callee_map = 0;

  int spill_off = 0;
  for (int i = 0; i < nv; i++) {
    r->value_id[i] = rs->vx.local_to_value[i]->id;
    if (rs->colour[i] >= 0) {
      r->phys_reg[i] = rs->colour[i];
      r->spilled[i] = 0;
      r->stack_offset[i] = 0;
      /* Track used callee-saved regs */
      if (rs->colour[i] >= REG_CALLEE_BASE)
        r->callee_map |= 1 << (rs->colour[i] - REG_CALLEE_BASE);
    } else {
      r->phys_reg[i] = -1;
      r->spilled[i] = 1;
      r->stack_offset[i] = spill_off;
      spill_off += 4;
    }
  }

  r->spill_area_size = spill_off;
  return r;
}

/* ================================================================== */
/*  公开接口：对一个函数执行图着色寄存器分配                            */
/*                                                                      */
/*  allocate_registers(func) 的执行流程：                               */
/*    Step 1: flatten_bbs   — 将 BB 链表转为数组                       */
/*    Step 2: collect_values — 收集所有活跃值                           */
/*    Step 3: compute_gen_kill + compute_liveness — 数据流分析          */
/*    Step 4: build_interference — 构造冲突图                          */
/*    Step 5: simplify + select_colours — 着色                         */
/*    Step 6: build_result — 打包结果                                  */
/*                                                                      */
/*  特殊处理：有函数调用的函数只使用 $s 寄存器（callee-saved），         */
/*  这样就不需要在每个调用点保存/恢复 $t 寄存器。                       */
/*  相应的 colour_base 和 K_eff 分别设为 8 和 8。                       */
/* ================================================================== */

RegAllocResult* allocate_registers(Value* func) {
  RAState rs;
  memset(&rs, 0, sizeof(rs));

  /* Step 1: flatten BBs */
  rs.func = func;
  rs.bbs = flatten_bbs(func, &rs.n_bb);
  if (rs.n_bb == 0) {
    free(rs.bbs);
    return NULL;
  }

  /* Step 2: collect values */
  collect_values(&rs);
  if (rs.n_vals == 0) {
    free(rs.bbs);
    return NULL;
  }

  /* Allocate adjacency matrix (n_vals × n_vals) */
  rs.adj = (bool*)calloc(rs.n_vals * rs.n_vals, sizeof(bool));
  if (!rs.adj) {
    free(rs.bbs);
    free(rs.gen);
    free(rs.kill);
    free(rs.live_in);
    free(rs.live_out);
    return NULL;
  }

  /* Initialise colours to "available for allocation" (-2 = unprocessed) */
  for (int i = 0; i < rs.n_vals; i++) rs.colour[i] = -2;

  /* Step 3: liveness analysis */
  compute_gen_kill(&rs);
  compute_liveness(&rs);

  /* Step 4: interference graph */
  build_interference(&rs);

  /* Step 5: colour */
  /* Reset colours to "allocatable" */
  for (int i = 0; i < rs.n_vals; i++) rs.colour[i] = 0;

  int colour_base = 0; /* caller-saved: $t0-$t7 */

  /* For functions with calls, only use callee-saved regs ($s0-$s7)
     since caller-saved regs would be clobbered across calls. */
  int K_eff = K;
  bool has_calls = false;
  for (int bi = 0; bi < rs.n_bb; bi++) {
    Value* inst = rs.bbs[bi]->u.bb.inst_head;
    while (inst) {
      if (inst->u.inst.opcode == OP_CALL) {
        has_calls = true;
        break;
      }
      inst = inst->u.inst.nxt;
    }
    if (has_calls) break;
  }
  if (has_calls) {
    K_eff = 8;
    colour_base = REG_CALLEE_BASE; /* $s0-$s7 */
  }

  simplify(&rs, K_eff);
  select_colours(&rs, K_eff, colour_base);

  /* Step 6: build result */
  RegAllocResult* result = build_result(&rs);

  /* Cleanup */
  free(rs.bbs);
  free(rs.gen);
  free(rs.kill);
  free(rs.live_in);
  free(rs.live_out);
  free(rs.adj);

  return result;
}

void free_reg_alloc_result(RegAllocResult* res) {
  if (!res) return;
  free(res->value_id);
  free(res->phys_reg);
  free(res->spilled);
  free(res->stack_offset);
  free(res);
}