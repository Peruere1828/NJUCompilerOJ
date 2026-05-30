/**
 * reg_alloc.c — Graph-Colouring Register Allocator (Briggs-style)
 *
 * Algorithm overview:
 *   1. Build a dense index of all live values in the function.
 *   2. Compute liveness (gen/kill → live_in/live_out) per basic block.
 *   3. Build the interference graph: two values interfere if they are
 *      simultaneously live at any program point.
 *   4. Simplify: repeatedly push nodes with degree < K onto a stack.
 *   5. Spill: when all remaining nodes have degree >= K, pick one and
 *      mark it for spilling (remove from graph temporarily).
 *   6. Select: pop nodes from the stack, assign the first available colour.
 *   7. Build the result struct.
 */

#include "reg_alloc.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "IRbuilder.h"   /* MAX_ID */

/* ================================================================== */
/*  LiveSet — bit vector for up to 512 values                          */
/* ================================================================== */
#define LIVE_WORDS 8
#define LIVE_BITS  (LIVE_WORDS * 64)

typedef struct {
    unsigned long long w[LIVE_WORDS];
} LiveSet;

static inline void ls_clear(LiveSet* s)   { memset(s->w, 0, sizeof(s->w)); }
static inline bool  ls_test (LiveSet* s, int i) { return (s->w[i>>6] >> (i&63)) & 1; }
static inline void  ls_set  (LiveSet* s, int i) { s->w[i>>6] |= (1ULL << (i&63)); }
static inline void  ls_clr  (LiveSet* s, int i) { s->w[i>>6] &= ~(1ULL << (i&63)); }
static inline void  ls_copy (LiveSet* d, LiveSet* s) { memcpy(d->w, s->w, sizeof(s->w)); }
static bool         ls_eq   (LiveSet* a, LiveSet* b) { return memcmp(a->w, b->w, sizeof(a->w)) == 0; }
static inline void  ls_union(LiveSet* d, LiveSet* a, LiveSet* b) {
    for (int i = 0; i < LIVE_WORDS; i++) d->w[i] = a->w[i] | b->w[i];
}
static inline void  ls_diff (LiveSet* d, LiveSet* a, LiveSet* b) {
    for (int i = 0; i < LIVE_WORDS; i++) d->w[i] = a->w[i] & ~b->w[i];
}
static int ls_popcount(LiveSet* s) {
    int c = 0;
    for (int i = 0; i < LIVE_WORDS; i++) c += __builtin_popcountll(s->w[i]);
    return c;
}

/* ================================================================== */
/*  Value indexing — maps global Value ids to dense local indices      */
/* ================================================================== */
#define MAX_VALS  512

typedef struct {
    int  count;
    int  global_to_local[MAX_ID + 1];   /* global Value->id → local idx, -1 = not tracked */
    Value* local_to_value[MAX_VALS];
} ValueIndex;

static void vx_init(ValueIndex* vx) {
    memset(vx->global_to_local, -1, sizeof(vx->global_to_local));
    vx->count = 0;
}

static int vx_add(ValueIndex* vx, Value* val) {
    int gid = val->id;
    if (vx->global_to_local[gid] >= 0)
        return vx->global_to_local[gid];  /* already tracked */
    int lid = vx->count;
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
/*  Liveness analysis                                                  */
/* ================================================================== */

typedef struct {
    Value*    func;
    ValueIndex vx;
    int        n_vals;

    /* Flat BB array and count */
    Value**   bbs;
    int       n_bb;

    /* Per-BB liveness sets */
    LiveSet*  gen;
    LiveSet*  kill;
    LiveSet*  live_in;
    LiveSet*  live_out;

    /* Interference graph */
    int       degree[MAX_VALS];
    bool      adj[MAX_VALS][MAX_VALS];  /* dense adjacency for K=16, N≤512 */

    /* Spill cost heuristic */
    int       use_count[MAX_VALS];
    int       def_count[MAX_VALS];
    int       loop_depth[MAX_VALS];

    /* Colouring */
    int       colour[MAX_VALS];        /* -1 = uncoloured, -2 = spilled */
    int       simplify_stack[MAX_VALS];
    int       simplify_top;
} RAState;

/* ---- Flat BB array ---- */
static int count_bbs(Value* func) {
    int n = 0;
    Value* bb = func->u.func.bb_head;
    while (bb) { n++; bb = bb->u.bb.next_bb; }
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

/* ---- Collect all live values (VK_VAR + result-producing VK_INST) ---- */
static void collect_values(RAState* rs) {
    vx_init(&rs->vx);

    for (int bi = 0; bi < rs->n_bb; bi++) {
        Value* bb = rs->bbs[bi];
        Value* inst = bb->u.bb.inst_head;
        while (inst) {
            /* Track all VK_VAR operands */
            for (int i = 0; i < inst->u.inst.num_ops; i++) {
                Value* op = inst->u.inst.ops[i];
                if (op && op->vk == VK_VAR)
                    vx_add(&rs->vx, op);
            }
            /* Track VK_INST that produce a result (have an id and
               could be used by other instructions) */
            Opcode op = inst->u.inst.opcode;
            if ((op >= OP_I_ADD && op <= OP_I_DIV)
                || op == OP_GET_ADDR || op == OP_LOAD
                || op == OP_CALL || op == OP_READ
                || op == OP_ASSIGN) {
                vx_add(&rs->vx, inst);
            }
            inst = inst->u.inst.nxt;
        }
    }
    rs->n_vals = rs->vx.count;
}

/* ---- Instruction is a definition of a value ---- */
static int inst_def(RAState* rs, Value* inst) {
    Opcode op = inst->u.inst.opcode;
    /* These opcodes produce a new value */
    if ((op >= OP_I_ADD && op <= OP_I_DIV)
        || op == OP_GET_ADDR || op == OP_LOAD
        || op == OP_CALL || op == OP_READ) {
        return vx_lookup(&rs->vx, inst);
    }
    /* OP_ASSIGN defines ops[0] (the destination) */
    if (op == OP_ASSIGN) {
        Value* dest = inst->u.inst.ops[0];
        return vx_lookup(&rs->vx, dest);
    }
    return -1;
}

/* ---- Instruction uses which values (writes them to defs array) ---- */
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

/* ---- Compute gen/kill per basic block ---- */
static void compute_gen_kill(RAState* rs) {
    int nv = rs->n_vals;
    int nb = rs->n_bb;

    rs->gen  = (LiveSet*)calloc(nb, sizeof(LiveSet));
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
                if (!ls_test(&rs->kill[bi], uses[u]))
                    ls_set(&rs->gen[bi], uses[u]);
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

/* ---- Edge detection for loop depth estimation ---- */
static int count_back_edges(Value* bb) {
    /* Simple heuristic: count incoming edges as a proxy for loop depth.
       A node in a loop body has its loop header as a predecessor. */
    return bb->u.bb.num_preds - 1;
}

/* ---- Iterative liveness analysis ---- */
static void compute_liveness(RAState* rs) {
    int nb = rs->n_bb;

    rs->live_in  = (LiveSet*)calloc(nb, sizeof(LiveSet));
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
                if (depth > rs->loop_depth[d])
                    rs->loop_depth[d] = depth;
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
/*  Interference graph                                                 */
/* ================================================================== */

static void build_interference(RAState* rs) {
    int nv = rs->n_vals;
    memset(rs->degree, 0, sizeof(rs->degree));
    memset(rs->adj,   0, sizeof(rs->adj));

    /* Two values interfere if they are simultaneously live at the
       exit of any basic block.  A live range is the set of BBs
       where the value is live.  We use the conservative approximation:
       if both x and y are in live_out of some BB, they interfere. */
    for (int bi = 0; bi < rs->n_bb; bi++) {
        int* live = NULL;
        int count = 0;
        int live_buf[MAX_VALS];
        live = live_buf;

        for (int v = 0; v < nv; v++) {
            if (ls_test(&rs->live_out[bi], v) || ls_test(&rs->live_in[bi], v)) {
                live[count++] = v;
            }
        }

        for (int i = 0; i < count; i++) {
            for (int j = i + 1; j < count; j++) {
                int a = live[i], b = live[j];
                if (!rs->adj[a][b]) {
                    rs->adj[a][b] = true;
                    rs->adj[b][a] = true;
                    rs->degree[a]++;
                    rs->degree[b]++;
                }
            }
        }
    }
}

/* ================================================================== */
/*  Graph colouring (Briggs optimistic)                                 */
/* ================================================================== */
#define K  NUM_ALLOC_REGS

static int spill_cost(RAState* rs, int v) {
    return (rs->use_count[v] + rs->def_count[v])
           * (rs->loop_depth[v] * 10 + 1);
}

static void simplify(RAState* rs) {
    int nv = rs->n_vals;
    rs->simplify_top = 0;
    bool removed[MAX_VALS];
    memset(removed, 0, sizeof(removed));

    for (int i = 0; i < nv; i++) {
        /* Find a node with degree < K */
        int best = -1;
        for (int v = 0; v < nv; v++) {
            if (!removed[v] && rs->colour[v] >= 0
                && rs->degree[v] < K) {
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
                if (!removed[u] && rs->adj[best][u])
                    rs->degree[u]--;
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
                    if (!removed[u] && rs->adj[spill][u])
                        rs->degree[u]--;
                }
            } else {
                break;  /* all nodes processed */
            }
        }
    }
}

static void select_colours(RAState* rs) {
    int nv = rs->n_vals;

    /* Restore adjacency for all non-spilled nodes */
    bool active[MAX_VALS];
    memset(active, 0, sizeof(active));
    for (int i = 0; i < nv; i++) {
        if (rs->colour[i] >= 0)
            active[i] = true;
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
            if (active[u] && rs->adj[v][u] && rs->colour[u] >= 0)
                used[rs->colour[u]] = true;
        }

        /* Assign first free colour */
        int c = 0;
        while (c < K && used[c]) c++;

        if (c < K) {
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
/*  Build result                                                       */
/* ================================================================== */

static RegAllocResult* build_result(RAState* rs) {
    RegAllocResult* r = (RegAllocResult*)malloc(sizeof(RegAllocResult));
    int nv = rs->n_vals;

    r->n_vals        = nv;
    r->value_id      = (int*) malloc(sizeof(int)  * nv);
    r->phys_reg      = (int*) malloc(sizeof(int)  * nv);
    r->spilled       = (char*)malloc(sizeof(char) * nv);
    r->stack_offset  = (int*) malloc(sizeof(int)  * nv);
    r->callee_map    = 0;

    int spill_off = 0;
    for (int i = 0; i < nv; i++) {
        r->value_id[i] = rs->vx.local_to_value[i]->id;
        if (rs->colour[i] >= 0) {
            r->phys_reg[i] = rs->colour[i];
            r->spilled[i]  = 0;
            r->stack_offset[i] = 0;
            /* Track used callee-saved regs */
            if (rs->colour[i] >= REG_CALLEE_BASE)
                r->callee_map |= 1 << (rs->colour[i] - REG_CALLEE_BASE);
        } else {
            r->phys_reg[i] = -1;
            r->spilled[i]  = 1;
            r->stack_offset[i] = spill_off;
            spill_off += 4;
        }
    }

    r->spill_area_size = spill_off;
    return r;
}

/* ================================================================== */
/*  Public interface                                                    */
/* ================================================================== */

RegAllocResult* allocate_registers(Value* func) {
    RAState rs;
    memset(&rs, 0, sizeof(rs));

    /* Step 1: flatten BBs */
    rs.func = func;
    rs.bbs  = flatten_bbs(func, &rs.n_bb);
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

    /* Initialise colours to "available for allocation" (-2 = unprocessed) */
    for (int i = 0; i < rs.n_vals; i++)
        rs.colour[i] = -2;

    /* Step 3: liveness analysis */
    compute_gen_kill(&rs);
    compute_liveness(&rs);

    /* Step 4: interference graph */
    build_interference(&rs);

    /* Step 5: colour */
    /* Reset colours to "allocatable" */
    for (int i = 0; i < rs.n_vals; i++)
        rs.colour[i] = 0;
    simplify(&rs);
    select_colours(&rs);

    /* Step 6: build result */
    RegAllocResult* result = build_result(&rs);

    /* Cleanup */
    free(rs.bbs);
    free(rs.gen);
    free(rs.kill);
    free(rs.live_in);
    free(rs.live_out);

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