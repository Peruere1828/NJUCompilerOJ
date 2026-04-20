// 在实现了SSA后，我们需要把SSA重新转回TAC
#ifndef DESTROY_SSA_H
#define DESTROY_SSA_H

#include "IR.h"

void destroy_SSA(IRModule* ir_module);

#endif