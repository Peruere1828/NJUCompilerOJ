#ifndef OPTIMIZE_TAC_H
#define OPTIMIZE_TAC_H

#include "IR.h"

void optimize_TAC(IRModule* ir_module);

int pass_dce(Value* func);
int pass_simplify_CFG(Value* func);

#endif