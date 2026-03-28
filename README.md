2026 年春季编译原理大作业个人解答，完成了全部必做和选做部分。**请勿抄袭。**

选做部分用宏区分，详见 `config.h`。

我仅完成**我拿到的任务要求**中的必做任务与**指定选做任务**，对于要求明确禁止实现的其余选做任务，**即便代码中存在相关实现，我也不保证其正确性。** 代码中所有与要求可能存在冲突的内容，均已通过 `FEAT:` 标记注明。

## 环境搭建

我提供了 `Dockerfile` 用于搭建实验环境。

可以参考如下命令为自己和队友搭建实验环境，之后队友可以通过 `ssh` 访问主机的 2345 端口进入实验容器。

```bash
docker build --network host -t compiler-oj-env .
docker run -id --name my-lab --network host -v ~/compiler-oj/lab_my:/workspace compiler-oj-env /bin/bash
docker run -d --name teammate-lab --network host -v ~/compiler-oj/lab_teammate:/workspace compiler-oj-env /usr/sbin/sshd -D -p 2345
```