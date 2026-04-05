#ifndef SEMANTIC_ERROR_H
#define SEMANTIC_ERROR_H

extern int SEMANTIC_ERROR;

typedef enum {
  // 错误类型 1：变量在使用时未经定义
  ERR_UNDEFINED_VARIABLE = 1,
  // 错误类型 2：函数在调用时未经定义
  ERR_UNDEFINED_FUNCTION,
  // 错误类型 3：变量重复定义，或变量与已定义结构体名重复
  ERR_REDEFINED_VARIABLE_OR_STRUCT_NAME,
  // 错误类型 4：函数重复定义（同名函数多次定义）
  ERR_REDEFINED_FUNCTION,
  // 错误类型 5：赋值号两边表达式类型不匹配
  ERR_TYPE_MISMATCH_ASSIGNMENT,
  // 错误类型 6：赋值号左边是只有右值的表达式
  ERR_LVALUE_REQUIRED,
  // 错误类型 7：操作数类型不匹配，或操作数类型与运算符不匹配
  ERR_TYPE_MISMATCH_OPERATOR,
  // 错误类型 8：return 语句返回类型与函数定义返回类型不匹配
  ERR_RETURN_TYPE_MISMATCH,
  // 错误类型 9：函数调用时实参与形参数目或类型不匹配
  ERR_ARGUMENT_MISMATCH,
  // 错误类型 10：对非数组型变量使用数组访问操作符 []
  ERR_ARRAY_ACCESS_ON_NON_ARRAY,
  // 错误类型 11：对普通变量使用函数调用操作符 ()
  ERR_FUNCTION_CALL_ON_NON_FUNCTION,
  // 错误类型 12：数组访问下标为非整数（如 a[1.5]）
  ERR_NON_INTEGER_ARRAY_INDEX,
  // 错误类型 13：对非结构体型变量使用 . 操作符
  ERR_DOT_OPERATOR_ON_NON_STRUCT,
  // 错误类型 14：访问结构体中未定义的域
  ERR_UNDEFINED_STRUCT_FIELD,
  // 错误类型 15：同一结构体中域名重复定义，或定义时对域初始化
  ERR_REDEFINED_STRUCT_FIELD_OR_INIT,
  // 错误类型 16：结构体名与已定义的结构体或变量名重复
  ERR_REDEFINED_STRUCT_NAME,
  // 错误类型 17：直接使用未定义过的结构体来定义变量
  ERR_UNDEFINED_STRUCT_TYPE,
  // 错误类型 18：函数进行了声明，但没有被定义
  ERR_FUNCTION_DECLARED_BUT_NOT_DEFINED,
  // 错误类型 19：函数多次声明互相冲突，或声明与定义冲突
  ERR_CONFLICTING_FUNCTION_DECLARATIONS
} SemanticErrorType;

void print_semantic_error(SemanticErrorType err_type, int line_num,
                          const char* name);

#endif