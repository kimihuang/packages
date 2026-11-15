# Panic / Oops 分析 SOP

> 目标：内核 panic 或 oops 后，系统性定位根因。本文档沉淀「现象 → 定位 → 根因」的标准套路，配合 `tools/panic/`、`tools/fault/` 工具使用。
>
> 以下 `DO` 命令假设已在 TRACE32 PowerView 命令行 `CD` 到本项目根目录（或用绝对路径 DO）。

---

## 0. 前置

- 环境已准备：`DO env/sim_armv8_setup.cmm`（vmlinux + awareness 就绪）
- 调试时先关 watchdog，避免干扰：`DO tools/common/disable_watchdogs.cmm`

---

## 1. 抓现场（最重要，先别乱动）

panic 发生瞬间的现场是唯一证据，先落盘再分析。

**主动复现**（预先埋断点）：

```cmm
DO tools/panic/trap_panic.cmm   ; 断 panic/die/__do_kernel_fault
Go                                 ; 复现 panic
; 命中后：
DO tools/panic/save_ramdump.cmm ; 生成 restore_<ts>.cmm + 内存/寄存器/翻译文件
```

**被动发现**（已停在 panic 处）：

```cmm
DO tools/panic/save_ramdump.cmm ; 直接保存当前现场
```

---

## 2. 读内核日志

Linux awareness 提供的 `TASK.DMESG` 可直接读内核环形缓冲：

```cmm
TASK.DMESG    ; 查看 panic 前的内核日志（oops 信息、调用栈、寄存器 dump）
```

重点关注：
- oops 类型（`Unable to handle kernel paging request` / `BUG` / `Kernel panic`）
- fault 地址与访问类型（读/写、user/kernel）
- oops 打印的寄存器现场和 call trace

---

## 3. 定位 fault 现场（段错误类）

若为段错误/页错误，用官方 `segv` 脚本把寄存器现场复原到 fault 发生点：

```cmm
DO tools/fault/trap_segv.cmm   ; 第一次：设断点
Go
DO tools/fault/trap_segv.cmm   ; 命中后第二次：复原现场到 fault 点
```

复原后即可看到 fault 发生时的 PC、X0-X30、SP，以及是哪个函数触发了非法访问。

---

## 4. 栈回溯

交互式下用 `Frame.View` 查看调用栈，或按核查看：

```cmm
DO tools/hang/show_stacks.cmm   ; 各核 PC + 线程列表
Frame.View                         ; 当前核调用栈
```

结合 oops 里的 call trace，确认 panic 的调用路径（是哪个驱动/子系统传入）。

---

## 5. 根因分析（基于符号 + 源码）

- 用 PC / call trace 反查符号：`sYmbol.NAME(<addr>)` 或直接 `List` 反汇编
- 检查关键数据结构（task_struct、page、锁等），用 `Var.View` / `Var.VALUE`
- 若与内存相关：检查越界/踩踏（`tools/mem/` 预留 watchpoint 工具）
- 若与并发相关：检查锁状态、各核是否死锁（`show_stacks` 对比各核停点）

---

## 6. 验证修复

- 改代码 → 重编 vmlinux → 更新 `tools/common/config.cmm` 的 `&STB_VMLINUX`
- 重跑 `cases/boot_to_shell.cmm` + 复现 case，确认不再 panic

---

## 附：常见 oops 类型速查

| oops 信息 | 常见根因方向 |
|-----------|-------------|
| `Unable to handle kernel paging request` | 空指针/野指针解引用、内存越界 |
| `Unable to handle kernel NULL pointer dereference` | 空指针（最典型） |
| `BUG: scheduling while atomic` | 原子上下文里睡眠/调度 |
| `BUG: soft lockup` | 软锁（关中断死循环） |
| `BUG: spinlock recursion` / 死锁 | 锁误用 |
| `Kernel panic - not syncing: VFS` | 根文件系统挂载失败 |
| `Out of memory` / OOM | 内存耗尽（`tools/mem/` 场景） |
