# 实验四：MIPS32 汇编代码生成

## 概述

自 `c94cce2` (update gitignore) 以来，完成了编译器后端——将 C-- 语言的 IR 中间表示翻译为 MIPS32 汇编，并通过 spim 模拟器验证。共 14 个提交，新增 3 个核心文件，修改 10 个文件，净增约 2700 行代码。最终测试结果：**553/553 全部通过**（base 496 + extend 57）。

---

## 1. 新增文件

| 文件 | 功能 |
|------|------|
| `Code/codegen.c` (886 行) | MIPS32 汇编代码生成器，支持 Phase 1（栈式）和 Phase 2（图着色）两种模式 |
| `Code/codegen.h` (17 行) | `generate_mips()` 接口声明 |
| `Code/reg_alloc.c` (804 行) | Briggs 风格的图着色寄存器分配器 |
| `Code/reg_alloc.h` (92 行) | 寄存器分配器公共接口，定义 `RegAllocResult` 结构体 |
| `Test/phase4/test.py` (343 行) | 测试框架：编译 C-- → 运行 spim → 比对 stdout |
| `Test/phase4/check_spim.py` (95 行) | spim 输出检查器 |
| `Test/phase4/check_irsim.py` (71 行) | IR 模拟器输出检查器 |
| `Test/phase4/test_cases/` | 三个冒烟测试用例 (add, fib, sign) |

---

## 2. 代码生成 (`codegen.c`)

### 2.1 两种寄存器分配模式

```
Phase 1 (--phase=1): 栈式朴素方案
  所有变量和临时值存储在栈帧中，运算时 load → $t8/$t9 → 计算 → store 回栈。
  不需要寄存器分配，简单但生成大量冗余 lw/sw。

Phase 2 (--phase=2): 图着色寄存器分配 (默认)
  通过 reg_alloc.c 将 IR 值映射到物理寄存器，溢出的值才放栈上。
  显著减少内存访问指令。
```

### 2.2 栈帧布局

```
高地址
  saved $ra (仅 has_calls)
  saved $fp
  saved $sX (phase 2, 按需)
  ... 局部变量 / 临时值 / 溢出区 ...
  arg build area (16 bytes, 仅 has_calls)
低地址  ← $fp
```

### 2.3 调用约定

- 前 4 个整数参数 → `$a0`-`$a3`，其余 → 栈上
- 返回值 → `$v0`
- `$t8`/`$t9` 为指令选择暂存器（不参与寄存器分配）
- `main` 保持原名，其他用户函数加 `_` 前缀避免与 MIPS 指令名冲突

### 2.4 Floor 除法

C-- 语义要求向负无穷取整，而 MIPS `div` 向零截断。实现方法：

```
1. div $t8,$t9   → LO=trunc(a/b), HI=a%b
2. 若余数=0 或 a,b 同号 → 直接取商
3. 若余数≠0 且 a,b 异号 → floor = trunc - 1
```

---

## 3. 图着色寄存器分配器 (`reg_alloc.c`)

### 3.1 算法流程 (Briggs 乐观着色)

```
Step 1: 值索引     → 收集所有活跃值，建立全局→局部索引映射
Step 2: 活跃性分析 → 迭代数据流分析，计算 gen/kill/live_in/live_out
Step 3: 冲突图     → 邻接矩阵存储，逐指令模拟活跃集变化
Step 4: 简化       → 度数 < K 的节点压栈移除（Kempe 定理）
Step 5: 溢出       → 启发式选择溢出代价最小的节点
Step 6: 选择       → 逆序弹出节点，分配第一个可用颜色
Step 7: 构建结果   → 打包为 RegAllocResult
```

### 3.2 寄存器池 (MIPS32)

| 类型 | 寄存器 | 索引 | 用途 |
|------|--------|------|------|
| caller-saved | `$t0`-`$t7` | 0-7 | 无调用函数可用 |
| callee-saved | `$s0`-`$s7` | 8-15 | 有调用函数使用 |
| 暂存器 | `$t8`,`$t9` | 16-17 | 指令选择（不参与分配） |
| 返回值 | `$v0`,`$v1` | 18-19 | 函数返回 |
| 参数 | `$a0`-`$a3` | 20-23 | 参数传递 |
| 特殊 | `$ra`,`$fp`,`$sp` | 24-26 | 栈/帧管理 |

### 3.3 分配策略

- **无函数调用**：K=16，使用 `$t0`-`$t7` + `$s0`-`$s7`
- **有函数调用**：K=8，仅使用 `$s0`-`$s7`（`$t` 寄存器跨调用不保留）

### 3.4 溢出代价

```
spill_cost(v) = (use_count + def_count) × (loop_depth × 10 + 1) / (degree + 1)
```

循环深度通过回边数量近似估算。

---

## 4. SSA 处理修复

### 4.1 SSA 销毁 (`destroy_SSA.c`)

完善了三步流程：

1. **清理死代码**：移除 GOTO/RETURN 之后的不可达指令，确保每个基本块有明确终结符
2. **拆分关键边**：避免 phi 节点销毁时的并行拷贝冲突
3. **两阶段 phi 消除**：
   - 阶段 1（快照）：危险读取先复制到临时变量
   - 阶段 2（写入）：安全地将值写入目标

### 4.2 关键边拆分

```
拆分前: if (x) → A (含 phi) 或 B (含 phi)  [A,B 入度均≥2]
拆分后: if (x) → mid_A → A; else → mid_B → B
         COPY 指令放在 mid_A/mid_B 中，各自只影响一个后继
```

---

## 5. 函数调用参数处理

### 5.1 问题

原始 `translate_Args` 边求值边发射 ARG 指令。当遇到嵌套调用 `f(g(x), h(y))` 时，内层 `g(x)` 和 `h(y)` 的 CALL+ARG 指令会与外层 `f` 的 ARG 指令交错，导致参数顺序错误。

### 5.2 解决方案

两阶段参数求值：

```
阶段 1 (eval_and_collect_args): 递归求值所有参数表达式，生成嵌套 CALL
阶段 2 (translate_Args):        统一逆序发射 ARG 指令
```

这样 `ARG g_ret, ARG h_ret` 连续发射，`CALL f` 时 `$a0=g_ret, $a1=h_ret`。

---

## 6. 栈帧布局修复

关键修复：`arg_area`（16 字节传出参数区）必须在局部变量之前分配。

```
修复前: $fp+0 = 第一个局部变量，可能被第 5 个 CALL 参数覆盖
修复后: $fp+0..$fp+15 = arg_area，局部变量从 $fp+16 开始
```

---

## 7. 调试支持

在 `main.c` 中增加了命令行选项：

```
./parser test.cmm out.s --phase=1    # 使用栈式模式
./parser test.cmm out.s --phase=2    # 使用图着色模式（默认）
./parser test.cmm out.s --dump-ir    # 同时输出 .ir 中间代码文件
```

---

## 8. 测试结果

| 测试集 | 用例数 | 结果 |
|--------|--------|------|
| base (基础) | 496 | ✅ 全部通过 |
| extend (扩展) | 57 | ✅ 全部通过 |
| **合计** | **553** | **553/553** |

测试命令：
```bash
python3 ./Test/phase4/test.py -r ./parser --spim-only -e both -ac
```

---

## 9. 提交历史

```
0c60e9e docs: add comprehensive Chinese comments explaining algorithm details
43be896 feat: switch to phase=2 graph-colouring register allocator (552/553)
77d66cc fix: frame layout — arg_area must be reserved before locals, not after
8095465 fix: translate_Args defer ARG emission, 97/97 base pass
498cd3f diag: add assertions to codegen and restore phi assert
8612551 fix: SSA CFG terminator + floor-div semantics
1983334 fix: implement Python-style floor division matching irsim semantics
3ef6cde fix: SSA destroy assertion + stack smash for deep CFGs
c727cc8 fix: translate args: nested call ARG interleaving
2c2f225 fix: ARG buffering with arg_tos marker — 96/97 base tests pass
2ef74f3 fix: correct ARG buffering for nested function calls
257294a fix: critical destroy_SSA id assignment + 2-arg CLI
129e610 feat: add Briggs-style graph-colouring register allocator
7c8e9d7 feat: implement stack-based MIPS32 codegen (Phase 1, STAGE_FOUR)
deec067 add tests
```