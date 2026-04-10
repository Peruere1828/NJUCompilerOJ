/*

编译器中间代码里所有能产生/持有/使用值的实体（常量、变量、指令、基本块），全部抽象为同一个基类
Value；用 Use 结构体专门记录“定义 -> 使用”的依赖关系，不耦合在实体内部。

Use 结构体是连接定义者和使用者的有向边，是整个依赖管理的最小单元
- def：被使用的定义点（只能是 Value：常量、变量、另一条指令）；
- user：使用这个值的指令（指令是唯一的使用者类型，因为只有指令会消费值）；
- pre/nxt：双向链表，把同一个 def 的所有使用者串成一条链。
同一个 def（比如一个常量）可能被多条指令使用，对应多个 Use 节点，
因此可以串成链表挂在 def 的 use_list 上；

我们会进行两步的翻译把AST翻译成中间代码。首先会翻译成允许可变变量的初始 IR，
在这个阶段，代表局部变量 x 的 Value 其实不是一个真正意义上的数据值，
而是抽象的内存变量。以 x = x+5 为例，会首先翻译成
t_1 := v_1 + #5
v_1 := t_1
这里 v_1 相当于一个内存位置，并不是 SSA 的虚拟寄存器。
同时这里 Value 的关系是：
Value A（V_VAR，v_1）
Value B（V_CONST_INT，#5）
Value C（V_INST，OP_ADD）它本身既是加法指令，也是临时结果 t_1
Value D（V_INST，OP_ASSIGN）表示 v_1 = t_1
然后会有这样的 Def-Use 链：
Use1.def = A, Use1.user = C
Use2.def = B, Use2.user = C
Use3.def = C, Use3.user = D
Use4.def = A, Use4.user = D
如果考虑每个 Value 的 use_lists，就会有：
Value A：
- Use 1：def = A, user = C。语义：我作为右值，被加法指令 C 读取了。
- Use 4：def = A, user = D。语义：我作为左值（目标地址），被赋值指令 D 覆写了。
Value B：
- Use 2：def = B, user = C。语义：我作为一个常数参数，被加法指令 C 读取了。
Value C：
- Use 3：def = C, user = D。语义：我算出来的这个中间结果，被赋值指令 D
拿去当作数据源了。 Value D：
- 空，因为它不产生任何可以被后续表达式继续利用的 Value。

在下一步，我们会把这种 IR 进行变量重命名，让它符合 SSA 的要求，变成这样：
t_1 := v_1_1 + #5
v_1_2 := t_1

之前阶段的名称字段char*实际上都是引用的AST上的内容。考虑到中间代码是一个独立的部分，
这个阶段涉及的字段应当重新strdup

先翻译成TAC格式，再做CFG，计算支配树得到SSA
*/

#ifndef IR_H
#define IR_H

#include "AST.h"
#include "type.h"

typedef struct Use Use;
typedef struct Value Value;
typedef struct IRModule IRModule;

struct Use {
  Value* def;      // 指向被使用的源数据
  Value* user;     // 指向使用数据的指令
  Use *pre, *nxt;  // 双向链表
};

typedef enum {
  VK_CONST_INT,    // 整数常量
  VK_CONST_FLOAT,  // 浮点数常量
  VK_VAR,          // 局部变量
  VK_GLOBAL_VAR,   // 全局变量
  VK_INST,         // 计算或跳转指令
  VK_FUNCTION,     // 函数
  VK_BB            // 基本块
} ValueKind;

typedef enum {
  // 算数运算
  OP_I_ADD,
  OP_I_SUB,
  OP_I_MUL,
  OP_I_DIV,
  OP_F_ADD,
  OP_F_SUB,
  OP_F_MUL,
  OP_F_DIV,
  OP_ASSIGN,

  // 内存分配
  OP_DEC,       // 内存分配 (DEC x [size])
  OP_GET_ADDR,  // 取地址 (x := &y)
  OP_LOAD,      // 解引用读取 (x := *y)
  OP_STORE,     // 解引用写入 (*x := y)

  // 控制流
  OP_LABEL,
  OP_GOTO,
  OP_IF_GOTO,
  OP_RETURN,

  // 函数调用
  OP_CALL,
  OP_ARG,
  OP_PARAM,

  OP_READ,
  OP_WRITE
} Opcode;

struct Value {
  ValueKind vk;
  Type* tp;
  Use* use_list;

  int id;

  union {
    // 常量
    unsigned long int_val;
    float float_val;

    // 局部变量
    int var_id;

    // 全局变量
    struct {
      int var_id;
      Value* next_global;  // 用于串联到 IRModule
    } global_var;

    // 在语义分析阶段给所有局部变量和全局变量分配了唯一var_id

    // 指令
    struct {
      Opcode opcode;
      // 操作数数组
      int num_ops;
      Value** ops;
      RelopKind rk;  // 条件跳转的关系符号

      // 归属的基本块，以及前后指令
      Value* parent_bb;
      Value* pre;
      Value* nxt;
    } inst;

    // 基本块
    struct {
      int bb_id;  // 基本块id

      Value* parent_func;  // 所属的函数 (VK_FUNC)
      Value* inst_head;    // 块内指令链表头
      Value* inst_tail;    // 块内指令链表尾

      Value* next_bb;  // 用于在 Function 中串联基本块

      // CFG 预留：前驱与后继基本块数组
      Value** preds;
      int num_preds;
      Value** succs;
      int num_succs;
    } bb;

    // 函数
    struct {
      char* name;
      int argc;
      Value** args;  // 形参 Value 列表

      IRModule* parent_module;
      Value* bb_head;  // 函数内基本块链表头
      Value* bb_tail;  // 函数内基本块链表尾

      Value* next_func;  // 串联在 IRModule 的 func_list 中
    } func;
  } u;
};

// 用于在 Module 层维持一个结构体注册表
typedef struct StructTypeDef {
  char* name;  // 必须是 strdup 后的独立内存
  Type* type;  // 指向具体的结构体类型描述
  struct StructTypeDef* next;
} StructTypeDef;

// 表示整个 C-- 程序的顶层 IR 模块
typedef struct IRModule {
  // 函数链表头 (通过 Value.u.func.next_func 串联)
  Value* func_list;

  // 全局变量链表头 (通过 Value.u.global.next_global 串联)
  Value* global_var_list;

  // 结构体类型注册表
  StructTypeDef* struct_list;
} IRModule;

// 创建基础的 Value 节点
Value* create_value(ValueKind vk, Type* tp);
// 指令 user 使用了数据源 def
void add_use(Value* def, Value* user);

#endif