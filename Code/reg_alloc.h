#ifndef REG_ALLOC_H
#define REG_ALLOC_H

#include "IR.h"

/**
 * Graph-colouring register allocator (Briggs-style) for MIPS32.
 *
 * Allocatable pool:
 *   $t0-$t7  (8 caller-saved registers, indices 0-7)
 *   $s0-$s7  (8 callee-saved registers, indices 8-15)
 *
 * Reserved (not in allocation pool):
 *   $t8, $t9 — codegen scratch
 *   $v0, $v1 — return values
 *   $a0-$a3  — argument passing
 *   $sp, $fp, $ra — stack / frame / return address
 */

#define NUM_ALLOC_REGS  16    /* $t0..$t7 + $s0..$s7 */
#define REG_CALLER_BASE  0    /* $t0 */
#define REG_CALLEE_BASE  8    /* $s0 */

/**
 * Result of register allocation for one function.
 */
typedef struct {
    int   n_vals;                /* number of values allocated */
    int*  value_id;              /* [local_idx] -> global Value->id */
    int*  phys_reg;              /* [local_idx] -> physical register index
                                    0..15 = $t0..$s7, -1 = spilled */
    char* spilled;               /* [local_idx] -> 1 if spilled */
    int*  stack_offset;          /* [local_idx] -> offset from $fp for spills */
    int   spill_area_size;       /* total spill area in bytes (4-aligned) */
    unsigned char callee_map;    /* bit i set if $s_i is assigned */
} RegAllocResult;

/**
 * Run graph-colouring register allocation on one function.
 * Returns a heap-allocated result; free with free_reg_alloc_result().
 */
RegAllocResult* allocate_registers(Value* func);

void free_reg_alloc_result(RegAllocResult* res);

#endif