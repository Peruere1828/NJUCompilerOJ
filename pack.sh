#!/bin/bash

# A helper bash script for packing the submission files

# ==========================================
# 请在这里修改为你的实际姓名和学号
STUDENT_NAME="NAME"
STUDENT_ID="ID"
DOC_PATH="stage1.md"
# ==========================================

ZIP_NAME="${STUDENT_NAME}_${STUDENT_ID}.zip"

echo "==== 开始准备打包: $ZIP_NAME ===="

# 1. 检查是否存在 Code 目录并进入
if [ ! -d "Code" ]; then
    echo ">> 错误: 找不到 Code 目录！请确保你在项目的根目录下运行此脚本。"
    exit 1
fi

cd Code || exit 1

# 2. 执行 make clean 和 make
echo ">> 正在清理并编译项目..."
make clean
make

# 检查编译是否成功生成了 parser
if [ ! -f "parser" ]; then
    echo ">> 错误: 编译失败，未能在 Code 目录下生成 parser 可执行文件！打包已中止。"
    exit 1
fi

# 3. 将生成的 parser 拷贝到根目录并替换
echo ">> 编译成功！正在将 parser 拷贝到根目录..."
cp parser ../

# 4. 再次执行 make clean 保持提交的源码目录干净
echo ">> 正在清理 Code 目录中的编译产物..."
make clean

# 5. 回到根目录
cd ..

echo ">> 正在从 $DOC_PATH 生成 report.pdf..."
md2pdf ./doc/$DOC_PATH ./report.pdf

# 检查生成是否成功
if [ ! -f "report.pdf" ]; then
    echo ">> 错误: 生成 report.pdf 失败！请检查 md2pdf 是否安装或 $DOC_PATH 是否存在。"
    exit 1
fi

# 6. 打包所需文件（直接打包目标文件和文件夹，避免嵌套）
echo ">> 正在生成压缩包..."

# 如果之前已经有同名压缩包，先将其删除
if [ -f "$ZIP_NAME" ]; then
    rm "$ZIP_NAME"
fi

# 仅打包题目要求的文件：Code 目录、根目录的 parser、README 和 report.pdf
# -r 参数用于递归打包目录
zip -r "$ZIP_NAME" Code parser README report.pdf

if [ $? -eq 0 ]; then
    echo "====  打包成功！ ===="
    echo "已生成: $ZIP_NAME"
    echo "------------------------------------------------"
    echo "你可以运行以下命令来预览压缩包内部结构，确保没有错误嵌套："
    echo "unzip -l $ZIP_NAME"
else
    echo ">> 错误: 打包失败，请检查是否缺少上述必需文件（例如 README 或 report.pdf）。"
fi