## 特性

实现了全部选做内容，并提供宏来对应开关。详见 `config.h`。

## 功能测试

测试了[这个测试](https://github.com/Peruere1828/nju-compiler-test)（fork from NijikaIjichi/nju-compiler-test）的所有部分，结果如下：

| 测试选项     | 结果                                          |
| ------------ | --------------------------------------------- |
| Advance 样例 | AC: 613, FalsePositive: 35, WA: 2, total: 650 |
| 全部选做     | AC: 118, WA: 2, total: 120                    |
| 仅选做一     | AC: 108, FalsePositive: 1, total: 109         |
| 仅选做二     | AC: 111, FalsePositive: 1, total: 112         |
| 仅选做三     | AC: 109, total: 109                           |

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

Advance 样例和全部选做下 WA 的样例主要是因为同时开启选做一和选做三时，两个具有结构等价形参的函数声明本不应该报错，但是评测程序错误地认为需要报错。

## 局限性

可能存在内存泄漏问题。