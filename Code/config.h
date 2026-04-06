#ifndef CONFIG_H
#define CONFIG_H

// #define STAGE_ONE

// #define DEBUG 1

// 识别八进制和十六进制整数
#define STAGE_ONE_REQ_ONE 1
// 识别科学记数法小数
#define STAGE_ONE_REQ_TWO 1
// 识别单行注释和跨行注释
#define STAGE_ONE_REQ_THREE 1

// #define STAGE_TWO

// 函数除了在定义之外还可以进行声明
#define STAGE_TWO_REQ_ONE 1
// 变量的定义受可嵌套作用域的影响
#define STAGE_TWO_REQ_TWO 1
// 结构体间的类型等价机制由名等价改为结构等价
#define STAGE_TWO_REQ_THREE 1

#define STAGE_THREE

// 可以出现结构体类型的变量以及函数参数
#define STAGE_THREE_REQ_ONE 1
// 可以出现高维数组类型变量以及一维数组类型的函数参数
#define STAGE_THREE_REQ_TWO 1

#endif