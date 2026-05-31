/**
 * reg_alloc.h — 图着色寄存器分配器 公共接口
 *
 * ============================================================================
 * 概述
 * ============================================================================
 * 本模块实现了 Briggs 风格的图着色寄存器分配算法。
 * 输入：一个函数的 IR（基本块 + 指令，SSA 已销毁）
 * 输出：RegAllocResult — 每个 IR 值的物理寄存器分配方案
 *
 * ============================================================================
 * 寄存器池 (MIPS32)
 * ============================================================================
 * 可分配寄存器（共 16 个，指数 0-15）：
 *
 *   caller-saved ($t0-$t7, indices 0-7)：
 *     跨函数调用不保留，适合不包含调用的函数
 *
 *   callee-saved ($s0-$s7, indices 8-15)：
 *     跨函数调用保留（调用者保存），适合包含调用的函数
 *     使用时序言/尾声必须保存/恢复
 *
 * 保留寄存器（不参与分配）：
 *   $t8, $t9  — 指令选择的临时寄存器（二元运算、加载等）
 *   $v0, $v1  — 函数返回值
 *   $a0-$a3   — 函数参数传递
 *   $sp, $fp, $ra — 栈指针 / 帧指针 / 返回地址
 *
 * ============================================================================
 * 分配策略
 * ============================================================================
 *   - 无函数调用：K=16，使用 $t0-$t7 + $s0-$s7
 *     优点：寄存器池大，溢出概率低
 *
 *   - 有函数调用：K=8，仅使用 $s0-$s7
 *     原因：$t 寄存器在 CALL 指令后可能被破坏
 *     方案：直接把分配池限制在 $s，免去在每个调用点保存/恢复 $t
 *     代价：可用寄存器减半，溢出概率升高
 *
 *   - 溢出处理：无法着色的值存储在栈帧的溢出区
 *     溢出区紧挨在局部变量上方，属于函数活动记录的一部分
 */

#ifndef REG_ALLOC_H
#define REG_ALLOC_H

#include "IR.h"

#define NUM_ALLOC_REGS  16    /* $t0..$t7 + $s0..$s7 = 16 个 */
#define REG_CALLER_BASE  0    /* $t0 起始索引 */
#define REG_CALLEE_BASE  8    /* $s0 起始索引 */

/**
 * RegAllocResult — 一个函数的寄存器分配结果
 *
 * 数组均以局部索引（0..n_vals-1）为下标，与 ValueIndex 的内部索引一致。
 *
 * 字段说明：
 *   value_id[li]      — 局部索引 li 对应的全局 Value->id
 *   phys_reg[li]      — 分配的物理寄存器号（0..15），-1 表示溢出
 *   spilled[li]       — 是否溢出（1=溢出，存储在栈上）
 *   stack_offset[li]  — 溢出后在 $fp 上的偏移
 *   spill_area_size   — 溢出区总大小（字节，已 4 对齐）
 *   callee_map        — 位掩码：bit s 为 1 表示 $s_s 被分配了值
 *                        序言/尾声据此保存/恢复对应的 $s 寄存器
 */
typedef struct {
    int   n_vals;                /* 分配的值总数 */
    int*  value_id;              /* [local_idx] -> 全局 Value->id */
    int*  phys_reg;              /* [local_idx] -> 物理寄存器号
                                    0..7 = $t0..$t7, 8..15 = $s0..$s7, -1 = 溢出 */
    char* spilled;               /* [local_idx] -> 1 表示溢出 */
    int*  stack_offset;          /* [local_idx] -> 从 $fp 起的偏移 */
    int   spill_area_size;       /* 溢出区总字节数（4 对齐） */
    unsigned char callee_map;    /* bit i=1 表示 $s_i 被使用 */
} RegAllocResult;

/**
 * 对一个函数执行图着色寄存器分配。
 *
 * @param func  函数 IR Value（VK_FUNCTION 类型）
 * @return      堆上分配的 RegAllocResult；用完后需调用 free_reg_alloc_result() 释放
 *              如果函数没有基本块或活跃值，返回 NULL
 */
RegAllocResult* allocate_registers(Value* func);

/**
 * 释放 register allocation result 占用的内存。
 */
void free_reg_alloc_result(RegAllocResult* res);

#endif