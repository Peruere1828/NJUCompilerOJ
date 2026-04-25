## 特性

实现了全部选做内容，并提供宏来对应开关。详见 `config.h`。

“不同结构体的域重名”或者“域和普通变量重名”均不报错，例如：

```c
struct st1 { int i; };
struct st2 { int i; };
int main() { int i; return 0; }
```

**不会报任何错**。

## 功能测试

测试了[这个测试](https://github.com/Peruere1828/nju-compiler-test/tree/0035661e904771cc1d6a0adc76231fedc0611195)（fork from NijikaIjichi/nju-compiler-test）的所有部分，结果如下：

| 测试选项     | 结果                                          |
| ------------ | --------------------------------------------- |
| Advance 样例 | AC: 642, FalsePositive: 35, WA: 3, total: 680 |
| 全部选做     | AC: 147, WA: 3, total: 150                    |
| 仅选做一     | AC: 136, FalsePositive: 1, total: 137         |
| 仅选做二     | AC: 139, FalsePositive: 1, total: 140         |
| 仅选做三     | AC: 137, total: 137                           |

其中仅选做一和仅选做二中的 FalsePositive 的样例简写（后文同样为样例简写）如下：

```c
struct IntVector {
  int ix, iy;
};

struct FloatVector {
  int fx, fy;
};

int vec_is_equal(struct IntVector iv, struct IntVector iw) {
  return 0;
}

int vec_is_equal(struct FloatVector fv, struct FloatVector fw) {
  return 0;
}

int main() {
  struct IntVector v1;
  struct FloatVector v2;

  vec_is_equal(v1, v1);
  vec_is_equal(v2, v2);
  return 0;
}
```

评测程序认为应仅在第二个 `vec_is_equal` 定义处行报 4 号错误，但事实上第二次调用处也可以再多报一次 9 号错误。

Advance 样例和全部选做下 WA 的样例主要是因为同时开启选做一和选做三时，两个具有结构等价形参的函数声明本不应该报错，但是评测程序错误地认为需要报错。这个问题属于测试脚本的局限性，按照 PDF 的指示，我的程序输出实际是正确的。

## 局限性

已知存在内存泄漏问题。例如 `semantic.c` 中 `visit_VarDec` 函数中定义的 `array_type` 有内存泄漏。考虑到项目本身是一个玩具级别的编译器，故暂时不去仔细处理，留待后续修复。