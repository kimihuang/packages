# Boot-wrapper-aarch64 构建与 QEMU 启动调试记录

## 1. 构建环境加载路径问题

**现象**: 直接运行 `build.sh` 时报错 `板卡配置文件不存在`，且工具链路径指向 `boot-wrapper-aarch64/tools/...`（错误路径）。

**原因**: `envsetup.sh` 使用 `PROJECT_ROOT=$(pwd)` 设置项目根目录。当 `build.sh` 从其他目录执行时，`$(pwd)` 不是项目根目录，导致所有后续路径（工具链、板卡配置等）全部错误。

**解决**: `build.sh` 先通过向上查找 `build/envsetup.sh` 确定项目根目录，然后 `cd` 到项目根目录再 `source envsetup.sh`，最后 `cd` 回原目录：

```bash
cd "$PROJECT_ROOT"
source "$PROJECT_ROOT/build/envsetup.sh"
cd "$_old_pwd"
```

## 2. 缺少 autotools 依赖

**现象**: `autoreconf: command not found`

**原因**: boot-wrapper 使用 autotools 构建系统（`configure.ac` + `Makefile.am`），需要安装 `autoconf`、`automake`、`libtool`。

**解决**:
```bash
sudo apt-get install autoconf automake libtool
```

## 3. QEMU 启动 boot-wrapper 无输出

**现象**: `qemu-system-aarch64 -M virt -kernel linux-system.axf` 启动后没有任何输出，超时终止。

**原因**: boot-wrapper 的 `_start` 入口（`arch/aarch64/boot.S`）会检查当前异常级别（Exception Level）：

```asm
mrs x0, CurrentEL
cmp x0, #0xc        // EL3?
b.eq reset_at_el3
cmp x0, #0x8        // EL2?
b.eq reset_at_el2
b .                 // EL1/EL0 不支持，死循环
```

QEMU virt 默认不带 `secure=on`，CPU 启动异常级别取决于 QEMU 版本和配置。通过 `-d in_asm` 追踪发现执行到了 `b .`（地址 `0x14`），说明当前 EL 既不匹配 EL3 也不匹配 EL2，CPU 进入了死循环。

**解决**: 添加 `-M virt,secure=on`，让 QEMU 从 EL3 启动：

```bash
qemu-system-aarch64 -M virt,secure=on ...
```

## 4. Boot-wrapper 跳入内核后内核挂起

**现象**: 添加 `secure=on` 后，boot-wrapper 正常初始化（输出 "Boot-wrapper v0.2"、"All CPUs initialized. Entering kernel..."），但跳入内核后无任何输出。

**原因**: 仅添加 `secure=on` 时，QEMU 不启用 EL2 虚拟化支持。boot-wrapper 从 EL3 通过 `eret` 降级进入内核时，内核运行在 EL2，但 QEMU 未正确配置虚拟化扩展，导致内核无法正常运行。

**解决**: 同时添加 `virtualization=on`：

```bash
qemu-system-aarch64 -M virt,secure=on,virtualization=on ...
```

## 5. QEMU `-machine` 参数说明

| 参数 | 作用 |
|------|------|
| `secure=on` | 启用 TrustZone，CPU 从 EL3 启动（boot-wrapper 要求 EL3 或 EL2 入口） |
| `virtualization=on` | 启用 EL2 虚拟化扩展（Linux 内核需要运行在 EL2） |

两个参数缺一不可：
- 只有 `secure=on` → boot-wrapper 初始化成功，但内核挂起
- 都不加 → boot-wrapper 无法识别当前 EL，直接死循环

## 6. 与 QEMU 原生启动方式的对比

| 项目 | QEMU 原生（`boot_kernel`） | boot-wrapper |
|------|---------------------------|--------------|
| 启动方式 | `-kernel Image -initrd rootfs.cpio -dtb xxx.dtb -append "cmdline"` | `-kernel linux-system.axf`（单文件，内核+DTB+initrd 打包） |
| EL 入口 | QEMU 内置 loader 处理，内核从 EL2 直接启动 | boot-wrapper 在 EL3 初始化 PSCI/GIC 后降级到内核 |
| PSCI | QEMU 内置 PSCI 实现 | boot-wrapper 提供的 PSCI 实现（smc 调用） |
| QEMU 参数 | `-M virt`（默认即可） | `-M virt,secure=on,virtualization=on` |
| DTB 来源 | 外部传入或 QEMU 自动生成 | boot-wrapper 从源 DTB 修改后嵌入（添加 chosen/psci 节点） |

## 7. 最终 QEMU 启动命令

```bash
qemu-system-aarch64 \
    -M virt,secure=on,virtualization=on \
    -cpu cortex-a57 \
    -smp 4 \
    -m 1024 \
    -nographic \
    -kernel linux-system.axf
```

## 8. 成功启动日志关键节点

```
Boot-wrapper v0.2              # boot-wrapper 在 EL3 初始化
Entered at EL3
Memory layout:                 # 内存布局打印
CPU0-3: initializing...        # PSCI smc 唤醒次级 CPU
All CPUs initialized. Entering kernel...

Booting Linux on physical CPU 0x0   # 内核启动（运行在 EL2）
PSCI: Using PSCI v0.1 Function IDs # 使用 boot-wrapper 提供的 PSCI
CPU: All CPU(s) started at EL2
Run /init as init process            # rootfs 正常挂载
#                                  # 进入 shell
```
