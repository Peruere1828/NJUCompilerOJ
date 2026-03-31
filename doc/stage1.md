## 特性

实现了全部选做内容，并提供宏来对应开关。详见 `config.h`。

注意当宏关闭时，仍然会识别相关词法，但是会对它报错。

## 功能测试

测试了[这个测试](https://github.com/Peruere1828/nju-compiler-test)（fork from NijikaIjichi/nju-compiler-test）的基础内容部分，结果如下：

| 测试选项 | 结果                                       |
| -------- | ------------------------------------------ |
| 全部选做 | AC: 85, FalsePositive: 1, WA: 1, total: 87 |
| 仅选做一 | AC: 67, FalsePositive: 1, WA: 2, total: 70 |
| 仅选做二 | AC: 67, FalsePositive: 1, WA: 2, total: 70 |
| 仅选做三 | AC: 69, FalsePositive: 1, WA: 1, total: 71 |

其中 FalsePositive 的样例简写（后文同样为样例简写）如下：

```c
int main() {
  Node node;
  int a = 1;
}
```

标答认为应仅在 `Node` 行报语法错误，但我在两行都报了语法错误。

WA 的样例包括：

```c
a = 1
b = 2;
```

标答认为应在前一行报语法错误，但我报了后一行。

```c
/*
* Traverse all elements in the array and find the max value
*/
int main() {}
```

不实现选做三时，标答认为前三行都要报词法或语法错误，但我只报了第一行的语法错误。