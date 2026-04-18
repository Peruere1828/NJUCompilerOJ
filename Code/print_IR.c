#include <stdio.h>

#include "AST.h"
#include "IR.h"
#include "IRbuilder.h"
#include "translate.h"

#if defined(DEBUG) && defined(STAGE_THREE)
#define PRINT_DEBUG
#endif

// 为所有指令生成 ID（可以在生成指令时做，也可以在打印前统一做）
int global_inst_id = 1;

void print_inst(Value* inst, FILE* out);
void print_value(Value* val, FILE* out);

static const char* relop_to_str(RelopKind rk) {
  switch (rk) {
    case RELOP_EQ:
      return "==";
    case RELOP_NEQ:
      return "!=";
    case RELOP_LT:
      return "<";
    case RELOP_LEQ:
      return "<=";
    case RELOP_GT:
      return ">";
    case RELOP_GEQ:
      return ">=";
    default:
      return "??";
  }
}

void print_module(IRModule* module, FILE* out) {
  if (!module) return;

  // 遍历所有函数
  Value* curr_func = module->func_list;
  while (curr_func) {
    fprintf(out, "FUNCTION %s :\n", curr_func->u.func.name);

    // 遍历函数内的所有基本块
    Value* curr_bb = curr_func->u.func.bb_head;
    while (curr_bb) {
      fprintf(out, "LABEL label%d :\n", curr_bb->u.bb.bb_id);

      // 遍历块内所有指令
      Value* curr_inst = curr_bb->u.bb.inst_head;
      while (curr_inst) {
        print_inst(curr_inst, out);
        curr_inst = curr_inst->u.inst.nxt;
      }
      curr_bb = curr_bb->u.bb.next_bb;
    }
    curr_func = curr_func->u.func.next_func;
  }
}

void print_inst(Value* inst, FILE* out) {
  if (inst == NULL || inst->vk != VK_INST) return;

  // 如果这条指令有返回值 (比如加法、调用)，需要先打印左值 "t_x := "
  // 有些指令 (如 GOTO, RETURN, STORE) 没有左值结果
  Opcode op = inst->u.inst.opcode;
  int has_lhs =
      (op == OP_I_ADD || op == OP_I_SUB || op == OP_I_MUL || op == OP_I_DIV ||
       op == OP_F_ADD || op == OP_F_SUB || op == OP_F_MUL || op == OP_F_DIV ||
       op == OP_CALL || op == OP_GET_ADDR || op == OP_LOAD || op == OP_PHI);

  if (has_lhs) {
    print_value(inst, out);  // 打印它自己 (左值 t_x)
    fprintf(out, " := ");
  }

  Value** ops = inst->u.inst.ops;  // 操作数数组

  switch (inst->u.inst.opcode) {
    case OP_ASSIGN:
      print_value(ops[0], out);
      fprintf(out, " := ");
      print_value(ops[1], out);
      break;
    case OP_I_ADD:
    case OP_F_ADD:
      print_value(ops[0], out);
      fprintf(out, " + ");
      print_value(ops[1], out);
      break;
    case OP_I_SUB:
    case OP_F_SUB:
      print_value(ops[0], out);
      fprintf(out, " - ");
      print_value(ops[1], out);
      break;
    case OP_I_MUL:
    case OP_F_MUL:
      print_value(ops[0], out);
      fprintf(out, " * ");
      print_value(ops[1], out);
      break;
    case OP_I_DIV:
    case OP_F_DIV:
      print_value(ops[0], out);
      fprintf(out, " / ");
      print_value(ops[1], out);
      break;
    case OP_GET_ADDR:
      fprintf(out, "&");
      print_value(ops[0], out);
      break;
    case OP_LOAD:
      fprintf(out, "*");
      print_value(ops[0], out);
      break;
    case OP_STORE:
      // *x := y
      fprintf(out, "*");
      print_value(ops[0], out);
      fprintf(out, " := ");
      print_value(ops[1], out);
      break;
    case OP_GOTO:
      fprintf(out, "GOTO ");
      print_value(ops[0], out);
      break;
    case OP_IF_GOTO:
      // IF x [relop] y GOTO z
      fprintf(out, "IF ");
      print_value(ops[0], out);
      fprintf(out, " %s ", relop_to_str(inst->u.inst.rk));
      print_value(ops[1], out);
      fprintf(out, " GOTO ");
      print_value(ops[2], out);
      break;
    case OP_RETURN:
      fprintf(out, "RETURN ");
      print_value(ops[0], out);
      break;
    case OP_DEC:
      fprintf(out, "DEC ");
      print_value(ops[0], out);
      fprintf(out, " ");
      fprintf(out, "%lu", ops[1]->u.int_val);
      break;
    case OP_ARG:
      fprintf(out, "ARG ");
      print_value(ops[0], out);
      break;
    case OP_CALL:
      fprintf(out, "CALL ");
      print_value(ops[0], out);
      break;
    case OP_PARAM:
      fprintf(out, "PARAM ");
      print_value(ops[0], out);
      break;
    case OP_READ:
      fprintf(out, "READ ");
      print_value(inst, out);
      break;
    case OP_WRITE:
      fprintf(out, "WRITE ");
      print_value(ops[0], out);
      break;
    case OP_PHI:
      fprintf(out, "PHI(");
      for (int i = 0; i < inst->u.inst.num_ops; i += 2) {
        if (i > 0) fprintf(out, ", ");
        fprintf(out, "[");
        print_value(ops[i], out);
        fprintf(out, ", ");
        print_value(ops[i + 1], out);
        fprintf(out, "]");
      }
      fprintf(out, ")");
      break;
    default:
      fprintf(out, "UNKNOWN_INST");
      break;
  }
#ifdef PRINT_DEBUG
  if (has_lhs && inst->use_list != NULL) {
    fprintf(out, "  \t// uses: [");
    Use* cur_use = inst->use_list;
    while (cur_use != NULL) {
      print_value(cur_use->user, out);  // 打印使用这条指令结果的下游指令
      if (cur_use->nxt) fprintf(out, ", ");
      cur_use = cur_use->nxt;
    }
    fprintf(out, "]");
  }
#endif
  fprintf(out, "\n");
}

void print_value(Value* val, FILE* out) {
  if (!val) return;
  switch (val->vk) {
    case VK_CONST_INT:
      fprintf(out, "#%ld", val->u.int_val);
      break;
    case VK_CONST_FLOAT:
      fprintf(out, "#%f", val->u.float_val);
      break;
    case VK_VAR:
      // 局部变量打印为 v_id
      fprintf(out, "v%d", val->id);
#ifdef PRINT_DEBUG
      if (val->tp && val->tp->kind == TYPE_BASIC) {
        fprintf(out, " BASICv");
      } else if (!val->tp) {
        fprintf(out, " UKE_TYPE");
      }
#endif
      break;
    case VK_INST:
      // 指令的结果打印为 t_id
      fprintf(out, "t%d", val->id);
#ifdef PRINT_DEBUG
      if (val->tp && val->tp->kind == TYPE_BASIC) {
        fprintf(out, " BASICi");
      }
#endif
      break;
    case VK_BB:
      // 基本块作为标号打印
      fprintf(out, "label%d", val->u.bb.bb_id);
      break;
    case VK_FUNCTION:
      fprintf(out, "%s", val->u.func.name);
      break;
    default:
      break;
  }
}