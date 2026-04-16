import os
import subprocess
import sys

# 完整定义阶段和选做宏
STAGES_CONFIG = [
    {
        "macro": "STAGE_ONE",
        "reqs": [
            ("STAGE_ONE_REQ_ONE", "识别八进制和十六进制整数"),
            ("STAGE_ONE_REQ_TWO", "识别科学记数法小数"),
            ("STAGE_ONE_REQ_THREE", "识别单行注释和跨行注释")
        ]
    },
    {
        "macro": "STAGE_TWO",
        "reqs": [
            ("STAGE_TWO_REQ_ONE", "函数除了在定义之外还可以进行声明"),
            ("STAGE_TWO_REQ_TWO", "变量的定义受可嵌套作用域的影响"),
            ("STAGE_TWO_REQ_THREE", "结构体间的类型等价机制由名等价改为结构等价")
        ]
    },
    {
        "macro": "STAGE_THREE",
        "reqs": [
            ("STAGE_THREE_REQ_ONE", "可以出现结构体类型的变量以及函数参数"),
            ("STAGE_THREE_REQ_TWO", "可以出现高维数组类型变量以及一维数组类型的函数参数")
        ]
    }
    # 后续可在此继续添加 STAGE_FOUR 等
]

CONFIG_PATH = "Code/config.h"

def run_cmd(cmd, step_name):
    print(f"\n[{step_name}] Executing: {cmd}")
    result = subprocess.run(cmd, shell=True, text=True)
    if result.returncode != 0:
        print(f"[{step_name}] FAILED!")
        sys.exit(1)
    print(f"[{step_name}] PASSED.")

def rewrite_config(active_stage, active_reqs):
    """通过全量字符串覆盖生成干净的 config.h"""
    
    lines = [
        "#ifndef CONFIG_H",
        "#define CONFIG_H\n"
    ]
    
    for stage_data in STAGES_CONFIG:
        stage_macro = stage_data["macro"]
        
        # 阶段主宏
        if stage_macro == active_stage:
            lines.append(f"#define {stage_macro}\n")
        else:
            lines.append(f"// #define {stage_macro}\n")
            
        # 选做宏
        for req_macro, desc in stage_data["reqs"]:
            lines.append(f"// {desc}")
            if req_macro in active_reqs:
                lines.append(f"#define {req_macro} 1")
            else:
                lines.append(f"// #define {req_macro} 1")
        lines.append("") # 空行分隔
        
    lines.append("#endif\n")

    # 直接覆盖写入
    with open(CONFIG_PATH, "w") as f:
        f.write("\n".join(lines))

def compile_parser():
    build_cmd = "cd Code && make > /dev/null && cp parser ../ && make clean > /dev/null && cd .. && chmod +x parser"
    run_cmd(build_cmd, "Compilation")

def run_test(stage_idx, req_idx):
    test_dir = f"lab{stage_idx}"
    
    # 使用 (...) 打包命令，并使用 >> 追加到根目录的 build.log
    if stage_idx == 1:
        test_cmd = f"(cd ./test_framework/{test_dir} && python3 test.py -p ../../parser -g {req_idx}) >> build.log 2>&1"
    elif stage_idx == 2:
        test_cmd = f"(cd ./test_framework/{test_dir} && python3 test.py -p ../../parser -g {req_idx}) >> build.log 2>&1"
    elif stage_idx == 3:
        if req_idx == 1 or req_idx == 2:
            test_cmd = f"(cd ./test_framework/{test_dir} && ./run.sh -r ../../parser -e extend{req_idx} -c) >> build.log 2>&1"
        else:
            test_cmd = f"(cd ./test_framework/{test_dir} && ./run.sh -r ../../parser -e both -ac) >> build.log 2>&1"
            
    run_cmd(test_cmd, f"Test Stage {stage_idx} Req {req_idx}")

def main():
    accumulated_reqs = [] # 保存之前阶段已经开启的所有选做宏

    for stage_idx_zero_based, stage_data in enumerate(STAGES_CONFIG):
        stage_idx = stage_idx_zero_based + 1
        current_stage_macro = stage_data["macro"]
        
        # 提取当前阶段所有的宏名称
        current_stage_reqs = [req[0] for req in stage_data["reqs"]]

        print(f"\n" + "="*50)
        print(f" STARTING {current_stage_macro}")
        print(f"="*50)

        # 1. 逐个测试当前阶段的选做
        for req_idx_zero_based, req_macro in enumerate(current_stage_reqs):
            req_idx = req_idx_zero_based + 1
            print(f"\n--- Testing {current_stage_macro} with {req_macro} ---")
            
            # 激活：之前阶段的所有选做 + 当前正在测试的选做
            active_reqs = accumulated_reqs + [req_macro]
            
            rewrite_config(current_stage_macro, active_reqs)
            compile_parser()
            run_test(stage_idx, req_idx)

        # 2. 测试当前阶段的“全部选做” (g = 0)
        print(f"\n--- Testing {current_stage_macro} with ALL Requirements ---")
        active_reqs = accumulated_reqs + current_stage_reqs
        
        rewrite_config(current_stage_macro, active_reqs)
        compile_parser()
        run_test(stage_idx, 0)

        # 3. 当前阶段测试完毕，将当前阶段的所有选做加入积累列表中，带入下一阶段
        accumulated_reqs.extend(current_stage_reqs)

    print("\n ALL STAGES AND REQUIREMENTS PASSED SUCCESSFULLY!")

if __name__ == "__main__":
    main()