# CMake 构建系统

Embedded Shell (esh) 支持 CMake 构建，与现有 Makefile 构建系统行为一致，产生相同的输出文件。

## 快速开始

```bash
# 配置（从项目根目录执行）
cmake -B build/armv8A-64 -S examples/emulation/armv8A-64

# 编译
cmake --build build/armv8A-64

# 产物在 build/armv8A-64/ 下：
#   shell.elf       ELF 二进制
#   shell.bin       纯二进制
#   shell.elf.map   链接映射
#   shell.elf.lst   反汇编列表
```

## 已验证的示例

| 示例 | 工具链 | bin 大小 | RAM 占用 | 状态 |
|------|--------|----------|----------|------|
| armv8A-64 (Cortex-A72 单核) | aarch64-none-linux-gnu- | 9660 B | 16 KB (58.96%) | 通过 |
| armv8A-64-smp (Cortex-A72 四核) | aarch64-none-linux-gnu- | 16836 B | 256 KB (6.42%) | 通过 |

## 所有示例 CMake 支持

所有示例均已提供 CMakeLists.txt（与对应 Makefile 参数一致）：

| 示例 | 工具链 | 特殊配置 |
|------|--------|----------|
| armv7A-32-Rpi2 | arm-none-eabi- | -lgcc |
| armv7M-32 | arm-none-eabi- | -mcpu=cortex-m3, STACK_START |
| armv8A-32-RPi3b | aarch64-linux-gnu- | — |
| armv8A-64 | aarch64-none-linux-gnu- | — |
| armv8A-64-smp | aarch64-none-linux-gnu- | — |
| armv8M-32 | arm-none-eabi- | ROM+RAM (two-seg), STACK_START, STACK_START2 |
| riscv-64 | riscv64-linux-gnu- | — |
| hifive | riscv64-linux-gnu- | ROM+RAM, SHELL_LITE, -m elf32lriscv |
| nucleo-f401re | arm-none-eabi- | ROM+RAM, -mcpu=cortex-m4 |
| tiva-c | arm-none-eabi- | ROM+RAM, -mcpu=cortex-m4 |

使用方式：
```bash
cmake -B build/<example> -S examples/<category>/<example>
cmake --build build/<example>
```

## 架构

```
shell/cmake/
└── esh.cmake                      # 共享构建模块（镜像 shell/Makefile + build-magic/*.mk）

examples/emulation/armv8A-64/
└── CMakeLists.txt                  # 最小化项目配置 → include(../../../shell/cmake/esh.cmake)

examples/emulation/armv8A-64-smp/
└── CMakeLists.txt                  # 同上模式
```

每个示例目录包含一个最小化的 `CMakeLists.txt`，设置项目变量后 `include` 共享模块。这与 Makefile 的 `-include $(SHELL_ROOT)/Makefile` 模式一致。

## CMakeLists.txt 模板

```cmake
cmake_minimum_required(VERSION 3.16)

# ---- 必需变量 ----
set(TOOLCHAIN_PREFIX  "<prefix>")        # 交叉编译工具链前缀
set(STARTUP           "<startup_name>")   # 启动文件名（不含 .S 后缀）
set(RAM_BASE_PHYSICAL "0x<addr>")         # RAM 起始物理地址（hex）
set(RAM_SIZE          "0x<size>")         # RAM 大小（hex）

# ---- 可选变量 ----
set(OPTIMIZATION      "g")                # 优化级别：0/1/2/3/s/g（默认 0）
set(LD_FLAGS          "--no-warn-rwx-segments")
# set(ROM_BASE_PHYSICAL "0x<addr>")       # 设置后启用 two-seg.ld (ROM+RAM)
# set(ROM_SIZE          "0x<size>")       # ROM 大小
# set(USER_LAYOUT_FILE  "<path>.ld")      # 自定义链接脚本（覆盖上述所有）
# set(SHELL_LITE        1)                # 裁剪 shell 功能
# set(EXTERN_SRC        "dir1;dir2")      # 额外源码搜索目录
# set(IGNORE_SRC_PATH   "dir1;dir2")      # 排除的源码路径

# ---- 工具链（必须在 project() 之前） ----
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_C_COMPILER   "${TOOLCHAIN_PREFIX}gcc")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_PREFIX}g++")
set(CMAKE_ASM_COMPILER "${TOOLCHAIN_PREFIX}gcc")
set(CMAKE_LINKER       "${TOOLCHAIN_PREFIX}ld")

project(shell LANGUAGES C ASM)

include(../../../shell/cmake/esh.cmake)
```

## 变量参考

### 必需变量

| 变量 | 说明 | 示例 |
|------|------|------|
| `TOOLCHAIN_PREFIX` | 交叉编译工具链前缀 | `"aarch64-none-linux-gnu-"` |
| `STARTUP` | 启动汇编文件名（不含 `.S`） | `"aarch64"` / `"aarch64-multicore"` |
| `RAM_BASE_PHYSICAL` | RAM 起始物理地址 (hex) | `"0x40000000"` |
| `RAM_SIZE` | RAM 大小 (hex) | `"0x4000"` |

### 可选变量

| 变量 | 默认值 | 说明 |
|------|--------|------|
| `PROJECT` | `"shell"` | 输出文件名 |
| `OPTIMIZATION` | `"0"` | GCC 优化级别 |
| `ROM_BASE_PHYSICAL` | 空 | 设置后使用 two-seg.ld (ROM+RAM) |
| `ROM_SIZE` | 空 | ROM 大小（设置 ROM_BASE 时必需） |
| `USER_LAYOUT_FILE` | 空 | 自定义链接脚本路径 |
| `LD_FLAGS` | 空 | 额外链接器标志 |
| `ASM_FLAGS` | 空 | 额外汇编器标志 |
| `DEFINES` | 空 | 额外预处理器定义 |
| `EXTERN_SRC` | 空 | 额外源码搜索目录（分号分隔） |
| `IGNORE_SRC_PATH` | 空 | 排除的源码路径（分号分隔） |
| `SHELL_LITE` | `0` | 设为 1 裁剪 shell 功能 |
| `ECHO_INIT_VALUE` | `"0x1"` | 输入回显初始值 |
| `PROMPT` | `"#"` | Shell 提示符字符 |

## 与 Makefile 的对应关系

| Makefile 机制 | CMake 对应 |
|---------------|-----------|
| `-include $(SHELL_ROOT)/Makefile` | `include(../../../shell/cmake/esh.cmake)` |
| `build-magic/defaults.mk` | `esh.cmake` Phase 1-2（验证 + 默认值） |
| `build-magic/find-source.mk` | `esh.cmake` Phase 5（`file(GLOB_RECURSE)` + `find`） |
| `build-magic/find-headers.mk` | `esh.cmake` Phase 6（`file(GLOB_RECURSE)` + 去重） |
| `build-magic/configuration.mk` | `esh.cmake` Phase 10（`message(STATUS ...)`） |
| `$(TOOLCHAIN_PREFIX)ld -T ...` | `add_custom_command(COMMAND ${CMAKE_LINKER} ...)` |
| `$(TOOLCHAIN_PREFIX)objcopy` | `add_custom_command(COMMAND ${OBJCOPY} ...)` |
| `$(TOOLCHAIN_PREFIX)objdump` | `add_custom_command(COMMAND ${OBJDUMP} ...)` |
| `VPATH` | CMake 的 OBJECT library 自动处理 |
| `--jobs=$(nproc)` | CMake 默认并行构建 |

## 源码发现规则

与 Makefile 完全一致：

| 类型 | 搜索范围 |
|------|----------|
| C/C++ 源码 (`.c`/`.cpp`) | 项目目录 + `shell/` + `EXTERN_SRC` |
| 汇编 (`.S`) | 仅项目目录（不含 `shell/`） |
| 启动文件 | 项目目录优先，`shell/` 兜底 |
| 头文件 (`.h`/`.hpp`) | 项目目录 + `shell/` + `EXTERN_SRC`（提取唯一目录作为 `-I`） |

启动文件通过 `find` 命令按名称搜索（与 Makefile 行为一致），并自动从汇编列表中排除以避免重复编译。

## 编译标志

与 Makefile 一致：

```
-Wall -O<OPTIMIZATION> -nostdlib -nostartfiles -ffreestanding -ggdb
```

C++ 额外添加 `-Wwrite-strings`。

自动注入的预处理器定义：
- `-DRAM_BASE_PHYSICAL=<addr>`
- `-DECHO_INIT_VALUE=<val>`
- `-D__PROMPT__=<char>`（通过 `target_compile_options` 传递以正确处理 `#`）
- `-D__BUILD_USER__=<user>`
- `-D__BUILD_HOST__=<host>`
- `-D__SHELL_VERSION__=<hash>`
- `-D__USER_REPO_VERSION__=<hash>`

## 链接器脚本选择

自动选择，与 Makefile 逻辑一致：

- 有 `USER_LAYOUT_FILE` → 使用自定义脚本，无 `--defsym`
- 有 `ROM_BASE_PHYSICAL` → `shell/scatter/two-seg.ld` + `__ROM_BASE__`/`__ROM_SIZE__`
- 默认 → `shell/scatter/one-seg.ld` + `__RAM_BASE__`/`__RAM_SIZE__`

## 在 QEMU 中运行 CMake 构建产物

```bash
# armv8A-64（单核）
qemu-system-aarch64 -M virt,secure=on,virtualization=on \
    -cpu cortex-a72 -nographic \
    -kernel build/armv8A-64/shell.elf

# armv8A-64-smp（四核）
qemu-system-aarch64 -smp 4 -M virt,secure=on,virtualization=on \
    -cpu cortex-a72 -nographic \
    -bios build/armv8A-64-smp/shell.elf \
    -device loader,addr=0x40100000,cpu-num=0 \
    -device loader,addr=0x40100000,cpu-num=1 \
    -device loader,addr=0x40100000,cpu-num=2 \
    -device loader,addr=0x40100000,cpu-num=3 \
    -device loader,file=build/armv8A-64-smp/shell.elf
```

退出 QEMU：`Ctrl+A` 然后 `X`。

## 已知差异（与 Makefile 构建）

- **产物位置**：Makefile 将 `.elf`/`.bin` 放在示例目录下，CMake 放在 build 目录下
- **bin 内容**：大小完全一致，但嵌入的 git hash 字符串可能因构建目录不同而略有差异（不影响功能）
- **对象文件后缀**：CMake 使用 `.obj`，Makefile 使用 `.o`（仅文件名，不影响链接）
