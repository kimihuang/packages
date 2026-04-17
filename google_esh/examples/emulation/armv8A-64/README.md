# ARMv8-A 64-bit (Cortex-A72) QEMU 仿真编译运行指南

## 概述

`examples/emulation/armv8A-64` 是 Embedded Shell (esh) 项目的 ARMv8-A 64-bit 示例，基于 **Cortex-A72** 处理器，在 **QEMU virt** 机器上仿真运行。该示例实现了一个基于 UART 的交互式命令行 shell，内存占用 < 4kB。

## 目录结构

```
examples/emulation/armv8A-64/
├── Makefile              # 构建配置（工具链、内存布局、QEMU 启动参数）
├── hello.c               # 示例命令：hello 命令实现
└── platform/
    ├── platform.c        # 平台初始化（UART 注册）
    └── uart/
        ├── uart.h        # UART 驱动头文件
        └── uart.c        # PL011 UART 驱动实现
```

## 环境依赖

### 必需工具

| 工具 | 用途 |
|------|------|
| `make` | 构建系统 |
| `aarch64-none-linux-gnu-gcc` / `aarch64-none-linux-gnu-ld` | AArch64 交叉编译器 |
| `qemu-system-aarch64` | QEMU AArch64 系统仿真器 |

### 安装依赖

```bash
# 交叉编译工具链（使用 aarch64-none-linux-gnu- 前缀）
sudo apt install -y gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu

# 如果系统中已有 aarch64-none-linux-gnu- 工具链则无需额外安装

# QEMU 仿真器
sudo apt install -y qemu-system-aarch64

# 构建
sudo apt install -y make binutils
```

> **注意**：本项目 Makefile 中 `TOOLCHAIN_PREFIX` 已设置为 `aarch64-none-linux-gnu-`。如果你的系统只有 `aarch64-linux-gnu-` 前缀的工具链，需修改 Makefile 第 17 行：
> ```
> TOOLCHAIN_PREFIX = aarch64-linux-gnu-
> ```

### 可选工具（用于 GDB 调试）

```bash
sudo apt install -y gdb-multiarch

# GDB Dashboard（可选，提供可视化调试界面）
wget -P ~ https://git.io/.gdbinit
pip3 install pygments
```

## 编译

```bash
cd examples/emulation/armv8A-64
make
```

编译输出示例：

```
Detected Configuration
*
├── PROJECT             : shell
├── TOOLCHAIN           : aarch64-none-linux-gnu-
├── STARTUP             : aarch64
├── RAM BASE            : 0x40000000
├── RAM SIZE            : 0x4000
...

compiling hello.c
compiling platform/platform.c
compiling platform/uart/uart.c
...
generating shell.elf
Size Report:
   text    data    bss     dec     hex    filename
   9385     281    136    9802    264a    (TOTALS)
Memory region         Used Size  Region Size  %age Used
             RAM:        9660 B        16 KB     58.96%
generating shell.bin
Done!
```

生成的文件：

| 文件 | 说明 |
|------|------|
| `shell.elf` | ELF 可执行文件（含调试符号） |
| `shell.bin` | 纯二进制文件 |
| `shell.elf.map` | 链接映射文件 |
| `shell.elf.lst` | 汇编列表文件 |

## 运行

```bash
make run
```

QEMU 启动参数说明（`Makefile:40-42`）：

```
qemu-system-aarch64 -M virt,secure=on,virtualization=on \
                     -cpu cortex-a72 -nographic \
                     -kernel shell.elf
```

| 参数 | 说明 |
|------|------|
| `-M virt,secure=on,virtualization=on` | 使用 QEMU virt 平台，启用安全扩展和虚拟化 |
| `-cpu cortex-a72` | 模拟 Cortex-A72 处理器 |
| `-nographic` | 无图形输出，通过终端交互 |
| `-kernel shell.elf` | 加载 ELF 作为内核镜像 |

启动后看到提示符 `#`，即可输入命令：

```
Build: [a4ec632:1ecb507] - [user@host] - Apr 17 2026 - 11:45:56
#
```

### Shell 内置命令

| 命令 | 说明 |
|------|------|
| `hello` | 示例命令，回显输入的参数 |
| `help` | 显示所有可用命令 |
| `r32 <addr>` | 读取 32 位内存 |
| `w32 <addr> <val>` | 写入 32 位内存 |
| `read <addr> <len>` | 读取指定长度的内存 |
| `echo on/off` | 开关输入回显 |
| `version` | 显示构建信息 |
| `printf_examples` | printf 用法示例 |

### 交互示例

```
Build: [a4ec632:1ecb507] - [user@host] - Apr 17 2026
# hello world
hello world
Press ctrl + a, x to exit !
# help
hello
        Echoes the commandline
        usage: hello <any string>
...
#
```

### 退出 QEMU

按 `Ctrl+A`，然后按 `X` 退出。

## GDB 调试

需要两个终端。

**终端 1** — 启动 QEMU 并暂停 CPU：

```bash
make debug
```

**终端 2** — 连接 GDB：

```bash
make gdb
```

连接后可使用标准 GDB 命令进行调试（`break`、`continue`、`step` 等）。

## Makefile 配置说明

```makefile
TOOLCHAIN_PREFIX = aarch64-none-linux-gnu-   # 交叉编译工具链前缀
OPTIMIZATION     = g                          # 优化级别 (-Og)
STARTUP          = aarch64                    # 启动文件（shell/startup/aarch64.S）
RAM_BASE_PHYSICAL = 0x40000000               # RAM 起始物理地址
RAM_SIZE          = 0x4000                    # RAM 大小（16KB）
SHELL_ROOT        = ../../../shell             # shell 源码相对路径
```

## 添加自定义命令

1. 在项目目录下创建 `.c` 文件（如 `mycmd.c`）
2. 包含 `#include "shell.h"`
3. 编写函数，签名为 `int func(int argc, char** argv)`
4. 使用 `ADD_CMD(name, "help text", func)` 宏注册命令
5. 重新 `make` 编译，构建系统会自动发现新源文件

示例：

```c
#include "shell.h"

int mycmd(int argc, char** argv) {
  printf("My custom command! argc=%d\n", argc);
  return 0;
}

ADD_CMD(mycmd, "My custom command", mycmd);
```
