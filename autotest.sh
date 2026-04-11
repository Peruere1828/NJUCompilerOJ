#!/bin/bash

# A helper bash script for auto-test

CODE_DIR="Code"

if [ "$1" == "p1" ]; then
    TEST_DIR="Test/phase1"
elif [ "$1" == "p2" ]; then
    TEST_DIR="Test/phase2"
elif [ "$1" == "p3" ]; then
    TEST_DIR="Test/phase3"
elif [ "$1" == "p4" ]; then
    TEST_DIR="Test/phase4"
elif [ "$1" == "p5" ]; then
    TEST_DIR="Test/phase5"
else
    echo "Usage: $0 p1|p2|p3|p4|p5"
    exit 1
fi

echo "REBUILDING THE PROJECT..."
cd $CODE_DIR || exit 1
make clean
make

if [ ! -f "parser" ]; then
    echo ">> 错误: 编译失败，未能在 $CODE_DIR 目录下生成 parser 可执行文件！"
    exit 1
fi

cp parser ../

echo ">> 正在清理 Code 目录中的编译产物..."
make clean

echo "COMPILATION SUCCESSFUL! Starting tests..."

echo "RUNNING TESTS..."
cd ..
chmod +x parser

for test_file in $TEST_DIR/*.cmm; do
    echo "----------------------------------------"
    echo "Testing: $test_file"

    if [ "$1" == "p3" ]; then
        mkdir -p "$TEST_DIR/tmp"
        base=$(basename "$test_file" .cmm)
        ./parser "$test_file" "$TEST_DIR/tmp/$base.ir"
    else
        ./parser "$test_file"
    fi
done