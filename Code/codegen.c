/**
 * codegen.c — MIPS32 Assembly Code Generator
 *
 * Phase 1: stack-based. Every virtual register and variable lives on the
 * stack frame.  All arithmetic loads operands into $t8/$t9, computes, and
 * stores the result back.  No register allocation is performed.
 *
 * Phase 2 (future): graph-colouring register allocation via reg_alloc.c.
 *
 * Stack-frame layout (standard MIPS convention):
 *
 *   HIGH ADDR   $fp + frame_size - 4  = saved $ra  (only if has_calls)
 *               $fp + frame_size - 8  = saved $fp
 *               $fp + frame_size - 8 - 4*k = saved $sX  (phase 2)
 *               ...
 *               $fp + N               = last local
 *               ...
 *   LOW ADDR    $fp + 0               = first local
 *
 * Prologue:
 *     addiu $sp, $sp, -FRAME_SIZE
 *     sw    $ra, FRAME_SIZE-4($sp)
 *     sw    $fp, FRAME_SIZE-8($sp)
 *     move  $fp, $sp
 *
 * Epilogue:
 *     move  $sp, $fp
 *     lw    $fp, FRAME_SIZE-8($sp)
 *     lw    $ra, FRAME_SIZE-4($sp)
 *     addiu $sp, $sp, FRAME_SIZE
 *     jr    $ra
 *
 * Calling convention:
 *   - First 4 integer args in $a0-$a3; rest on stack (right-to-left ARG order)
 *   - Return value in $v0
 *   - $t8, $t9 are scratch registers for instruction selection
 *   - Syscall 1 = print_int, 5 = read_int, 10 = exit, 11 = print_char
 */

#include "codegen.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "AST.h"        /* RelopKind */
#include "reg_alloc.h"
#include "translate.h"

/* ------------------------------------------------------------------ */
/*  Per-function code-generation context                               */
/* ------------------------------------------------------------------ */
typedef struct {
    FILE*  out;
    int    phase;
    Value* func;
    char*  func_name;

    /* ---- Stack frame ---- */
    bool   has_calls;
    int    frame_size;           /* total, 8-byte aligned */
    int    ra_off;               /* offset of saved $ra from $fp */
    int    fp_off;               /* offset of saved $fp from $fp */
    int    next_var_off;         /* next free slot offset from $fp (grows upward) */
    int    arg_area;             /* outbound arg build area (min 16 if has_calls) */

    /* ---- Slot assignments ---- */
    int    var_slot[0x4000];     /* var_id -> offset from $fp */
    int    temp_slot[0x4000];    /* inst_id -> offset from $fp (phase 1 only) */

    /* ---- Argument tracking ---- */
    Value* arg_buf[64];           /* buffered ARG values (right-to-left order) */
    int    arg_count;             /* number of ARGs in current call sequence */

    /* ---- Phase 2 ---- */
    int    phys_reg[0x4000];     /* value id -> MIPS reg (0..K-1) or -1 */
    int    spill_off[0x4000];    /* value id -> spill offset from $fp */
    unsigned char callee_saved_map; /* bit i set if $s_i is used */
    int    num_saved_s;
    int    saved_s[8];           /* list of $sX regs actually used */
} CG;

/* MIPS register numbers (internal encoding for phase 2 reg. alloc.) */
enum {
    R_T0 = 0,  R_T1,  R_T2,  R_T3,  R_T4,  R_T5,  R_T6,  R_T7,
    R_S0,  R_S1,  R_S2,  R_S3,  R_S4,  R_S5,  R_S6,  R_S7,
    R_T8 = 16, R_T9 = 17,
    R_V0 = 18, R_V1,
    R_A0, R_A1, R_A2, R_A3,
    R_RA, R_FP, R_SP,
    R_ZERO = -1,
};

static const char* reg_name(int r) {
    static const char* names[] = {
        "$t0","$t1","$t2","$t3","$t4","$t5","$t6","$t7",
        "$s0","$s1","$s2","$s3","$s4","$s5","$s6","$s7",
        "$t8","$t9","$v0","$v1","$a0","$a1","$a2","$a3",
        "$ra","$fp","$sp",
    };
    if (r >= 0 && r < (int)(sizeof(names)/sizeof(names[0]))) return names[r];
    return "??";
}

/* ================================================================== */
/*  Helpers                                                            */
/* ================================================================== */

static bool inst_has_result(Value* inst) {
    /* Instructions whose result is referenced by other instructions. */
    switch (inst->u.inst.opcode) {
    case OP_I_ADD: case OP_I_SUB: case OP_I_MUL: case OP_I_DIV:
    case OP_F_ADD: case OP_F_SUB: case OP_F_MUL: case OP_F_DIV:
    case OP_GET_ADDR: case OP_LOAD: case OP_CALL: case OP_READ:
        return true;
    default:
        return false;
    }
}

static const char* mips_label(const char* func_name, int bb_id) {
    static char buf[256];
    snprintf(buf, sizeof(buf), ".L_%s_%d", func_name, bb_id);
    return buf;
}

static const char* epilogue_label(const char* func_name) {
    static char buf[256];
    snprintf(buf, sizeof(buf), ".L_%s_epi", func_name);
    return buf;
}

static int align8(int n) { return (n + 7) & ~7; }

/* ================================================================== */
/*  Pass 1 — Frame layout analysis                                     */
/* ================================================================== */

static void analyse_frame(CG* cg) {
    Value* func = cg->func;
    cg->has_calls = false;
    cg->next_var_off = 0;

    /* Initialise sentinels: -1 = not yet assigned */
    memset(cg->var_slot,  -1, sizeof(cg->var_slot));
    memset(cg->temp_slot, -1, sizeof(cg->temp_slot));

    /* ---- Sub-pass 1a: DEC and PARAM ---- */
    Value* bb = func->u.func.bb_head;
    while (bb) {
        Value* inst = bb->u.bb.inst_head;
        while (inst) {
            Opcode op = inst->u.inst.opcode;

            if (op == OP_CALL)
                cg->has_calls = true;

            if (op == OP_DEC) {
                Value* var  = inst->u.inst.ops[0];
                int    size = inst->u.inst.ops[1]->u.int_val;
                cg->var_slot[var->id] = cg->next_var_off;
                cg->next_var_off += size;
            }

            if (op == OP_PARAM) {
                Value* var = inst->u.inst.ops[0];
                if (cg->var_slot[var->id] == -1) {
                    cg->var_slot[var->id] = cg->next_var_off;
                    cg->next_var_off += 4;
                }
            }

            inst = inst->u.inst.nxt;
        }
        bb = bb->u.bb.next_bb;
    }

    /* ---- Sub-pass 1b: Any remaining VK_VAR ops (from phi elimination etc.) ---- */
    bb = func->u.func.bb_head;
    while (bb) {
        Value* inst = bb->u.bb.inst_head;
        while (inst) {
            /* Check all operands for VK_VAR without a slot */
            for (int i = 0; i < inst->u.inst.num_ops; i++) {
                Value* op = inst->u.inst.ops[i];
                if (op && op->vk == VK_VAR && cg->var_slot[op->id] == -1) {
                    cg->var_slot[op->id] = cg->next_var_off;
                    cg->next_var_off += 4;
                }
            }
            inst = inst->u.inst.nxt;
        }
        bb = bb->u.bb.next_bb;
    }

    /* ---- Sub-pass 1c: temp slots for every VK_INST that could be a result ---- */
    if (cg->phase == 1) {
        bb = func->u.func.bb_head;
        while (bb) {
            Value* inst = bb->u.bb.inst_head;
            while (inst) {
                /* Assign a slot if this instruction produces a value that can be
                   referenced by other instructions (i.e., it has a def-use chain).
                   This includes arithmetic results AND VK_INST destinations of
                   copy instructions created by SSA destruction. */
                if (inst->id > 0 && (inst_has_result(inst)
                    || inst->u.inst.opcode == OP_ASSIGN)) {
                    cg->temp_slot[inst->id] = cg->next_var_off;
                    cg->next_var_off += 4;
                }
                inst = inst->u.inst.nxt;
            }
            bb = bb->u.bb.next_bb;
        }
    }

    /* ---- Compute final frame size ---- */
    int control = 8;
    if (cg->has_calls) control += 4;
    control = align8(control);

    int body = align8(cg->next_var_off);
    cg->arg_area = cg->has_calls ? 16 : 0;
    cg->frame_size = control + body + cg->arg_area;

    cg->fp_off = cg->frame_size - 8;
    cg->ra_off = cg->frame_size - 4;
}

/* ================================================================== */
/*  Pass 2 — Primitive: load / store IR values                         */
/* ================================================================== */

/**
 * Emit code that loads an IR Value into the given MIPS physical register.
 */
static void load_val(CG* cg, Value* val, int dst_reg) {
    switch (val->vk) {
    case VK_CONST_INT:
        fprintf(cg->out, "\tli %s, %d\n", reg_name(dst_reg), val->u.int_val);
        return;
    case VK_CONST_FLOAT:
        /* not supported per assumptions */
        fprintf(cg->out, "\t# FLOAT const unsupported\n");
        return;
    case VK_VAR: {
        int off = cg->var_slot[val->id];
        fprintf(cg->out, "\tlw %s, %d($fp)\n", reg_name(dst_reg), off);
        return;
    }
    case VK_INST: {
        if (cg->phase == 2 && cg->phys_reg[val->id] >= 0) {
            int r = cg->phys_reg[val->id];
            if (r != dst_reg)
                fprintf(cg->out, "\tmove %s, %s\n", reg_name(dst_reg), reg_name(r));
        } else {
            int off = (cg->phase == 2) ? cg->spill_off[val->id]
                                       : cg->temp_slot[val->id];
            if (off < 0) {
                fprintf(cg->out, "\t# BUG: inst t%d has no slot\n", val->id);
                off = 0;
            }
            fprintf(cg->out, "\tlw %s, %d($fp)\n", reg_name(dst_reg), off);
        }
        return;
    }
    default:
        fprintf(cg->out, "\t# ERROR: cannot load vk=%d id=%d\n", val->vk, val->id);
        return;
    }
}

/**
 * Emit code that stores a MIPS register value back to an IR destination.
 */
static void store_val(CG* cg, Value* dest, int src_reg) {
    switch (dest->vk) {
    case VK_VAR: {
        int off = cg->var_slot[dest->id];
        fprintf(cg->out, "\tsw %s, %d($fp)\n", reg_name(src_reg), off);
        return;
    }
    case VK_INST: {
        if (cg->phase == 2 && cg->phys_reg[dest->id] >= 0) {
            int r = cg->phys_reg[dest->id];
            if (r != src_reg)
                fprintf(cg->out, "\tmove %s, %s\n", reg_name(r), reg_name(src_reg));
        } else {
            int off = (cg->phase == 2) ? cg->spill_off[dest->id]
                                       : cg->temp_slot[dest->id];
            if (off < 0) {
                fprintf(cg->out, "\t# BUG: inst t%d has no slot\n", dest->id);
                off = 0;
            }
            fprintf(cg->out, "\tsw %s, %d($fp)\n", reg_name(src_reg), off);
        }
        return;
    }
    default:
        fprintf(cg->out, "\t# ERROR: cannot store to vk=%d id=%d\n",
                dest->vk, dest->id);
        return;
    }
}

/* ================================================================== */
/*  Pass 2 — Per-instruction emitters                                  */
/* ================================================================== */

static void emit_binary(CG* cg, Value* inst, const char* mnemonic) {
    load_val(cg, inst->u.inst.ops[0], R_T8);
    load_val(cg, inst->u.inst.ops[1], R_T9);
    if (strcmp(mnemonic, "div") == 0) {
        fprintf(cg->out, "\tdiv %s, %s\n", reg_name(R_T8), reg_name(R_T9));
        fprintf(cg->out, "\tmflo %s\n", reg_name(R_T8));
    } else {
        fprintf(cg->out, "\t%s %s, %s, %s\n",
                mnemonic, reg_name(R_T8), reg_name(R_T8), reg_name(R_T9));
    }
    store_val(cg, inst, R_T8);
}

static void emit_assign(CG* cg, Value* inst) {
    /* dest := src   ----   ops[0] = dest(VAR), ops[1] = src */
    Value* dest = inst->u.inst.ops[0];
    Value* src  = inst->u.inst.ops[1];
    load_val(cg, src, R_T8);
    store_val(cg, dest, R_T8);
}

static void emit_get_addr(CG* cg, Value* inst) {
    /* t := &var   ----   ops[0] = var(VAR) */
    Value* var = inst->u.inst.ops[0];
    int off = cg->var_slot[var->id];
    fprintf(cg->out, "\taddiu %s, $fp, %d\n", reg_name(R_T8), off);
    store_val(cg, inst, R_T8);
}

static void emit_load(CG* cg, Value* inst) {
    /* t := *addr  ----   ops[0] = addr */
    load_val(cg, inst->u.inst.ops[0], R_T8);
    fprintf(cg->out, "\tlw %s, 0(%s)\n", reg_name(R_T8), reg_name(R_T8));
    store_val(cg, inst, R_T8);
}

static void emit_store(CG* cg, Value* inst) {
    /* *addr := src  ----   ops[0] = addr, ops[1] = src */
    load_val(cg, inst->u.inst.ops[0], R_T8);  /* $t8 = address */
    load_val(cg, inst->u.inst.ops[1], R_T9);  /* $t9 = value */
    fprintf(cg->out, "\tsw %s, 0(%s)\n", reg_name(R_T9), reg_name(R_T8));
}

static void emit_goto(CG* cg, Value* inst) {
    Value* target = inst->u.inst.ops[0];
    fprintf(cg->out, "\tj %s\n",
            mips_label(cg->func_name, target->u.bb.bb_id));
}

static void emit_if_goto(CG* cg, Value* inst) {
    Value* lhs    = inst->u.inst.ops[0];
    Value* rhs    = inst->u.inst.ops[1];
    Value* target = inst->u.inst.ops[2];
    RelopKind rk  = inst->u.inst.rk;
    const char* tlabel = mips_label(cg->func_name, target->u.bb.bb_id);

    load_val(cg, lhs, R_T8);
    load_val(cg, rhs, R_T9);

    switch (rk) {
    case RELOP_EQ:
        fprintf(cg->out, "\tbeq %s, %s, %s\n",
                reg_name(R_T8), reg_name(R_T9), tlabel);
        break;
    case RELOP_NEQ:
        fprintf(cg->out, "\tbne %s, %s, %s\n",
                reg_name(R_T8), reg_name(R_T9), tlabel);
        break;
    case RELOP_LT:
        fprintf(cg->out, "\tblt %s, %s, %s\n",
                reg_name(R_T8), reg_name(R_T9), tlabel);
        break;
    case RELOP_LEQ:
        fprintf(cg->out, "\tble %s, %s, %s\n",
                reg_name(R_T8), reg_name(R_T9), tlabel);
        break;
    case RELOP_GT:
        fprintf(cg->out, "\tbgt %s, %s, %s\n",
                reg_name(R_T8), reg_name(R_T9), tlabel);
        break;
    case RELOP_GEQ:
        fprintf(cg->out, "\tbge %s, %s, %s\n",
                reg_name(R_T8), reg_name(R_T9), tlabel);
        break;
    default:
        break;
    }
}

static void emit_return(CG* cg, Value* inst) {
    if (inst->u.inst.num_ops > 0 && inst->u.inst.ops[0]) {
        load_val(cg, inst->u.inst.ops[0], R_V0);
    }
    fprintf(cg->out, "\tj %s\n", epilogue_label(cg->func_name));
}

static void emit_arg(CG* cg, Value* inst) {
    /* Buffer ARG values.  The IR emits them in right-to-left order
       (last parameter first).  We buffer here and emit in reversed
       order at emit_call so that $a0 = first param, $a1 = second, ... */
    if (cg->arg_count < 64)
        cg->arg_buf[cg->arg_count++] = inst->u.inst.ops[0];
}

static void emit_call(CG* cg, Value* inst) {
    Value* func_val = inst->u.inst.ops[0];  /* VK_FUNCTION */
    int n = cg->arg_count;

    /* Emit args in LEFT-to-RIGHT order ($a0 = first param, etc.).
       Since ARGs were buffered in right-to-left order, we iterate
       backwards through the buffer. */
    for (int i = 0; i < n; i++) {
        /* i=0 → last buffered arg → first param → $a0
           i=1 → second-to-last → second param → $a1 */
        int buf_idx = n - 1 - i;
        Value* arg = cg->arg_buf[buf_idx];
        load_val(cg, arg, R_T8);

        if (i < 4) {
            fprintf(cg->out, "\tmove %s, %s\n",
                    reg_name(R_A0 + i), reg_name(R_T8));
        } else {
            int stk_off = (i - 4) * 4;
            fprintf(cg->out, "\tsw %s, %d($sp)\n",
                    reg_name(R_T8), stk_off);
        }
    }

    /* Compute call target label, avoiding MIPS instruction name collisions */
    const char* fname = func_val->u.func.name;
    if (strcmp(fname, "main") == 0) {
        fprintf(cg->out, "\tjal %s\n", fname);
    } else {
        fprintf(cg->out, "\tjal _%s\n", fname);
    }
    fprintf(cg->out, "\tnop\n");

    if (inst_has_result(inst)) {
        store_val(cg, inst, R_V0);
    }

    cg->arg_count = 0;  /* reset for next call sequence */
}

static void emit_read(CG* cg, Value* inst) {
    fprintf(cg->out, "\tli $v0, 5\n");       /* syscall 5 = read_int */
    fprintf(cg->out, "\tsyscall\n");
    store_val(cg, inst, R_V0);
}

static void emit_write(CG* cg, Value* inst) {
    Value* val = inst->u.inst.ops[0];
    load_val(cg, val, R_T8);
    fprintf(cg->out, "\tmove $a0, %s\n", reg_name(R_T8));
    fprintf(cg->out, "\tli $v0, 1\n");       /* syscall 1 = print_int */
    fprintf(cg->out, "\tsyscall\n");
    fprintf(cg->out, "\tli $a0, 10\n");       /* newline */
    fprintf(cg->out, "\tli $v0, 11\n");       /* syscall 11 = print_char */
    fprintf(cg->out, "\tsyscall\n");
}

/* ---- Opcode dispatch table ---- */
typedef void (*emitter_fn)(CG*, Value*);

static emitter_fn get_emitter(Opcode op) {
    switch (op) {
    case OP_ASSIGN: return emit_assign;
    case OP_GET_ADDR: return emit_get_addr;
    case OP_LOAD: return emit_load;
    case OP_STORE: return emit_store;
    case OP_GOTO: return emit_goto;
    case OP_IF_GOTO: return emit_if_goto;
    case OP_RETURN: return emit_return;
    case OP_ARG: return emit_arg;
    case OP_CALL: return emit_call;
    case OP_READ: return emit_read;
    case OP_WRITE: return emit_write;
    default: return NULL;
    }
}

static const char* binary_mnemonic(Opcode op) {
    switch (op) {
    case OP_I_ADD: return "addu";
    case OP_I_SUB: return "subu";
    case OP_I_MUL: return "mul";
    case OP_I_DIV: return "div";
    default: return "???";
    }
}

/* ================================================================== */
/*  Prologue / Epilogue                                                */
/* ================================================================== */

static void emit_prologue(CG* cg) {
    fprintf(cg->out, "\n# --- %s ---\n", cg->func_name);
    /* Avoid MIPS instruction name collisions: prefix user functions with '_'
       except for 'main' which spim needs as the entry point. */
    const char* label = cg->func_name;
    static char safe_name[256];
    if (strcmp(cg->func_name, "main") == 0) {
        /* keep 'main' as-is */
    } else {
        snprintf(safe_name, sizeof(safe_name), "_%s", cg->func_name);
        label = safe_name;
    }
    fprintf(cg->out, "\t.globl %s\n", label);
    fprintf(cg->out, "%s:\n", label);

    /* Allocate frame */
    fprintf(cg->out, "\taddiu $sp, $sp, -%d\n", cg->frame_size);
    fprintf(cg->out, "\tsw $fp, %d($sp)\n", cg->fp_off);
    if (cg->has_calls)
        fprintf(cg->out, "\tsw $ra, %d($sp)\n", cg->ra_off);
    fprintf(cg->out, "\tmove $fp, $sp\n");

    /* Phase 2: save callee-saved regs */
    if (cg->phase == 2 && cg->num_saved_s > 0) {
        /* We'll compute offsets during phase 2 integration.
           For now, saved_s regs are saved after $fp and $ra. */
        int base = cg->fp_off - 8;  /* below saved $fp */
        for (int i = 0; i < cg->num_saved_s; i++) {
            fprintf(cg->out, "\tsw %s, %d($fp)\n",
                    reg_name(cg->saved_s[i]), base - i * 4);
        }
    }

    /* Copy incoming parameters from $a0-$a3 / caller's stack to local slots.
       The PARAM instructions in the entry BB tell us which vars are params.
       We find them by scanning the entry BB before codegen. */
    Value* entry_bb = cg->func->u.func.bb_head;
    if (entry_bb) {
        int p_idx = 0;
        Value* inst = entry_bb->u.bb.inst_head;
        while (inst) {
            if (inst->u.inst.opcode == OP_PARAM) {
                Value* var = inst->u.inst.ops[0];
                int off = cg->var_slot[var->id];
                if (p_idx < 4) {
                    fprintf(cg->out, "\tsw %s, %d($fp)\n",
                            reg_name(R_A0 + p_idx), off);
                } else {
                    /* parameter passed on caller's stack, above our frame */
                    int caller_off = cg->frame_size + (p_idx - 4) * 4;
                    fprintf(cg->out, "\tlw %s, %d($fp)\n", reg_name(R_T8), caller_off);
                    fprintf(cg->out, "\tsw %s, %d($fp)\n", reg_name(R_T8), off);
                }
                p_idx++;
            }
            inst = inst->u.inst.nxt;
        }
    }
}

static void emit_epilogue(CG* cg) {
    fprintf(cg->out, "%s:\n", epilogue_label(cg->func_name));

    /* Restore callee-saved regs (phase 2) */
    if (cg->phase == 2 && cg->num_saved_s > 0) {
        int base = cg->fp_off - 8;
        for (int i = 0; i < cg->num_saved_s; i++) {
            fprintf(cg->out, "\tlw %s, %d($fp)\n",
                    reg_name(cg->saved_s[i]), base - i * 4);
        }
    }

    /* Standard epilogue */
    fprintf(cg->out, "\tmove $sp, $fp\n");
    fprintf(cg->out, "\tlw $fp, %d($sp)\n", cg->fp_off);
    if (cg->has_calls)
        fprintf(cg->out, "\tlw $ra, %d($sp)\n", cg->ra_off);
    fprintf(cg->out, "\taddiu $sp, $sp, %d\n", cg->frame_size);
    fprintf(cg->out, "\tjr $ra\n");
    fprintf(cg->out, "\tnop\n");
}

/* ================================================================== */
/*  Per-function codegen                                               */
/* ================================================================== */

static void codegen_function(CG* cg, Value* func) {
    cg->func      = func;
    cg->func_name = func->u.func.name;
    cg->arg_count   = 0;

    /* Initialise slot arrays */
    memset(cg->var_slot,  0, sizeof(cg->var_slot));
    memset(cg->temp_slot, 0, sizeof(cg->temp_slot));

    /* Pass 1 */
    analyse_frame(cg);

    /* Phase 2: run register allocation before emission */
    if (cg->phase == 2) {
        RegAllocResult* ra = allocate_registers(func);
        if (ra) {
            /* Populate phys_reg and spill_off tables */
            for (int i = 0; i < ra->n_vals; i++) {
                int vid = ra->value_id[i];
                if (ra->spilled[i]) {
                    cg->phys_reg[vid]  = -1;
                    cg->spill_off[vid] = cg->next_var_off + ra->stack_offset[i];
                } else {
                    cg->phys_reg[vid]  = ra->phys_reg[i];
                    cg->spill_off[vid] = 0;
                }
            }
            /* Extend frame for spill area */
            cg->next_var_off += ra->spill_area_size;

            /* Track callee-saved registers */
            cg->callee_saved_map = ra->callee_map;
            cg->num_saved_s = 0;
            for (int s = 0; s < 8; s++) {
                if (ra->callee_map & (1 << s))
                    cg->saved_s[cg->num_saved_s++] = REG_CALLEE_BASE + s;
            }
            /* Extend frame for saved $s registers */
            cg->next_var_off += cg->num_saved_s * 4;

            /* Recalculate frame size */
            int control = 8;
            if (cg->has_calls) control += 4;
            control = align8(control);
            int body = align8(cg->next_var_off);
            cg->arg_area = cg->has_calls ? 16 : 0;
            cg->frame_size = control + body + cg->arg_area;
            cg->fp_off = cg->frame_size - 8;
            cg->ra_off = cg->frame_size - 4;

            free_reg_alloc_result(ra);
        }
    }

    /* Pass 2 */
    emit_prologue(cg);

    Value* bb = func->u.func.bb_head;
    while (bb) {
        fprintf(cg->out, "%s:\n",
                mips_label(cg->func_name, bb->u.bb.bb_id));

        Value* inst = bb->u.bb.inst_head;
        cg->arg_count = 0;
        while (inst) {
            Opcode op = inst->u.inst.opcode;

            /* Skip no-op instructions at emission time */
            if (op == OP_DEC || op == OP_PARAM || op == OP_NOP
                || op == OP_PHI || op == OP_LABEL) {
                inst = inst->u.inst.nxt;
                continue;
            }

            emitter_fn emit = get_emitter(op);
            if (emit)
                emit(cg, inst);
            else if (op == OP_I_ADD || op == OP_I_SUB
                     || op == OP_I_MUL || op == OP_I_DIV)
                emit_binary(cg, inst, binary_mnemonic(op));
            else
                fprintf(cg->out, "\t# TODO: opcode %d\n", op);

            inst = inst->u.inst.nxt;
        }

        bb = bb->u.bb.next_bb;
    }

    emit_epilogue(cg);
}

/* ================================================================== */
/*  Public entry point                                                 */
/* ================================================================== */

void generate_mips(IRModule* module, FILE* out, int phase) {
    if (!module || !out) return;

    fprintf(out, "\t.data\n");
    fprintf(out, "\t.text\n");

    CG cg;
    memset(&cg, 0, sizeof(cg));
    cg.out   = out;
    cg.phase = phase;

    Value* func = module->func_list;
    while (func) {
        codegen_function(&cg, func);
        func = func->u.func.next_func;
    }

    /* For stand-alone spim execution: add a tiny _start / exit wrapper? */
    /* Not needed — spim's built-in exception handler handles exit. */
}