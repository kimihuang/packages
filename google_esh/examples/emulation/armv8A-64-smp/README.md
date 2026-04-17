# ARMv8-A 64-bit 多核 (Cortex-A72 SMP) QEMU 仿真编译运行指南

## 概述

`examples/emulation/armv8A-64-smp` 是 Embedded Shell (esh) 项目的 **ARMv8-A 64-bit 多核**示例，基于 4 核 **Cortex-A72** 处理器，在 **QEMU virt** 机器上仿真运行。相比单核版本 (`armv8A-64`)，本示例实现了完整的多核启动、自旋锁同步、多核 printf 以及跨核函数调度机制。

## 与单核版本 (armv8A-64) 的区别

| 特性 | armv8A-64（单核） | armv8A-64-smp（多核） |
|------|:--:|:--:|
| CPU 核心数 | 1 | 4 |
| 启动文件 | shell/startup/aarch64.S | platform/aarch64-multicore.S |
| RAM 起始地址 | 0x40000000 | 0x40100000 |
| RAM 大小 | 16 KB | 256 KB |
| 代码大小 | ~9.8 KB | ~17 KB |
| QEMU 加载方式 | `-kernel` | `-bios` + `-device loader` |
| 自旋锁 | 无 | 有（ticket lock） |
| 多核 printf | 无 | 有（spinlock 保护） |

## 目录结构

```
examples/emulation/armv8A-64-smp/
├── Makefile                  # 构建配置
├── hello.c                   # 示例命令：hello
├── platform/
│   ├── platform.c            # 平台初始化（UART + printf 锁初始化）
│   ├── aarch64-multicore.S   # 多核启动汇编（栈设置、核间同步）
│   └── uart/
│       ├── uart.h            # PL011 UART 驱动头文件
│       └── uart.c            # UART 驱动实现
├── multicore/
│   ├── multicore.h           # 多核调度接口
│   └── multicore.c           # 多核函数调度、run/run_task/run_all 命令
├── spinlock/
│   ├── spinlock.h            # 自旋锁接口（ticket lock）
│   ├── spinlock.c            # 自旋锁实现（含死锁检测）
│   └── lock.S                # LDAXR/STXR 汇编原语（acquire/release/mycpu）
└── printf/
    ├── multicore_printf.h    # 多核安全 printf 宏
    └── multicore_printf.c    # printf 锁初始化
```

## 环境依赖

### 必需工具

| 工具 | 用途 |
|------|------|
| `make` | 构建系统 |
| `aarch64-none-linux-gnu-gcc` | AArch64 交叉编译器 |
| `qemu-system-aarch64` | QEMU AArch64 系统仿真器 |

### 安装依赖

```bash
sudo apt install -y make binutils gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu qemu-system-aarch64
```

> Makefile 中 `TOOLCHAIN_PREFIX` 已设置为 `aarch64-none-linux-gnu-`。

### 可选工具（GDB 调试）

```bash
sudo apt install -y gdb-multiarch
wget -P ~ https://git.io/.gdbinit
pip3 install pygments
```

## 编译

```bash
cd examples/emulation/armv8A-64-smp
make
```

编译输出示例：

```
Detected Configuration
*
├── PROJECT             : shell
├── TOOLCHAIN           : aarch64-none-linux-gnu-
├── STARTUP             : aarch64-multicore
├── RAM BASE            : 0x40100000
├── RAM SIZE            : 0x40000
...
compiling platform/platform.c
compiling platform/uart/uart.c
compiling multicore/multicore.c
compiling printf/multicore_printf.c
compiling spinlock/spinlock.c
assembling spinlock/lock.S
assembling platform/aarch64-multicore.S
generating shell.elf
Size Report:
   text    data    bss     dec     hex
  16205     449    364   17018   427a   (TOTALS)
Memory region         Used Size  Region Size  %age Used
             RAM:       16836 B       256 KB      6.42%
generating shell.bin
Done!
```

## 运行

```bash
make run
```

启动后会看到 4 个核心依次启动的输出：

```
Core 0 Starting Core 1.
Core 0 Starting Core 2.
Core 0 Starting Core 3.
Core 1 Bootup done
Core 2 Bootup done
Core 0 Bootup done.
Core 3 Bootup done
Build: [7a2a1d3:1ecb507] - [user@host] - Apr 17 2026 - 14:24:31
#
```

### 退出 QEMU

按 `Ctrl+A`，然后按 `X` 退出。

## Shell 命令

### 通用命令

| 命令 | 说明 |
|------|------|
| `help` | 显示所有可用命令 |
| `hello` | 回显输入参数 |
| `version` | 显示构建信息 |

### 多核专用命令

| 命令 | 说明 |
|------|------|
| `SecondaryBoot` | 启动所有从核（启动时自动执行） |
| `run all <func> [args...]` | 在所有核心上并行执行指定函数 |
| `run <core_id> <func> [args...]` | 在指定核心上执行函数 |
| `run_task <core_id> <func> [<core_id> <func> ...]` | 不同核心执行不同函数 |
| `run_all <func>` | 所有核心执行同一函数（无参数） |
| `calc <num1> <num2> <+/\-/\*>` | 四则运算示例 |
| `Function1` | 测试函数 1 |
| `Function2` | 测试函数 2 |

### 交互示例

```
# run all Function1
Core 0 starting execution
Core:0 executing fun1
...
Core 1 starting execution
Core:1 executing fun1
...

# run 2 Function2
Starting Core 2....
Core 2 starting execution
Core:2 executing fun2
...

# run_task 1 Function1 2 Function2
Starting Core 1
Starting Core 2
Starting Execution....
Core 1 starting execution
Core:1 executing fun1
Core 2 starting execution
Core:2 executing fun2
...
```

## 核心架构

### 多核启动流程 (`aarch64-multicore.S`)

1. 每个核心通过 `mpidr_el1` 寄存器获取自身 Core ID（0~3）
2. 每个核心分配独立栈空间（每核 1024 字节）
3. **Core 0**：跳转到 `prompt` 进入 shell 主循环
4. **Core 1~3**：进入 WFE (Wait For Event) 自旋循环，等待 Core 0 唤醒

Core 0 通过以下机制唤醒从核：
- 将目标函数地址写入 `spin_cpu[cpuid]`
- 执行 `SEV` (Set Event) 指令
- 从核被唤醒后跳转到该函数地址执行，完成后回到自旋循环

### 自旋锁 (`spinlock/`)

采用 **Ticket Lock** 算法实现互斥，基于 ARMv8 LDAXR/STXR 排他指令对：

```
acquire: 取号 → 等待叫号（WFE 循环检查 curr_ticket）
release: 递增 curr_ticket → 唤醒等待核（STLR 产生 event）
```

- 不可重入：同一核心重复加锁会触发死锁检测并冻结该核心
- 使用场景：保护多核 printf，防止多个核心同时输出导致字符交错

### 多核 Printf (`printf/multicore_printf.h`)

```c
#define multicore_printf(fmt, ...) \
  do {                             \
    if (pr.locking) {              \
      spin_lock(&pr.lock);         \
    }                              \
    printf(fmt, ##__VA_ARGS__);    \
    if (pr.locking) {              \
      spin_unlock(&pr.lock);       \
    }                              \
  } while (0)
```

通过 `pr.locking` 开关控制是否启用锁保护，可在需要时关闭以测试并发问题。

### 核心状态管理

- `spin_cpu[]`：每核的函数地址槽，Core 0 写入函数地址，从核读取后跳转执行
- `core_available[]`：每核的可用状态标志，执行中为 0，空闲为 1
- `wait_for_cores()`：Core 0 等待所有从核完成当前任务后才返回 shell 提示符

## QEMU 启动参数说明

```makefile
qemu-system-aarch64 -smp 4 -M virt,secure=on,virtualization=on \
                     -cpu cortex-a72 -nographic \
                     -bios shell.elf \
                     -device loader,addr=0x40100000,cpu-num=0 \
                     -device loader,addr=0x40100000,cpu-num=1 \
                     -device loader,addr=0x40100000,cpu-num=2 \
                     -device loader,addr=0x40100000,cpu-num=3 \
                     -device loader,file=shell.elf
```

| 参数 | 说明 |
|------|------|
| `-smp 4` | 模拟 4 个 CPU 核心 |
| `-bios shell.elf` | 所有核心从同一入口点启动 |
| `-device loader,addr=0x40100000,cpu-num=N` | 每个核心重置到 RAM 起始地址 |
| `-device loader,file=shell.elf` | 将 ELF 加载到内存 |

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

## Makefile 配置说明

```makefile
TOOLCHAIN_PREFIX = aarch64-none-linux-gnu-    # 交叉编译工具链前缀
OPTIMIZATION     = g                           # 优化级别 (-Og)
STARTUP          = aarch64-multicore           # 多核启动汇编文件
RAM_BASE_PHYSICAL = 0x40100000                # RAM 起始物理地址
RAM_SIZE          = 0x40000                    # RAM 大小（256KB）
LD_FLAGS          = --no-warn-rwx-segments     # 链接器标志
SHELL_ROOT        = ../../../shell             # shell 源码相对路径
```

## 添加自定义多核命令

1. 在项目目录创建 `.c` 文件
2. 包含 `#include "multicore_printf.h"` 和 `#include "shell.h"`
3. 使用 `ADD_CMD()` 或 `AUTO_CMD()` 注册命令
4. 使用 `multicore_printf()` 替代 `printf()` 进行安全输出
5. 通过 `run` 命令在指定核心或所有核心上执行

示例：

```c
#include "multicore_printf.h"
#include "shell.h"

int my_task(int argc, char *argv[]) {
  // argv[0] 是函数名，argv[1] 起是用户参数
  multicore_printf("Core task running with argc=%d\n", argc);
  return 0;
}

ADD_CMD(my_task, "My multicore task", my_task);
```

使用方式：

```
# 在所有核心上执行
run all my_task

# 在核心 2 上执行
run 2 my_task
```
