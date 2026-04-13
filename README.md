2026 年春季编译原理大作业个人解答，完成了全部必做和选做部分。**请勿抄袭。**

选做部分用宏区分，详见 `config.h`。

## 环境搭建

我提供了 `Dockerfile` 用于搭建实验环境。

可以参考如下命令为自己和队友搭建实验环境，之后自己可以在 VSCode 中先 `ssh` 连接主机，再附加到容器；队友可以通过 `ssh` 直接访问主机的 2345 端口进入实验容器。

```bash
docker build --network host -t compiler-oj-env .
docker run -id --name my-lab --network host --restart unless-stopped -v ~/compiler-oj/lab_my:/workspace compiler-oj-env /bin/bash
docker run -d --name teammate-lab --network host --restart unless-stopped -v ~/compiler-oj/lab_teammate:/workspace compiler-oj-env /usr/sbin/sshd -D -p 2345
```

注意：`Dockerfile` 中额外下载了 `PrinceXML`，用于在 VSCode 中用 Markdown 相关插件导出 pdf。如果没有需要可以删去相关命令。

## 额外脚本提供

我提供了 `pack.sh` 和 `autotest.sh` 两个脚本，运行前需要 `chmod +x`。

`pack.sh` 的作用是打包提交文件。需要注意的是，使用前需要**手动**导出提交报告，放在 `doc` 目录下，同时需要正确设置 `pack.sh` 开头的若干变量，以保证成功打包。

`autotest.sh` 会自动构建代码，并且运行 `Test` 目录下的测试。注意并不会检验测试的正确性。

## 更多测试样例

可以参考[我 fork 的测试用例仓库](https://github.com/Peruere1828/nju-compiler-test)。