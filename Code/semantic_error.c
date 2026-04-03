#include "semantic_error.h"

#include <stdio.h>

int SEMANTIC_ERROR = 0;

void print_semantic_error(SemanticErrorType err_type, int line_num,
                          const char* name) {
  SEMANTIC_ERROR++;
  switch (err_type) {
    case ERR_UNDEFINED_VARIABLE:
      printf("Error type %d at Line %d: Undefined variable \"%s\".\n", err_type,
             line_num, name);
      break;
    case ERR_UNDEFINED_FUNCTION:
      printf("Error type %d at Line %d: Undefined function \"%s\".\n", err_type,
             line_num, name);
      break;
    case ERR_REDEFINED_VARIABLE_OR_STRUCT_NAME:
      printf(
          "Error type %d at Line %d: Redefined variable or struct name "
          "\"%s\".\n",
          err_type, line_num, name);
      break;
    case ERR_REDEFINED_FUNCTION:
      printf("Error type %d at Line %d: Redefined function \"%s\".\n", err_type,
             line_num, name);
      break;
    case ERR_TYPE_MISMATCH_ASSIGNMENT:
      printf("Error type %d at Line %d: Type mismatch in assignment.\n",
             err_type, line_num);
      break;
    case ERR_LVALUE_REQUIRED:
      printf("Error type %d at Line %d: Invalid lvalue in assignment.\n",
             err_type, line_num);
      break;
    case ERR_TYPE_MISMATCH_OPERATOR:
      printf("Error type %d at Line %d: Operand type mismatch.\n", err_type,
             line_num);
      break;
    case ERR_RETURN_TYPE_MISMATCH:
      printf(
          "Error type %d at Line %d: Return type mismatch in function "
          "\"%s\".\n",
          err_type, line_num, name);
      break;
    case ERR_ARGUMENT_MISMATCH:
      printf(
          "Error type %d at Line %d: Argument mismatch in function call "
          "\"%s\".\n",
          err_type, line_num, name);
      break;
    case ERR_ARRAY_ACCESS_ON_NON_ARRAY:
      printf("Error type %d at Line %d: \"%s\" is not an array.\n", err_type,
             line_num, name);
      break;
    case ERR_FUNCTION_CALL_ON_NON_FUNCTION:
      printf("Error type %d at Line %d: \"%s\" is not a function.\n", err_type,
             line_num, name);
      break;
    case ERR_NON_INTEGER_ARRAY_INDEX:
      printf("Error type %d at Line %d: Array index is not integer.\n",
             err_type, line_num);
      break;
    case ERR_DOT_OPERATOR_ON_NON_STRUCT:
      printf("Error type %d at Line %d: \"%s\" is not a struct.\n", err_type,
             line_num, name);
      break;
    case ERR_UNDEFINED_STRUCT_FIELD:
      printf("Error type %d at Line %d: Undefined field \"%s\".\n", err_type,
             line_num, name);
      break;
    case ERR_REDEFINED_STRUCT_FIELD_OR_INIT:
      printf("Error type %d at Line %d: Redefined struct field \"%s\".\n",
             err_type, line_num, name);
      break;
    case ERR_REDEFINED_STRUCT_NAME:
      printf("Error type %d at Line %d: Redefined struct name \"%s\".\n",
             err_type, line_num, name);
      break;
    case ERR_UNDEFINED_STRUCT_TYPE:
      printf("Error type %d at Line %d: Undefined struct \"%s\".\n", err_type,
             line_num, name);
      break;
    case ERR_FUNCTION_DECLARED_BUT_NOT_DEFINED:
      printf(
          "Error type %d at Line %d: Function \"%s\" declared but not "
          "defined.\n",
          err_type, line_num, name);
      break;
    case ERR_CONFLICTING_FUNCTION_DECLARATIONS:
      printf(
          "Error type %d at Line %d: Conflicting declarations of function "
          "\"%s\".\n",
          err_type, line_num, name);
      break;
    default:
      printf("Error type %d at Line %d: Unknown error.\n", err_type, line_num);
      break;
  }
}