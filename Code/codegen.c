/**
 * codegen.c — MIPS32 汇编代码生成器
 *
 * ============================================================================
 * 两种模式
 * ============================================================================
 *
 * Phase 1 — 基于栈 (stack-based)：
 *   所有变量和临时值都存储在栈帧中。运算时从栈中 load 到 $t8/$t9 暂存器，
 *   计算后 store 回栈。每条指令独立使用 $t8/$t9——不需要寄存器分配。
 *   优点：简单直接，易于调试。
 *   缺点：生成代码有大量冗余的 lw/sw 指令。
 *
 * Phase 2 — 图着色 (graph-colouring)：
 *   通过 reg_alloc.c 的 Briggs 图着色算法将 IR 值映射到物理寄存器。
 *   只有溢出的值才存储在栈上。$t8/$t9 仍然作为指令选择的暂存器。
 *   优点：显著减少内存访问指令。
 *   缺点：需要序言/尾声保存 callee-saved 寄存器。
 *
 * ============================================================================
 * 栈帧布局 (MIPS 标准约定)
 * ============================================================================
 *
 *   高地址
 *   ┌──────────────────────────────┐  ← $sp (进入函数前)
 *   │ caller's stack frame         │
 *   │   ...                        │
 *   │   arg 5+ for callee          │  (通过 0($sp), 4($sp)... 传递)
 *   ├──────────────────────────────┤  ← $sp (进入函数后) / $fp
 *   │ saved $ra (如果 has_calls)   │  $fp + ra_off
 *   │ saved $fp                     │  $fp + fp_off
 *   │ saved $sX (phase 2, 可选的)   │  按需，在 body 顶部
 *   │   ... locals ...              │
 *   │ spilled values (phase 2)      │  溢出区
 *   │ local vars / temps            │
 *   │ arg build area (16 bytes)     │  $fp + 0  (为 callee 预留)
 *   │                               │
 *   低地址
 *
 *   帧大小 = 控制区(8 或 12 字节, 8对齐) + body(8对齐)
 *   body 从 $fp+0 开始，包含了:
 *     - arg_area (16 bytes, 仅 has_calls)
 *     - 局部变量 (var_slot)
 *     - 临时值 (temp_slot, phase 1 only)
 *     - 溢出区 (phase 2)
 *     - 保存的 $s 寄存器区 (phase 2)
 *
 * ============================================================================
 * 序言 (Prologue)
 * ============================================================================
 *   addiu $sp, $sp, -FRAME_SIZE    # 分配栈帧
 *   sw    $fp, FRAME_SIZE-8($sp)   # 保存调用者的 $fp
 *   sw    $ra, FRAME_SIZE-4($sp)   # 保存返回地址（仅当 has_calls）
 *   move  $fp, $sp                  # 建立新帧指针
 *   sw    $sX, ...                  # 保存 callee-saved 寄存器 (phase 2)
 *   # 复制参数到局部变量槽
 *
 * ============================================================================
 * 尾声 (Epilogue)
 * ============================================================================
 *   lw    $sX, ...                  # 恢复 callee-saved 寄存器 (phase 2)
 *   move  $sp, $fp                  # 恢复栈指针
 *   lw    $fp, FRAME_SIZE-8($sp)   # 恢复调用者的 $fp
 *   lw    $ra, FRAME_SIZE-4($sp)   # 恢复返回地址
 *   addiu $sp, $sp, FRAME_SIZE     # 释放栈帧
 *   jr    $ra                      # 返回
 *
 * ============================================================================
 * 调用约定
 * ============================================================================
 *   - 前 4 个整数参数在 $a0-$a3 中传递
 *   - 剩余参数在栈上传递（从 0($sp) 开始，右到左的 ARG 顺序）
 *   - 返回值在 $v0 中
 *   - $t8, $t9 是指令选择的暂存寄存器
 *   - 系统调用：1=print_int, 5=read_int, 10=exit, 11=print_char
 */

#include "codegen.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "AST.h" /* RelopKind */
#include "reg_alloc.h"
#include "translate.h"

/* ------------------------------------------------------------------ */
/*  CG — 单函数代码生成上下文                                          */
/*                                                                      */
/*  每个函数有一个独立的 CG 实例，包含该函数的全部状态。                */
/* ------------------------------------------------------------------ */
typedef struct {
  FILE* out;
  int phase;
  Value* func;
  char* func_name;

  /* ---- Stack frame ---- */
  bool has_calls;
  int frame_size;   /* total, 8-byte aligned */
  int ra_off;       /* offset of saved $ra from $fp */
  int fp_off;       /* offset of saved $fp from $fp */
  int next_var_off; /* next free slot offset from $fp (grows upward) */
  int arg_area;     /* outbound arg build area (min 16 if has_calls) */

  /* ---- Slot assignments ---- */
  int var_slot[0x4000];  /* var_id -> offset from $fp */
  int temp_slot[0x4000]; /* inst_id -> offset from $fp (phase 1 only) */

  /* ---- Argument tracking ---- */
  Value* arg_buf[64]; /* buffered ARG values (right-to-left order) */
  int arg_count;      /* total buffered ARGs */
  int arg_tos;        /* tos marker: args since last CALL start here */

  /* ---- Phase 2 ---- */
  int phys_reg[0x4000];           /* value id -> MIPS reg (0..K-1) or -1 */
  int spill_off[0x4000];          /* value id -> spill offset from $fp */
  unsigned char callee_saved_map; /* bit i set if $s_i is used */
  int num_saved_s;
  int saved_s[8]; /* list of $sX regs actually used */
} CG;

/* MIPS 物理寄存器编号（内部编码）。
   caller-saved ($t0-$t7): 0-7
   callee-saved ($s0-$s7): 8-15
   暂存器 ($t8,$t9): 16-17；返回值 ($v0,$v1): 18-19
   参数 ($a0-$a3): 20-23；特殊 ($ra,$fp,$sp): 24-26 */
enum {
  R_T0 = 0,
  R_T1,
  R_T2,
  R_T3,
  R_T4,
  R_T5,
  R_T6,
  R_T7,
  R_S0,
  R_S1,
  R_S2,
  R_S3,
  R_S4,
  R_S5,
  R_S6,
  R_S7,
  R_T8 = 16,
  R_T9 = 17,
  R_V0 = 18,
  R_V1,
  R_A0,
  R_A1,
  R_A2,
  R_A3,
  R_RA,
  R_FP,
  R_SP,
  R_ZERO = -1,
};

static const char* reg_name(int r) {
  static const char* names[] = {
      "$t0", "$t1", "$t2", "$t3", "$t4", "$t5", "$t6", "$t7", "$s0",
      "$s1", "$s2", "$s3", "$s4", "$s5", "$s6", "$s7", "$t8", "$t9",
      "$v0", "$v1", "$a0", "$a1", "$a2", "$a3", "$ra", "$fp", "$sp",
  };
  if (r >= 0 && r < (int)(sizeof(names) / sizeof(names[0]))) return names[r];
  return "??";
}

/* ================================================================== */
/*  辅助函数                                                            */
/* ================================================================== */

static bool inst_has_result(Value* inst) {
  /* Instructions whose result is referenced by other instructions. */
  switch (inst->u.inst.opcode) {
    case OP_I_ADD:
    case OP_I_SUB:
    case OP_I_MUL:
    case OP_I_DIV:
    case OP_F_ADD:
    case OP_F_SUB:
    case OP_F_MUL:
    case OP_F_DIV:
    case OP_GET_ADDR:
    case OP_LOAD:
    case OP_CALL:
    case OP_READ:
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
/*  Pass 1 — 栈帧布局分析 (Frame Layout Analysis)                      */
/*                                                                      */
/*  在生成任何汇编指令之前，先遍历整个函数的 IR 确定：                  */
/*    1. 是否包含函数调用 (has_calls) — 影响是否保存 $ra               */
/*    2. 每个变量的栈帧偏移 (var_slot)                                   */
/*    3. 每个临时值的栈帧偏移 (temp_slot, phase 1 only)                 */
/*    4. 最终帧大小 (frame_size) — 必须 8 字节对齐                      */
/*                                                                      */
/*  分析分为三个子步骤：                                                */
/*    a) 处理 DEC（变量声明）和 PARAM（函数参数）                       */
/*    b) 处理 OP_ASSIGN 的目标 VK_INST（SSA 销毁后的 phi 结果）        */
/*    c) Phase 1 专用：为所有产生结果的指令分配临时槽                   */
/*                                                                      */
/*  arg_area: 在 $fp+0 处保留 16 字节作为传出参数区（如果 has_calls）。  */
/*  这样当需要传递第 5 个及之后的 CALL 参数时，它们写入 0($sp)、         */
/*  4($sp) 等处，不会覆盖第一个局部变量。                               */
/* ================================================================== */

static void analyse_frame(CG* cg) {
  Value* func = cg->func;
  cg->has_calls = false;

  /* Initialise sentinels: -1 = not yet assigned */
  memset(cg->var_slot, -1, sizeof(cg->var_slot));
  memset(cg->temp_slot, -1, sizeof(cg->temp_slot));

  /* ---- Pre-scan: detect has_calls before laying out frame ---- */
  {
    Value* bb = func->u.func.bb_head;
    while (bb) {
      Value* inst = bb->u.bb.inst_head;
      while (inst) {
        if (inst->u.inst.opcode == OP_CALL) cg->has_calls = true;
        inst = inst->u.inst.nxt;
      }
      bb = bb->u.bb.next_bb;
    }
  }

  /* Outgoing arg build area: 16 bytes (4 slots) at $fp+0..$fp+15.
     Locals start above that so the 5th+ CALL arguments at 0($sp)
     don't clobber the first variable. */
  cg->arg_area = cg->has_calls ? 16 : 0;
  cg->next_var_off = cg->arg_area;

  /* ---- Sub-pass 1a: DEC and PARAM ---- */
  Value* bb = func->u.func.bb_head;
  while (bb) {
    Value* inst = bb->u.bb.inst_head;
    while (inst) {
      Opcode op = inst->u.inst.opcode;

      if (op == OP_DEC) {
        Value* var = inst->u.inst.ops[0];
        int size = inst->u.inst.ops[1]->u.int_val;
        assert(var->id >= 0 && var->id < 0x4000);
        cg->var_slot[var->id] = cg->next_var_off;
        cg->next_var_off += size;
      }

      if (op == OP_PARAM) {
        Value* var = inst->u.inst.ops[0];
        assert(var->id >= 0 && var->id < 0x4000);
        if (cg->var_slot[var->id] == -1) {
          cg->var_slot[var->id] = cg->next_var_off;
          cg->next_var_off += 4;
        }
      }

      inst = inst->u.inst.nxt;
    }
    bb = bb->u.bb.next_bb;
  }

  /* ---- Sub-pass 1b: Any remaining VK_VAR ops (from phi elimination etc.) ----
   */
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

  /* ---- Sub-pass 1b2: OP_ASSIGN destinations that are VK_INST (phi results)
   * ---- */
  bb = func->u.func.bb_head;
  while (bb) {
    Value* inst = bb->u.bb.inst_head;
    while (inst) {
      if (inst->u.inst.opcode == OP_ASSIGN) {
        Value* dest = inst->u.inst.ops[0];
        if (dest && dest->vk == VK_INST && cg->temp_slot[dest->id] == -1) {
          cg->temp_slot[dest->id] = cg->next_var_off;
          cg->next_var_off += 4;
        }
      }
      inst = inst->u.inst.nxt;
    }
    bb = bb->u.bb.next_bb;
  }

  /* ---- Sub-pass 1c: temp slots for every VK_INST that could be a result ----
   */
  if (cg->phase == 1) {
    bb = func->u.func.bb_head;
    while (bb) {
      Value* inst = bb->u.bb.inst_head;
      while (inst) {
        /* Assign a slot if this instruction produces a value that can be
           referenced by other instructions (i.e., it has a def-use chain).
           This includes arithmetic results AND VK_INST destinations of
           copy instructions created by SSA destruction. */
        if (inst->id > 0 &&
            (inst_has_result(inst) || inst->u.inst.opcode == OP_ASSIGN)) {
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

  /* body already includes the arg_area at the bottom ($fp+0).
     $fp+0..$fp+arg_area-1 = reserved for outgoing args 5+ */
  int body = align8(cg->next_var_off);
  cg->frame_size = control + body;
  assert(cg->frame_size < 1048576); /* 1 MB stack limit */

  cg->fp_off = cg->frame_size - 8;
  cg->ra_off = cg->frame_size - 4;
}

/* ================================================================== */
/*  Pass 2 — 基本操作：load / store IR 值                              */
/*                                                                      */
/*  load_val: 将 IR Value 加载到指定的 MIPS 物理寄存器                  */
/*    - VK_CONST_INT → li 指令（立即数加载）                            */
/*    - VK_VAR       → lw 从栈帧偏移加载                                */
/*    - VK_INST      → Phase 2: 从物理寄存器 move（如果已着色）         */
/*                      或从溢出区 lw（如果溢出）                        */
/*                      Phase 1: 从 temp_slot lw                       */
/*                                                                      */
/*  store_val: 将 MIPS 物理寄存器中的值存储回 IR 目标                   */
/*    - VK_VAR       → sw 到栈帧偏移                                    */
/*    - VK_INST      → Phase 2: move 到分配的物理寄存器（如果已着色）   */
/*                      或 sw 到溢出区（如果溢出）                       */
/*                      Phase 1: sw 到 temp_slot                       */
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
      assert(val->id >= 0 && val->id < 0x4000);
      int off = cg->var_slot[val->id];
      assert(off >= 0 && off < cg->frame_size);
      fprintf(cg->out, "\tlw %s, %d($fp)\n", reg_name(dst_reg), off);
      return;
    }
    case VK_INST: {
      if (cg->phase == 2 && cg->phys_reg[val->id] >= 0) {
        int r = cg->phys_reg[val->id];
        if (r != dst_reg)
          fprintf(cg->out, "\tmove %s, %s\n", reg_name(dst_reg), reg_name(r));
      } else {
        int off =
            (cg->phase == 2) ? cg->spill_off[val->id] : cg->temp_slot[val->id];
        if (off < 0) {
          fprintf(cg->out, "\t# BUG: inst t%d has no slot\n", val->id);
          off = 0;
        }
        fprintf(cg->out, "\tlw %s, %d($fp)\n", reg_name(dst_reg), off);
      }
      return;
    }
    default:
      fprintf(cg->out, "\t# ERROR: cannot load vk=%d id=%d\n", val->vk,
              val->id);
      return;
  }
}

/**
 * Emit code that stores a MIPS register value back to an IR destination.
 */
static void store_val(CG* cg, Value* dest, int src_reg) {
  switch (dest->vk) {
    case VK_VAR: {
      assert(dest->id >= 0 && dest->id < 0x4000);
      int off = cg->var_slot[dest->id];
      assert(off >= 0 && off < cg->frame_size);
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
      fprintf(cg->out, "\t# ERROR: cannot store to vk=%d id=%d\n", dest->vk,
              dest->id);
      return;
  }
}

/* ================================================================== */
/*  Pass 2 — 各指令的汇编发射器 (Per-instruction Emitters)              */
/*                                                                      */
/*  每条 IR 指令对应一个 emitter 函数，将 IR 翻译为 MIPS 汇编。          */
/*  算术指令使用 $t8/$t9 作为暂存器（不在可分配寄存器池中）。            */
/*                                                                      */
/*  特殊处理：                                                          */
/*    - emit_binary: I_DIV 使用 Python 风格的 floor 除法（向负无穷取整）  */
/*      MIPS 的 div 指令向零取整，需要额外处理符号不同的情况             */
/*    - emit_arg/emit_call: ARG 按右到左顺序缓冲，在 CALL 时逆序发射     */
/*      保证 $a0=第1个参数, $a1=第2个参数, ...                          */
/*    - emit_call: 用户函数名加 '_' 前缀避免与 MIPS 指令名冲突           */
/*      'main' 保持原名，因为 spim 需要它作为入口点                     */
/* ================================================================== */

static void emit_binary(CG* cg, Value* inst, const char* mnemonic) {
  load_val(cg, inst->u.inst.ops[0], R_T8);
  load_val(cg, inst->u.inst.ops[1], R_T9);
  if (strcmp(mnemonic, "div") == 0) {
    /* C-- 语义要求 floor 除法（向负无穷取整），而非 MIPS 默认的向零取整。
     *
     * 算法原理：
     *   MIPS div 指令向零截断，即 trunc(a/b)。
     *   我们需要 floor(a/b) = trunc(a/b) 当余数为0或结果非负时；
     *                     = trunc(a/b)-1 当余数≠0 且结果<0 时。
     *   等价条件：余数≠0 且被除数与除数异号 → floor = trunc - 1
     *
     * 仅使用 $t8/$t9（暂存器），不影响可分配寄存器池。 */
    static int div_label = 0;
    int lbl = div_label++;

    fprintf(cg->out, "\tdiv %s, %s\n", reg_name(R_T8), reg_name(R_T9));
    fprintf(cg->out, "\tmfhi %s\n", reg_name(R_T8)); /* clobber $t8 with HI */
    fprintf(cg->out, "\tbeq %s, $zero, .L_flr_%d\n", reg_name(R_T8), lbl);
    /* 延迟槽：重新加载被除数用于异或符号检查（beq 延迟槽始终执行） */
    load_val(cg, inst->u.inst.ops[0], R_T8);
    fprintf(cg->out, "\txor %s, %s, %s\n", reg_name(R_T8), reg_name(R_T8),
            reg_name(R_T9));
    fprintf(cg->out, "\tbgez %s, .L_flr_%d\n", reg_name(R_T8), lbl);
    fprintf(cg->out, "\tmflo %s\n", reg_name(R_T8));
    fprintf(cg->out, "\taddiu %s, %s, -1\n", reg_name(R_T8), reg_name(R_T8));
    fprintf(cg->out, "\tj .L_flr_ex_%d\n", lbl);
    fprintf(cg->out, ".L_flr_%d:\n", lbl);
    fprintf(cg->out, "\tmflo %s\n", reg_name(R_T8));
    fprintf(cg->out, ".L_flr_ex_%d:\n", lbl);
  } else {
    fprintf(cg->out, "\t%s %s, %s, %s\n", mnemonic, reg_name(R_T8),
            reg_name(R_T8), reg_name(R_T9));
  }
  store_val(cg, inst, R_T8);
}

static void emit_assign(CG* cg, Value* inst) {
  /* dest := src   ----   ops[0] = dest(VAR), ops[1] = src */
  Value* dest = inst->u.inst.ops[0];
  Value* src = inst->u.inst.ops[1];
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
  load_val(cg, inst->u.inst.ops[0], R_T8); /* $t8 = address */
  load_val(cg, inst->u.inst.ops[1], R_T9); /* $t9 = value */
  fprintf(cg->out, "\tsw %s, 0(%s)\n", reg_name(R_T9), reg_name(R_T8));
}

static void emit_goto(CG* cg, Value* inst) {
  Value* target = inst->u.inst.ops[0];
  fprintf(cg->out, "\tj %s\n", mips_label(cg->func_name, target->u.bb.bb_id));
}

static void emit_if_goto(CG* cg, Value* inst) {
  Value* lhs = inst->u.inst.ops[0];
  Value* rhs = inst->u.inst.ops[1];
  Value* target = inst->u.inst.ops[2];
  RelopKind rk = inst->u.inst.rk;
  const char* tlabel = mips_label(cg->func_name, target->u.bb.bb_id);

  load_val(cg, lhs, R_T8);
  load_val(cg, rhs, R_T9);

  switch (rk) {
    case RELOP_EQ:
      fprintf(cg->out, "\tbeq %s, %s, %s\n", reg_name(R_T8), reg_name(R_T9),
              tlabel);
      break;
    case RELOP_NEQ:
      fprintf(cg->out, "\tbne %s, %s, %s\n", reg_name(R_T8), reg_name(R_T9),
              tlabel);
      break;
    case RELOP_LT:
      fprintf(cg->out, "\tblt %s, %s, %s\n", reg_name(R_T8), reg_name(R_T9),
              tlabel);
      break;
    case RELOP_LEQ:
      fprintf(cg->out, "\tble %s, %s, %s\n", reg_name(R_T8), reg_name(R_T9),
              tlabel);
      break;
    case RELOP_GT:
      fprintf(cg->out, "\tbgt %s, %s, %s\n", reg_name(R_T8), reg_name(R_T9),
              tlabel);
      break;
    case RELOP_GEQ:
      fprintf(cg->out, "\tbge %s, %s, %s\n", reg_name(R_T8), reg_name(R_T9),
              tlabel);
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
  if (cg->arg_count < 64) cg->arg_buf[cg->arg_count++] = inst->u.inst.ops[0];
}

static void emit_call(CG* cg, Value* inst) {
  Value* func_val = inst->u.inst.ops[0]; /* VK_FUNCTION */

  /* Consume all ARGs pushed since the last CALL.  This correctly
     handles both sequential calls and nested calls by tracking
     the boundary between call sequences. */
  int n = cg->arg_count;

  for (int i = 0; i < n; i++) {
    int buf_idx = cg->arg_count - 1 - i;
    Value* arg = cg->arg_buf[buf_idx];
    load_val(cg, arg, R_T8);

    if (i < 4) {
      fprintf(cg->out, "\tmove %s, %s\n", reg_name(R_A0 + i), reg_name(R_T8));
    } else {
      int stk_off = (i - 4) * 4;
      fprintf(cg->out, "\tsw %s, %d($sp)\n", reg_name(R_T8), stk_off);
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

  /* Pop all consumed args */
  cg->arg_count = 0;
}

static void emit_read(CG* cg, Value* inst) {
  fprintf(cg->out, "\tli $v0, 5\n"); /* syscall 5 = read_int */
  fprintf(cg->out, "\tsyscall\n");
  store_val(cg, inst, R_V0);
}

static void emit_write(CG* cg, Value* inst) {
  Value* val = inst->u.inst.ops[0];
  load_val(cg, val, R_T8);
  fprintf(cg->out, "\tmove $a0, %s\n", reg_name(R_T8));
  fprintf(cg->out, "\tli $v0, 1\n"); /* syscall 1 = print_int */
  fprintf(cg->out, "\tsyscall\n");
  fprintf(cg->out, "\tli $a0, 10\n"); /* newline */
  fprintf(cg->out, "\tli $v0, 11\n"); /* syscall 11 = print_char */
  fprintf(cg->out, "\tsyscall\n");
}

/* ---- Opcode dispatch table ---- */
typedef void (*emitter_fn)(CG*, Value*);

static emitter_fn get_emitter(Opcode op) {
  switch (op) {
    case OP_ASSIGN:
      return emit_assign;
    case OP_GET_ADDR:
      return emit_get_addr;
    case OP_LOAD:
      return emit_load;
    case OP_STORE:
      return emit_store;
    case OP_GOTO:
      return emit_goto;
    case OP_IF_GOTO:
      return emit_if_goto;
    case OP_RETURN:
      return emit_return;
    case OP_ARG:
      return emit_arg;
    case OP_CALL:
      return emit_call;
    case OP_READ:
      return emit_read;
    case OP_WRITE:
      return emit_write;
    default:
      return NULL;
  }
}

static const char* binary_mnemonic(Opcode op) {
  switch (op) {
    case OP_I_ADD:
      return "addu";
    case OP_I_SUB:
      return "subu";
    case OP_I_MUL:
      return "mul";
    case OP_I_DIV:
      return "div";
    default:
      return "???";
  }
}

/* ================================================================== */
/*  序言 / 尾声 (Prologue / Epilogue)                                  */
/*                                                                      */
/*  序言负责：                                                          */
/*    1. 分配栈帧 (addiu $sp, $sp, -FRAME_SIZE)                         */
/*    2. 保存 $fp 和 $ra                                                */
/*    3. 保存 callee-saved $s 寄存器 (phase 2)                         */
/*    4. 将传入参数从 $a0-$a3 / caller's stack 复制到局部变量槽         */
/*                                                                      */
/*  尾声负责：                                                          */
/*    1. 恢复 callee-saved $s 寄存器 (phase 2)                         */
/*    2. 恢复 $fp 和 $ra                                               */
/*    3. 释放栈帧                                                       */
/*    4. 返回到调用者 (jr $ra)                                          */
/*                                                                      */
/*  注意：不可达的基本块（如被 IF_GOTO 跳过的 else 分支）也可能有        */
/*  j to epilogue 指令，所以尾声必须是单独的函数，不能内联到 return。    */
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
  if (cg->has_calls) fprintf(cg->out, "\tsw $ra, %d($sp)\n", cg->ra_off);
  fprintf(cg->out, "\tmove $fp, $sp\n");

  /* Phase 2: save callee-saved regs.
     The saved $s area is allocated at the top of the body (next_var_off
     already includes spill_area_size + num_saved_s*4).  Place them
     immediately above the spill area so they never overlap. */
  if (cg->phase == 2 && cg->num_saved_s > 0) {
    int base = cg->next_var_off - cg->num_saved_s * 4;
    for (int i = 0; i < cg->num_saved_s; i++) {
      fprintf(cg->out, "\tsw %s, %d($fp)\n", reg_name(cg->saved_s[i]),
              base + i * 4);
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
          fprintf(cg->out, "\tsw %s, %d($fp)\n", reg_name(R_A0 + p_idx), off);
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

  /* Restore callee-saved regs (phase 2) — must match prologue offsets */
  if (cg->phase == 2 && cg->num_saved_s > 0) {
    int base = cg->next_var_off - cg->num_saved_s * 4;
    for (int i = 0; i < cg->num_saved_s; i++) {
      fprintf(cg->out, "\tlw %s, %d($fp)\n", reg_name(cg->saved_s[i]),
              base + i * 4);
    }
  }

  /* Standard epilogue */
  fprintf(cg->out, "\tmove $sp, $fp\n");
  fprintf(cg->out, "\tlw $fp, %d($sp)\n", cg->fp_off);
  if (cg->has_calls) fprintf(cg->out, "\tlw $ra, %d($sp)\n", cg->ra_off);
  fprintf(cg->out, "\taddiu $sp, $sp, %d\n", cg->frame_size);
  fprintf(cg->out, "\tjr $ra\n");
  fprintf(cg->out, "\tnop\n");
}

/* ================================================================== */
/*  单函数代码生成流程                                                  */
/*                                                                      */
/*  1. 帧分析：确定需要多少栈空间                                       */
/*  2. Phase 2：运行图着色寄存器分配                                    */
/*     - 将 RA 结果填入 phys_reg/spill_off 表                           */
/*     - 扩展帧以容纳溢出区和保存的 $s 寄存器                          */
/*     - 重新计算帧大小                                                 */
/*  3. 发射序言                                                         */
/*  4. 遍历基本块，逐指令发射汇编                                       */
/*     - 跳过 DEC/PARAM/NOP/PHI/LABEL 等伪指令                         */
/*     - 二元运算由 emit_binary 处理                                    */
/*  5. 发射尾声                                                         */
/* ================================================================== */

static void codegen_function(CG* cg, Value* func) {
  cg->func = func;
  cg->func_name = func->u.func.name;
  cg->arg_count = 0;

  /* Initialise slot arrays */
  memset(cg->var_slot, 0, sizeof(cg->var_slot));
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
          cg->phys_reg[vid] = -1;
          cg->spill_off[vid] = cg->next_var_off + ra->stack_offset[i];
        } else {
          cg->phys_reg[vid] = ra->phys_reg[i];
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
      cg->frame_size = control + body;
      cg->fp_off = cg->frame_size - 8;
      cg->ra_off = cg->frame_size - 4;

      free_reg_alloc_result(ra);
    }
  }

  /* Pass 2 */
  emit_prologue(cg);

  Value* bb = func->u.func.bb_head;
  while (bb) {
    fprintf(cg->out, "%s:\n", mips_label(cg->func_name, bb->u.bb.bb_id));

    Value* inst = bb->u.bb.inst_head;
    cg->arg_count = 0;
    ;
    while (inst) {
      Opcode op = inst->u.inst.opcode;

      /* Skip no-op instructions at emission time */
      if (op == OP_DEC || op == OP_PARAM || op == OP_NOP || op == OP_PHI ||
          op == OP_LABEL) {
        inst = inst->u.inst.nxt;
        continue;
      }

      emitter_fn emit = get_emitter(op);
      if (emit)
        emit(cg, inst);
      else if (op == OP_I_ADD || op == OP_I_SUB || op == OP_I_MUL ||
               op == OP_I_DIV)
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
/*  公开入口点：对整个 IR 模块生成 MIPS 汇编                           */
/*                                                                      */
/*  遍历模块中的每个函数，为其独立执行代码生成。                        */
/*  Phase 1: 基于栈的朴素方案。Phase 2: 图着色寄存器分配。             */
/* ================================================================== */

void generate_mips(IRModule* module, FILE* out, int phase) {
  if (!module || !out) return;

  fprintf(out, "\t.data\n");
  fprintf(out, "\t.text\n");

  CG cg;
  memset(&cg, 0, sizeof(cg));
  cg.out = out;
  cg.phase = phase;

  Value* func = module->func_list;
  while (func) {
    codegen_function(&cg, func);
    func = func->u.func.next_func;
  }

  /* For stand-alone spim execution: add a tiny _start / exit wrapper? */
  /* Not needed — spim's built-in exception handler handles exit. */
}
