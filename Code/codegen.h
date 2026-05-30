#ifndef CODEGEN_H
#define CODEGEN_H

#include "IR.h"
#include <stdio.h>

/**
 * Generate MIPS32 assembly for the entire IR module.
 * Must be called AFTER optimize_TAC() (no OP_PHI/OP_NOP in the IR).
 *
 * @param module  The IR module containing all functions
 * @param out     Output file stream for the .s assembly
 * @param phase   1 = stack-based, 2 = graph-coloring register allocation
 */
void generate_mips(IRModule* module, FILE* out, int phase);

#endif