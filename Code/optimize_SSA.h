#ifndef OPTIMIZE_SSA_H
#define OPTIMIZE_SSA_H

#include "IR.h"

void optimize_SSA(IRModule* ir_module);

int pass_constant_propagation(Value* func);
int pass_CSE(Value* func);

#endif