ramparse Python 调试经验与方法
================================

本文档基于 ramparse v2 在 QEMU aarch64 (Linux 6.1) 环境下的实际调试经验,
总结常见的解析错误模式、诊断方法和修复策略。

## 1. 错误分类

ramparse 的解析错误可以分为以下几类:

### 1.1 TypeError: NoneType + int (最常见)

```
TypeError: unsupported operand type(s) for +: 'int' and 'NoneType'
```

**根因**: 某个函数返回了 None, 被直接用于算术运算。ramparse 中有三大 None 来源:

| 来源 | 函数 | 返回 None 的条件 |
|------|------|-----------------|
| gdb 查询失败 | `field_offset(struct, field)` | struct 或 field 在 DWARF 中不存在 |
| gdb 查询失败 | `address_of(symbol)` | symbol 在符号表中不存在 |
| 内存读取失败 | `read_word/read_u64/read_int/...` | 地址越界 / 虚拟地址转换失败 |

**排查方法**:

1. 查看完整 traceback, 定位出错行
2. 在出错行中找到 `+` 或 `-` 运算的左右两个操作数
3. 判断哪个是 None:
   - 如果涉及 `field_offset()` → struct/field 在 vmlinux 中不存在
   - 如果涉及 `address_of()` → 符号不存在
   - 如果涉及 `read_*()` → 地址无效或 v2p 转换失败
4. 用 gdb 手动验证:

   ```bash
   # 验证 struct 是否存在
   ptype/o struct xxx

   # 验证 field 是否存在
   ptype/o struct xxx | grep field_name

   # 验证符号是否存在
   print &symbol_name
   ```

### 1.2 GdbMIException

```
gdbmi.GdbMIException: 'xxx' has unknown type
```

**根因**: gdb 无法解析类型或符号。

**排查方法**:

1. 确认 vmlinux 是否包含调试信息 (文件大小 > 50MB)
2. 确认 gdb 是否能正常加载 vmlinux:
   ```bash
   aarch64-none-linux-gnu-gdb -batch -nx vmlinux -ex "ptype struct xxx"
   ```

### 1.3 IndexError / KeyError

```
IndexError: tuple index out of range
```

**根因**: `read_string()` 内部 `struct.unpack()` 解包失败, 或者 gdb 返回了意外格式。

**排查方法**: 在出错行前打印 read 函数的返回值。

## 2. 三大 None 来源详解

### 2.1 field_offset() — 结构体字段偏移

**实现** (`ramdump.py:2325`):

```python
def field_offset(self, the_type, field):
    try:
        return self.gdbmi.field_offset(the_type, field)
    except gdbmi.GdbMIException:
        pass  # 隐式返回 None
```

**返回 None 的场景**:

- vmlinux 被 strip, DWARF 信息丢失
- struct 或 field 在当前内核版本中不存在
- struct 名或 field 名拼写不匹配 (如 `page` 被重命名为 `slab`)

**代码库统计**: ~196 处调用, 仅 ~11 处有 None 保护 (5.6%)。

**调试技巧**:

```bash
# 查看完整的 struct 定义 (带偏移)
aarch64-none-linux-gnu-gdb -batch -nx vmlinux \
    -ex "ptype/o struct task_struct" \
    -ex "ptype/o struct page" \
    -ex "ptype/o struct slab"

# 查找某个 field 在哪个 struct 中
aarch64-none-linux-gnu-gdb -batch -nx vmlinux \
    -ex "ptype/o struct task_struct" | grep field_name
```

**常见内核版本变更导致 field_offset 返回 None**:

| 旧内核 (4.x/5.x) | 新内核 (6.1) | 说明 |
|-------------------|-------------|------|
| `struct kmem_cache_cpu.page` | `struct kmem_cache_cpu.slab` | 字段重命名 |
| `struct page.objects` | `struct slab.counters` | 移到 struct slab |
| `struct page.freelist` | `struct slab.freelist` | 移到 struct slab |
| `struct sched_info.last_queued` | (不存在) | struct 变为空 (size=0) |
| `struct thread_info.task` | (不存在) | thread_info 嵌入 task_struct |

### 2.2 address_of() — 符号地址查询

**实现** (`ramdump.py:2224`):

```python
def address_of(self, symbol):
    try:
        return self.gdbmi.address_of(symbol)
    except gdbmi.GdbMIException:
        pass  # 隐式返回 None
```

**返回 None 的场景**:

- 符号不存在 (如 QEMU 没有 `cpufreq_cpu_data`)
- 符号被编译器优化掉
- 模块未加载 (如 `qcom_kgsl`)

**调试技巧**:

```bash
# 在 vmlinux 中查找符号
aarch64-none-linux-gnu-nm vmlinux | grep symbol_name

# 如果 nm 找不到, 说明符号确实不存在
```

### 2.3 read_*() — 内存读取

**所有 read 方法在失败时静默返回 None, 不抛异常**:

| 方法 | 用途 | 失败原因 |
|------|------|---------|
| `read_word(addr)` | 读取 4 字节 | 地址不在 dump 范围内 |
| `read_u64(addr)` | 读取 8 字节 | 虚拟地址转物理地址失败 |
| `read_slong(addr)` | 读取有符号 long | 地址无效 |
| `read_cstring(addr, len)` | 读取 C 字符串 | 地址无效 |
| `read_physical(addr, len)` | 读取物理内存 | addr 不在任何 EBI 映射范围内 |

**特别注意**: `read_cstring()` 和 `read_binarystring()` 也有类似的 `pcpu_offset` 问题,
当传入 `cpu` 参数且 `per_cpu_offset()` 返回 None 时会崩溃。

## 3. 内核配置缺失的影响

ramparse 依赖内核内嵌的 `.config` (需要 `CONFIG_IKCONFIG=y`) 来获取 47 个 CONFIG_* 选项。
当配置不可用时:

| 函数 | 行为 | 影响 |
|------|------|------|
| `get_config_val("CONFIG_XXX")` | 返回 None | `int(None)` 崩溃 |
| `is_config_defined("CONFIG_XXX")` | 返回 False | 功能被静默禁用 |

**关键 CONFIG 选项及其影响**:

| CONFIG | 缺失时的症状 | 受影响的解析器 |
|--------|-------------|---------------|
| `CONFIG_IKCONFIG` | 所有 `get_config_val()` 返回 None | mm.py, slabinfo 等 |
| `CONFIG_PGTABLE_LEVELS` | `int(None)` → TypeError | slabinfo (vmemmap) |
| `CONFIG_ARM64_VA_BITS` | 同上 | mm.py |
| `CONFIG_THREAD_INFO_IN_TASK` | 错误判断 thread_info 位置 | taskdump, memusage |
| `CONFIG_SMP` | 跳过 per-cpu 逻辑 | taskdump, sched_info |
| `CONFIG_SCHED_INFO` | `struct sched_info` 为空 | taskdump |
| `CONFIG_SLAB_FREELIST_HARDENED` | freelist 指针计算错误 | slabinfo |
| `CONFIG_SLUB_CPU_PARTIAL` | 跳过 partial slab | slabinfo |
| `CONFIG_KALLSYMS` | 无法解析内核符号 | ftrace, stacktrace |
| `CONFIG_PRINTK_CALLER` | dmesg 格式解析错误 | dmesg |

**建议开启的内核配置**:

```makefile
# 必须开启 — 提供内核配置和调试信息
CONFIG_DEBUG_INFO=y
CONFIG_DEBUG_INFO_REDUCED=n   # 不要用精简版
CONFIG_IKCONFIG=y
CONFIG_IKCONFIG_PROC=y

# 建议开启 — 增强解析能力
CONFIG_KALLSYMS=y
CONFIG_KALLSYMS_ALL=y
CONFIG_SCHED_INFO=y
CONFIG_FTRACE=y
CONFIG_LOCKDEP=y

# 不要 strip vmlinux
# aarch64-none-linux-gnu-strip vmlinux  # 不要执行
```

## 4. 调试方法

### 4.1 使用 --debug 模式

```bash
python3 ramparse.py --ram-file ... --vmlinux ... --debug --print-tasks
```

`--debug` 模式下异常不会被静默捕获, 会直接打印完整堆栈并终止。

### 4.2 单独运行某个解析器

不需要运行全部解析器, 可以只运行出错的一个:

```bash
# 只运行 slabinfo
python3 ramparse.py --ram-file ... --vmlinux ... --slabinfo --stdout

# 只运行 print-tasks
python3 ramparse.py --ram-file ... --vmlinux ... --print-tasks --stdout
```

### 4.3 加 Python 断点调试

在 `ramparse.py` 的解析器调用处添加断点:

```bash
python3 -m pdb ramparse.py --ram-file ... --vmlinux ... --slabinfo --stdout
```

或在代码中临时添加 `breakpoint()`:

```python
# 在 parsers/slabinfo.py 的 parse() 方法中
def parse(self):
    breakpoint()  # 会在此处进入交互式调试
    self.validate_slab_cache(...)
```

### 4.4 注入调试输出

ramparse 使用 `print_out_str()` 输出内容到结果文件。临时添加调试输出:

```python
from print_out import print_out_str

def some_function(ramdump):
    addr = ramdump.address_of('some_symbol')
    print_out_str(f"DEBUG: some_symbol addr = {hex(addr) if addr else 'None'}")

    val = ramdump.read_u64(addr or 0)
    print_out_str(f"DEBUG: read value = {val}")
```

**注意**: 不要用 `print()`, 它只输出到控制台, 不会进入结果文件。
用 `print_out_str()` 可以在 `--stdout` 模式下同时看到输出。

### 4.5 手动验证 gdb 查询

在 ramparse 外部用 gdb 手动测试 struct/field/symbol 是否存在:

```bash
GDB=aarch64-none-linux-gnu-gdb

# 查看 struct 定义和字段偏移
$GDB -batch -nx binary/vmlinux -ex "ptype/o struct task_struct"

# 查看某个字段是否存在
$GDB -batch -nx binary/vmlinux -ex "ptype/o struct task_struct" | grep "stack"

# 查看符号地址
$GDB -batch -nx binary/vmlinux -ex "print &init_task"
$GDB -batch -nx binary/vmlinux -ex "print &__per_cpu_offset"

# 读取内核字符串
$GDB -batch -nx binary/vmlinux -ex "print /s (char *)&linux_banner"
```

### 4.6 快速定位出错行

当 ramparse 只输出 `"FAILED!"` 而没有详细信息时:

```bash
# 方法 1: 用 --debug 模式获取完整堆栈
python3 ramparse.py ... --debug --slabinfo --stdout 2>&1 | grep -A20 "Exception"

# 方法 2: 在 Python 中直接调用
python3 -c "
import ramparse
dump = ramparse.RamDump('binary/vmlinux', 'binary/ramdump_raw.bin',
                        0x40000000, 0x80000000)
from parsers.slabinfo import Slabinfo
Slabinfo(dump).parse()
"
```

## 5. 常见修复模式

### 5.1 None 保护 — field_offset 返回值

```python
# 修复前:
offset = ramdump.field_offset('struct xxx', 'field')
addr = base + offset  # offset 为 None 时崩溃

# 修复后:
offset = ramdump.field_offset('struct xxx', 'field')
if offset is None:
    offset = ramdump.field_offset('struct yyy', 'alt_field')  # 回退
if offset is None:
    return  # 或 print_out_str("warning...") 并 return
addr = base + offset
```

### 5.2 None 保护 — address_of 返回值

```python
# 修复前:
addr = ramdump.address_of('symbol')
val = ramdump.read_u64(addr)  # addr 为 None 时崩溃

# 修复后:
addr = ramdump.address_of('symbol')
if addr is None:
    print_out_str(f"Warning: symbol not found")
    return
val = ramdump.read_u64(addr)
```

### 5.3 None 保护 — read 函数返回值

```python
# 修复前:
val = ramdump.read_u64(addr)
result = val + 1  # val 为 None 时崩溃

# 修复后:
val = ramdump.read_u64(addr)
if val is None:
    return  # 或 continue
result = val + 1
```

### 5.4 内核版本适配 — struct 字段重命名

```python
# 当字段在不同内核版本中有不同名称时:
offset = ramdump.field_offset('struct kmem_cache_cpu', 'page')
if offset is None:
    # 内核 5.x+ 将 page 重命名为 slab
    offset = ramdump.field_offset('struct kmem_cache_cpu', 'slab')
```

### 5.5 内核版本适配 — 空结构体

```python
# 当结构体在当前版本中为空时:
offset_schedinfo = ramdump.field_offset('struct task_struct', 'sched_info')
field_val = ramdump.field_offset('struct sched_info', 'last_queued')
if offset_schedinfo is not None and field_val is not None:
    offset = offset_schedinfo + field_val
else:
    # 回退: 直接在 task_struct 中查找, 或设为 None
    offset = ramdump.field_offset('struct task_struct', 'last_queued')
```

### 5.6 无内核配置时的回退

```python
# 修复前:
nlevels = int(ramdump.get_config_val("CONFIG_PGTABLE_LEVELS"))
# get_config_val 返回 None → int(None) 崩溃

# 修复后:
nlevels = ramdump.get_config_val("CONFIG_PGTABLE_LEVELS")
if nlevels is None:
    # 从已知值推断 (如 VA_BITS)
    va_bits = ramdump.get_config_val("CONFIG_ARM64_VA_BITS") or ramdump.va_bits
    nlevels = 2 if va_bits <= 39 else (3 if va_bits <= 47 else 4)
else:
    nlevels = int(nlevels)
```

## 6. ramparse 整体架构参考

```
ramparse.py (入口, 解析命令行, 管理子解析器)
├── ramdump.py (RamDump 类)
│   ├── gdbmi.py (GDB MI 接口)
│   │   ├── field_offset()  → 返回 None 当 DWARF 缺失
│   │   ├── address_of()    → 返回 None 当符号不存在
│   │   └── read_memory()   → 返回 None 当地址无效
│   ├── mmu.py (虚拟地址 → 物理地址转换)
│   ├── mm.py (页表管理, vmemmap)
│   └── read_word/read_u64/... → 全部返回 None 当读取失败
├── print_out.py (输出管理)
│   ├── print_out_str()     → 输出到文件
│   └── print_out_exception() → 打印 traceback
├── parser_util.py (@register_parser 注册机制)
└── parsers/ (73 个子解析器)
```

## 7. 可用的子解析器完整列表 (73 个)

ramparse 共注册了 73 个子解析器, 当前 `ramparse.sh` 启用了 11 个:

| # | 参数 | 功能 | 当前启用 |
|---|------|------|---------|
| 1 | `--sched-info` | 调度器参数状态 | Y |
| 2 | `--watchdog` | Watchdog 信息 | N (Qualcomm) |
| 3 | `--print-tasks` | 任务栈打印 | Y |
| 4 | `--print-tasks-timestamps` | 任务调度统计 | N |
| 5 | `--check-for-panic` | Panic 检测 | Y |
| 6 | `--dmesg` | 内核日志 | Y |
| 7 | `--kbootlog` | 启动日志 | N |
| 8 | `--print-rtb` | RTB 信息 | N (Qualcomm) |
| 9 | `--print-irqs` | 中断信息 | Y |
| 10 | `--print-workqueues` | 工作队列 | N |
| 11 | `--print-runqueues` | 运行队列 | Y |
| 12 | `--print-memstat` | 内存统计 | N |
| 13 | `--print-memory-info` | 内存使用 | Y |
| 14 | `--print-vmstats` | vmstat/zoneinfo | N |
| 15 | `--slabinfo` | Slab 信息 | Y |
| 16 | `--slabsummary` | Slab 汇总 | N |
| 17 | `--slabpoison` | Slab 毒化检查 | N |
| 18 | `--print-reserved-mem` | 保留内存 | N |
| 19 | `--print-cma-areas` | CMA 区域 | N |
| 20 | `--print-softirq-stat` | 软中断统计 | N |
| 21 | `--print-qsee-log` | QSEE 日志 | N (Qualcomm) |
| 22 | `--print-pagetypeinfo` | 页类型信息 | N |
| 23 | `--print-pagetracking` | 页追踪 | N |
| 24 | `--print-pagealloccorruption` | 页分配损坏 | N |
| 25 | `--print-zram` | zram 数据 | N |
| 26 | `--print-lsof` | 打开的文件 | N |
| 27 | `--print-vmalloc` | Vmalloc 信息 | Y |
| 28 | `--timer-list` | 定时器列表 | N |
| 29 | `--clock-dump` | 时钟转储 | N (Qualcomm) |
| 30 | `--regulator` | 电压调节器 | N (Qualcomm) |
| 31-73 | ... | 其他 Qualcomm 专用 | N |

**QEMU 环境可额外尝试的解析器**:

```bash
# 这些解析器不依赖 Qualcomm 专用硬件, 可能在 QEMU 上也能工作
python3 ramparse.py ... --print-workqueues --stdout
python3 ramparse.py ... --print-memstat --stdout
python3 ramparse.py ... --print-vmstats --stdout
python3 ramparse.py ... --print-reserved-mem --stdout
python3 ramparse.py ... --timer-list --stdout
python3 ramparse.py ... --print-kconfig --stdout
python3 ramparse.py ... --dtb --stdout
python3 ramparse.py ... --print-lsof --stdout
```

开启 `CONFIG_IKCONFIG=y` 后可进一步启用:

```bash
python3 ramparse.py ... --print-kconfig --stdout
python3 ramparse.py ... --lockdep-heldlocks --stdout
python3 ramparse.py ... --uftrace --stdout
```
