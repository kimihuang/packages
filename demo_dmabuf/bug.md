# demo_dmabuf Bug 报告

> 测试环境: QEMU virt (ARM64, cortex-a57, 4核, 512MB), Linux 6.1
> 测试框架: labgrid + pytest, 测试文件: `labgrid_test/labgrid_qemu/test_dmabuf.py`
> 测试结果: **5 passed, 21 failed** (总 26 用例)
> 测试时间: 2026-04-27

---

## Bug #1 [CRITICAL] demo_heap.ko sg_alloc_table_from_pages 触发内核 BUG

**模块**: `demo_heap.ko` (`demo_heap/demo_heap.c`)

**现象**: 在 `test_dmabuf` 运行到 `test_mmap_read_write` (第2个用例) 时，内核触发 BUG 后 segfault，后续所有用例连锁崩溃。

**内核 Oops 输出**:

```
[   15.369435] ------------[ cut here ]------------
[   15.369697] kernel BUG at include/linux/scatterlist.h:115!
[   15.370127] Internal error: Oops - BUG: 00000000f2000800 [#1] SMP
[   15.370512] Modules linked in: demo_importer(O) demo_exporter(O) demo_heap(O)
[   15.378006] CPU: 0 PID: 152 Comm: test_dmabuf Tainted: G           O       6.1.0 #1
[   15.378402] Hardware name: linux,dummy-virt (DT)
[   15.378806] pstate: 00000005 (nzcv daif -PAN -UAO -TCO -DIT -SSBS BTYPE=--)
[   15.379101] pc : sg_alloc_append_table_from_pages+0x420/0x480
[   15.388356] lr : sg_alloc_append_table_from_pages+0x264/0x480
...
Call trace:
 sg_alloc_append_table_from_pages+0x420/0x480
 sg_alloc_table_from_pages_segment+0x44/0x130
 demo_heap_allocate+0x1a4/0xa80 [demo_heap]
 dma_heap_ioctl+0x14c/0x2e0
 __arm64_sys_ioctl+0x2e0/0xc30
 invoke_syscall.constprop.0+0x5c/0x110
 do_el0_svc+0x58/0x170
 el0_svc+0x34/0xf0
 el0t_64_sync_handler+0x114/0x120
 el0t_64_sync+0x18c/0x190
Code: 17ffffaa a90153f3 b9008fff 17ffff2d (d4210000)
---[ end trace 0000000000000000 ]---
Segmentation fault
```

**根因分析**:
- `demo_heap.c:demo_heap_allocate()` 调用 `sg_alloc_table_from_pages()` 时触发 BUG
- vmalloc_user() 分配的虚拟地址可能跨越多个不连续的物理页面
- Linux 6.1 内核的 `sg_alloc_table_from_pages_segment()` 中 `scatterlist.h:115` 处有 `BUG_ON` 检查不通过
- `demo_heap.c` 中先自己调用 `sg_alloc_table_from_pages()` 构建 `buf->sg_table`，而 `demo_heap_attach()` 时又为每个 attachment 再次调用 `sg_alloc_table_from_pages()`，第二次调用时可能因 vmalloc 页面的特性触发 BUG

**首次出现位置**: `demo_heap.c:demo_heap_allocate()` 内 `sg_alloc_table_from_pages()` 调用

**影响的 pytest 用例** (15个，连锁崩溃):
- `TestDmaBufHeap::test_heap_basic_alloc_free` — Bus error
- `TestDmaBufHeap::test_mmap_read_write` — kernel BUG + Segfault
- `TestDmaBufCpuSync::test_cpu_sync_cache` — Bus error (内核状态已损坏)
- `TestDmaBufExporter::test_exporter_alloc` — ExecutionError (Bus error)
- `TestDmaBufExporter::test_exporter_dma_fill` — ExecutionError (Bus error)
- `TestDmaBufImporter::test_importer_process` — ExecutionError (Bus error)
- `TestDmaBufSyncFile::test_export_sync_file` — ExecutionError (Bus error)
- `TestDmaBufSyncFile::test_import_sync_file` — ExecutionError (Bus error)
- `TestDmaBufImplicitSync::test_implicit_sync_wait` — ExecutionError (Bus error)
- `TestDmaBufConcurrency::test_multi_consumer_parallel` — ExecutionError (Bus error)
- `TestDmaBufConcurrency::test_fence_timeout` — ExecutionError (Bus error)
- `TestDmaBufFullPipeline::test_full_pipeline_test_suite` — ExecutionError (Bus error)
- `TestDmaBufFullPipeline::test_demo_app_full_9_steps` — DEMO_EXP_DMA_FILL: Operation not permitted
- `TestDmaBufStress::test_stress_repeated_cycles` — ExecutionError (Bus error)
- `TestDmaBufStress::test_all_13_tests_passed` — ExecutionError (kernel BUG)
- `TestDmaBufStress::test_no_kernel_errors` — ExecutionError (Bus error)

**目标板上 test_dmabuf 自身结果** (3 passed / 10 failed):
```
test 1  test_heap_alloc_free       PASS
test 2  test_mmap_read_write       PASS (首次运行时通过)
test 3  test_cpu_sync_cache        PASS
test 4  test_exporter_alloc        FAIL  ASSERT_EQ(ret, 0) got -1, expected 0  @ test_dmabuf.c:216
test 5  test_exporter_sync_fill    FAIL  ASSERT_EQ(ret, 0) got -1, expected 0  @ test_dmabuf.c:242
test 6  test_importer_process      FAIL  ASSERT_EQ(ret, 0) got -1, expected 0  @ test_dmabuf.c:290
test 7  test_export_sync_file      FAIL  ASSERT_EQ(ret, 0) got -1, expected 0  @ test_dmabuf.c:339
test 8  test_import_sync_file      FAIL  ASSERT_EQ(ret, 0) got -1, expected 0  @ test_dmabuf.c:391
test 9  test_implicit_sync_wait    FAIL  ASSERT_EQ(ret, 0) got -1, expected 0  @ test_dmabuf.c:446
test 10 test_multi_consumer_parallel FAIL ASSERT_EQ(ret, 0) got -1, expected 0 @ test_dmabuf.c:506
test 11 test_fence_timeout         FAIL  ASSERT_EQ(ret, 0) got -1, expected 0  @ test_dmabuf.c:577
test 12 test_full_pipeline         FAIL  ASSERT_EQ(ret, 0) got -1, expected 0  @ test_dmabuf.c:626
test 13 test_stress_repeated_cycles FAIL (rc=-1)
```

**test 4~12 的共同失败点**: 所有失败均为 `open("/dev/demo_exp", O_RDWR)` 返回 -1 (Operation not permitted) 或 ioctl 返回 -1，说明 `demo_exporter.ko` 在内核 BUG 后状态不可用。首次运行时前3个用例能通过，到 test 4 打开 `/dev/demo_exp` 时触发 exporter 的 `DEMO_EXP_ALLOC` ioctl 调用路径，此时可能因内核 BUG 的连锁影响导致失败。

**修复建议**:
1. 排查 `demo_heap.c` 中 `sg_alloc_table_from_pages()` 在 Linux 6.1 下的兼容性
2. 考虑使用 `sg_alloc_table_from_pages_segment()` 替代，或添加适当的 max_segment 参数
3. 检查 vmalloc_user() 分配的页面在传递给 sg table 时是否需要特殊处理

---

## Bug #2 [NORMAL] demo_exporter.ko DEMO_EXP_DMA_FILL 返回 Operation not permitted

**模块**: `demo_exporter.ko` (`demo_exporter/demo_exporter.c`)

**现象**: `demo_dmabuf_app` 在 Step 3 (DMA fill via /dev/demo_exp) 时报错:
```
DEMO_EXP_DMA_FILL: Operation not permitted
```

**demo_dmabuf_app 输出**:
```
=== DMA-BUF Demo Application ===
-- Step 1: Allocate DMA-BUF from /dev/dma_heap/demo --
[OK]   Allocated dmabuf fd=12, size=4096
-- Step 2: mmap, write 0xAA pattern, cache flush --
[OK]   Wrote 0xAA pattern, cache flushed
-- Step 3: DMA fill via /dev/demo_exp --
DEMO_EXP_DMA_FILL: Operation not permitted
```

**根因分析**:
- `demo_exporter.c:DEMO_EXP_DMA_FILL` ioctl 调用链中 `dma_resv_wait_timeout()` 等待 WRITE fence 超时或被拒绝
- 也可能与 Bug #1 的内核 BUG 连锁相关 — 首次成功分配后 mmap 触发了内核 BUG，exporter 状态已损坏
- 需要在 Bug #1 修复后重新验证此问题是否仍存在

**影响的 pytest 用例** (1个):
- `TestDmaBufFullPipeline::test_demo_app_full_9_steps`

---

## Bug #3 [NORMAL] S15dmabuf stop 后模块未卸载

**模块**: init.d 脚本 `S15dmabuf`

**现象**: 执行 `/etc/init.d/S15dmabuf stop` 后，`lsmod | grep demo` 仍显示三个模块存在:

```
demo_importer          16384  -2
demo_exporter          16384  -2
demo_heap              16384  -2
```

**根因分析**:
- 使用计数为 `-2`，表示模块正在被卸载中 (GOING_AWAY 状态)
- 但 rmmod 进程可能因 Bug #1 的内核 BUG 而未能完成卸载
- 也可能是卸载顺序问题：demo_heap 被 demo_exporter/demo_importer 引用，需先卸载依赖模块
- 需要在 Bug #1 修复后重新验证

**影响的 pytest 用例** (1个):
- `TestDmaBufModuleReload::test_module_stop_and_check`

---

## Bug #4 [NORMAL] 内核 BUG 后 Shell 超时 (连锁)

**现象**: 在 Bug #1 内核 BUG 触发后，后续 pytest 用例中出现 pexpect TIMEOUT (30s):
- `TestDmaBufReloadStability::test_reload_3_cycles_all_pass` — Timeout of 28.26s
- `TestDmaBufTools::test_test_dmabuf_exists` — Timeout of 29.99s
- `TestDmaBufTools::test_demo_dmabuf_app_exists` — Timeout of 29.99s
- `TestDmaBufTools::test_sync_file_info_exists` — Timeout of 29.99s

**根因**: 内核 BUG/Oops 后系统状态不稳定，shell 响应停滞。这不是独立的 bug，是 Bug #1 的连锁效应。

**修复**: 修复 Bug #1 后此问题自动消失。

---

## 通过的用例 (5个)

| 用例 | 说明 |
|------|------|
| `TestDmaBufModule::test_modules_loaded` | 模块加载正常 |
| `TestDmaBufModule::test_device_nodes_exist` | 设备节点存在 |
| `TestDmaBufModule::test_init_script_exists` | init.d 脚本存在 |
| `TestDmaBufModuleReload::test_module_start_after_stop` | 重新加载成功 |
| `TestDmaBufModuleReload::test_module_restart` | restart 成功 |

---

## 总结

| 优先级 | Bug ID | 描述 | 影响用例数 | 是否独立 |
|--------|--------|------|-----------|---------|
| CRITICAL | #1 | demo_heap.ko sg_alloc_table_from_pages 内核 BUG | 15+4=19 | 是 (根因) |
| NORMAL | #2 | demo_exporter.ko DMA_FILL Operation not permitted | 1 | 可能是 #1 连锁 |
| NORMAL | #3 | S15dmabuf stop 后模块未卸载 (-2 GOING_AWAY) | 1 | 可能是 #1 连锁 |
| NORMAL | #4 | 内核 BUG 后 Shell 超时 | 4 | #1 连锁 |

**修复优先级**: 先修复 Bug #1 (demo_heap.ko 内核 BUG)，修复后需全量回归。
