# demo-dmabuf Buildroot 包集成总结

## 概述

将 `src/packages/demo_dmabuf` 源码集成为 Buildroot 外部包 `demo-dmabuf`，
包含 3 个内核模块 + 3 个用户空间工具的构建和安装。

## 构建验证

```bash
cd /home/lion/workdir/sourcecode/quantum_main
source build/envsetup.sh && lunch 2
make linux-dirclean && make linux-rebuild    # 启用 DMA-BUF 内核支持
make demo-dmabuf-rebuild                      # 构建所有模块 + 用户空间
```

结果：**通过**（3 个 .ko + 3 个二进制文件安装到 target）

## 文件变更清单

### 1. 新增文件（Buildroot 包）

| 文件 | 说明 |
|------|------|
| `board/quantum/br2_external/package/demo-dmabuf/demo-dmabuf.mk` | Buildroot recipe：混合构建内核模块 + 用户空间程序 |
| `board/quantum/br2_external/package/demo-dmabuf/Config.in` | Menuconfig 配置项：`BR2_PACKAGE_DEMO_DMABUF` |
| `board/quantum/linux/patches/0001-dma-heap-export-dma_heap_add-for-kernel-modules.patch` | 内核补丁：导出 `dma_heap_add` 供外部模块使用 |

### 2. 修改文件（Buildroot 配置）

| 文件 | 变更内容 |
|------|---------|
| `board/quantum/br2_external/Config.in` | 添加 `source` 行引入 `demo-dmabuf/Config.in` |

### 3. 修改文件（内核配置）

| 文件 | 变更内容 |
|------|---------|
| `board/quantum/linux/configs/quantum_qemu_defconfig` | 启用 `CONFIG_SYNC_FILE=y`、`CONFIG_DMABUF_HEAPS=y`、`CONFIG_DMABUF_HEAPS_CMA=y`、`CONFIG_DMABUF_HEAPS_SYSTEM=y` |

### 4. 修改文件（源码 - Linux 6.1 兼容性适配）

#### 4.1 内核模块

| 文件 | 变更内容 |
|------|---------|
| `src/packages/demo_dmabuf/demo_heap/demo_heap.c` | 1. `allocate()` 返回类型：`int` → `struct dma_buf *`，参数：`unsigned int` → `unsigned long`（匹配 Linux 6.1 `dma_heap_ops`）<br>2. 错误返回：`return -ENOMEM` → `return ERR_PTR(-ENOMEM)`<br>3. 移除未使用变量（`vaddr`、`pfn`）<br>4. 添加 `MODULE_IMPORT_NS(DMA_BUF)` |
| `src/packages/demo_dmabuf/demo_exporter/demo_exporter.c` | 添加 `MODULE_IMPORT_NS(DMA_BUF)` |
| `src/packages/demo_dmabuf/demo_exporter/demo_timer_fence.c` | `dma_buf_get(dmabuf)` → `get_dma_buf(dmabuf)`（Linux 6.1 中 `dma_buf_get` 接收 `int fd`，获取 `struct dma_buf *` 引用需用 `get_dma_buf`） |
| `src/packages/demo_dmabuf/demo_importer/demo_importer.c` | 1. `dma_buf_map_attachment_unlocked()` → `dma_buf_map_attachment()`<br>2. `dma_buf_unmap_attachment_unlocked()` → `dma_buf_unmap_attachment()`（Linux 6.1 无 `_unlocked` 变体）<br>3. 添加 `MODULE_IMPORT_NS(DMA_BUF)` |

#### 4.2 用户空间程序

| 文件 | 变更内容 |
|------|---------|
| `src/packages/demo_dmabuf/demo_app/demo_app.c` | 内联定义 `struct dma_heap_allocation_data` 和 `DMA_HEAP_IOCTL_ALLOC`（交叉编译工具链 sysroot 无此头文件）；补充 `.heap_flags = 0` 初始化 |
| `src/packages/demo_dmabuf/demo_app/sync_file_info.c` | 添加 `#include <linux/types.h>`，提供 `__u64`/`__u32`/`__s32` 类型定义 |
| `src/packages/demo_dmabuf/demo_test/test_dmabuf.c` | 同 `demo_app.c`：内联定义 `struct dma_heap_allocation_data` + `DMA_HEAP_IOCTL_ALLOC` |

## 构建产物

### 内核模块（安装至 `target/lib/modules/6.1.0/extra/`）

| 模块 | 说明 |
|------|------|
| `demo_heap.ko` | 自定义 DMA-BUF heap 驱动（`/dev/dma_heap/demo`），使用 `vmalloc_user()` 分配非连续内存 |
| `demo_exporter.ko` | DMA-BUF 导出器（`/dev/demo_exp`），基于 hrtimer 的 `dma_fence` 异步填充 |
| `demo_importer.ko` | DMA-BUF 导入器（`/dev/demo_imp`），校验和处理 + completion fence 通知 |

### 用户空间程序（安装至 `target/usr/bin/`）

| 二进制 | 说明 |
|--------|------|
| `demo_dmabuf_app` | 完整 9 步 DMA-BUF 流水线演示（分配、mmap、CPU 同步、DMA 填充、fence 等待、导入、处理、验证） |
| `sync_file_info` | 查询并打印 sync_file fence 信息 |
| `test_dmabuf` | 自动化测试套件（13 个测试用例：heap 分配、mmap、CPU 同步、exporter、importer、sync_file、隐式同步、多消费者、超时、完整流水线、压力测试） |

## Linux 6.1 API 兼容性备注

| API | Linux 6.1 状态 |
|-----|---------------|
| `dma_heap_add()` | 内部符号，未导出。需内核补丁添加 `EXPORT_SYMBOL_GPL` |
| `dma_buf_*` 系列符号 | 均在 `DMA_BUF` 命名空间下导出，模块需声明 `MODULE_IMPORT_NS(DMA_BUF)` |
| `dma_heap_ops.allocate` | 返回 `struct dma_buf *`（非 `int`），参数为 `unsigned long` |
| `dma_buf_map_attachment()` | 无 `_unlocked` 变体 |
| `dma_buf_get(int fd)` | 接收 fd；获取 `struct dma_buf *` 引用需使用 `get_dma_buf()` |
| `dma_heap_allocation_data` | 交叉编译工具链 uapi 头文件中不存在，需内联定义 |
