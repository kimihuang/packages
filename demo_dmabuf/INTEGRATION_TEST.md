# DMA-BUF Demo 集成测试指导文档

## 1. 测试环境准备

### 1.1 硬件/虚拟机要求

- 平台：QEMU `virt`（ARM64, cortex-a57, 4核, 1GB 内存）
- 内核：Linux 6.1（启用 `CONFIG_DMA_SHARED_BUFFER`、`CONFIG_DMABUF_HEAPS`、`CONFIG_SYNC_FILE`）

### 1.2 构建并部署

```bash
cd /home/lion/workdir/sourcecode/quantum_main
source build/envsetup.sh && lunch 2
make linux-dirclean && make linux-rebuild
make demo-dmabuf-rebuild
# 完整镜像构建（包含 rootfs overlay 中的 init.d 脚本）
make
```

### 1.3 启动目标系统

```bash
# 使用项目提供的 QEMU 启动脚本，或手动启动
./scripts/run_qemu.sh
# 或
qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 4 -m 1024 \
  -kernel out/quantum_qemu_debug/images/Image \
  -dtb out/quantum_qemu_debug/images/quantum_qemu.dtb \
  -append "console=ttyAMA0 root=/dev/vda rw" \
  -drive file=rootfs.ext4,format=raw,if=virtio -nographic
```

### 1.4 验证模块自动加载

系统启动后，init.d 脚本 `S15dmabuf` 会自动加载三个内核模块。验证：

```bash
# 查看已加载模块
lsmod | grep demo

# 预期输出：
# demo_importer   20480  0
# demo_exporter   16384  0
# demo_heap       20480  0

# 查看设备节点
ls -la /dev/dma_heap/demo /dev/demo_exp /dev/demo_imp

# 预期输出：
# crw-rw-rw- 1 root root 10, 60 ... /dev/dma_heap/demo
# crw-rw-rw- 1 root root ...     /dev/demo_exp
# crw-rw-rw- 1 root root ...     /dev/demo_imp
```

如果模块未自动加载，可手动执行：

```bash
/etc/init.d/S15dmabuf start
```

---

## 2. 测试工具说明

| 工具 | 路径 | 用途 |
|------|------|------|
| `demo_dmabuf_app` | `/usr/bin/demo_dmabuf_app` | 完整 9 步 DMA-BUF 流水线演示 |
| `test_dmabuf` | `/usr/bin/test_dmabuf` | 自动化测试套件（13 个用例） |
| `sync_file_info` | `/usr/bin/sync_file_info` | 查询 sync_file fence 信息 |

---

## 3. 接口参考

### 3.1 设备节点

| 设备 | 驱动 | 说明 |
|------|------|------|
| `/dev/dma_heap/demo` | `demo_heap.ko` | 自定义 DMA-BUF heap，使用 `vmalloc_user()` 分配 |
| `/dev/demo_exp` | `demo_exporter.ko` | DMA-BUF 导出器，支持 DMA 填充 + timer fence |
| `/dev/demo_imp` | `demo_importer.ko` | DMA-BUF 导入器，支持 attach/map + 校验和处理 |

### 3.2 用户空间 IOCTL 接口

#### dma-heap 分配（标准内核接口）

```c
// 头文件：linux/dma-heap.h（需内联定义）
struct dma_heap_allocation_data {
    __u64 len;          // 分配大小（字节）
    __u32 fd;           // [输出] 返回的 dma-buf fd
    __u32 fd_flags;     // 文件描述符标志（O_RDWR | O_CLOEXEC）
    __u64 heap_flags;   // heap 标志（0）
};
#define DMA_HEAP_IOCTL_ALLOC _IOWR('H', 0x0, struct dma_heap_allocation_data)
```

#### 导出器 IOCTL（`/dev/demo_exp`）

| IOCTL | 命令 | 说明 |
|-------|------|------|
| `DEMO_EXP_ALLOC` | `_IOR('E', 1, ...)` | 通过导出器分配 dma-buf，返回 fd |
| `DEMO_EXP_DMA_FILL` | `_IOWR('E', 2, ...)` | DMA 填充 dma-buf（timer 延迟），返回 out_fence |
| `DEMO_EXP_QUERY` | `_IOWR('E', 3, ...)` | 查询 dma-buf 大小和 fence 状态 |

```c
struct demo_exp_fill_req {
    __s32 dmabuf_fd;       // 输入：dma-buf 文件描述符
    __u32 fill_pattern;    // 填充字节模式（如 0x55）
    __u32 delay_ms;        // timer 延迟（毫秒）
    __s32 out_fence_fd;    // [输出] 完成 fence fd
};
```

#### 导入器 IOCTL（`/dev/demo_imp`）

| IOCTL | 命令 | 说明 |
|-------|------|------|
| `DEMO_IMP_IMPORT` | `_IOWR('I', 1, ...)` | 导入 dma-buf（attach + map） |
| `DEMO_IMP_PROCESS` | `_IOWR('I', 2, ...)` | 处理（校验和计算）+ completion fence |
| `DEMO_IMP_UNIMPORT` | `_IO('I', 3)` | 取消导入（detach + unmap） |

```c
struct demo_imp_import_req {
    __s32 dmabuf_fd;       // 输入：dma-buf fd
    __u32 num_sg_entries;  // [输出] scatter-gather 条目数
};

struct demo_imp_process_req {
    __u32 mode;            // 处理模式
    __u32 checksum;        // [输出] 数据校验和
    __s32 out_fence_fd;    // [输出] 完成 fence fd
};
```

#### DMA-BUF 同步接口（标准内核接口）

```c
// CPU 缓存同步
struct dma_buf_sync {
    __u64 flags;  // DMA_BUF_SYNC_START|DMA_BUF_SYNC_WRITE 等
};
#define DMA_BUF_IOCTL_SYNC _IOW('b', 0, struct dma_buf_sync)

// 导出/导入 reservation fence
#define DMA_BUF_IOCTL_EXPORT_SYNC_FILE _IOWR('b', 1, ...)
#define DMA_BUF_IOCTL_IMPORT_SYNC_FILE _IOWR('b', 2, ...)
```

---

## 4. 自动化测试

### 4.1 运行完整测试套件

```bash
test_dmabuf
```

预期输出示例：

```
=== Running DMA-BUF Test Suite ===

[test 1/13] test_heap_alloc_free ... PASSED
[test 2/13] test_mmap_read_write ... PASSED
[test 3/13] test_cpu_sync_cache ... PASSED
[test 4/13] test_exporter_alloc ... PASSED
[test 5/13] test_exporter_sync_fill ... PASSED
[test 6/13] test_importer_process ... PASSED
[test 7/13] test_export_sync_file ... PASSED
[test 8/13] test_import_sync_file ... PASSED
[test 9/13] test_implicit_sync_wait ... PASSED
[test 10/13] test_multi_consumer_parallel ... PASSED
[test 11/13] test_fence_timeout ... PASSED
[test 12/13] test_full_pipeline ... PASSED
[test 13/13] test_stress_repeated_cycles ... PASSED
  (completed 100 iterations)

=== Results: 13/13 passed, 0 failed ===
```

### 4.2 各测试用例说明

| # | 用例 | 测试内容 | 覆盖组件 |
|---|------|---------|---------|
| 1 | `test_heap_alloc_free` | 从 `/dev/dma_heap/demo` 分配 dma-buf 并释放 | demo_heap |
| 2 | `test_mmap_read_write` | mmap 后读写验证数据一致性 | demo_heap + mmap |
| 3 | `test_cpu_sync_cache` | `DMA_BUF_IOCTL_SYNC` CPU 缓存同步 | dma-buf sync |
| 4 | `test_exporter_alloc` | 通过导出器 `DEMO_EXP_ALLOC` 分配 | demo_exporter |
| 5 | `test_exporter_sync_fill` | DMA 填充 + fence 等待 + 数据验证 | demo_exporter + fence |
| 6 | `test_importer_process` | 完整导入 + 处理 + fence 流程 | demo_importer |
| 7 | `test_export_sync_file` | `DMA_BUF_IOCTL_EXPORT_SYNC_FILE` 导出 fence | dma-buf sync |
| 8 | `test_import_sync_file` | `DMA_BUF_IOCTL_IMPORT_SYNC_FILE` 导入 fence 到 reservation | dma-buf sync |
| 9 | `test_implicit_sync_wait` | 隐式同步路径验证 | 隐式同步 |
| 10 | `test_multi_consumer_parallel` | 两个 importer 并行消费同一 dma-buf | demo_importer 并发 |
| 11 | `test_fence_timeout` | 短超时 poll 超时 + 正常等待恢复 | fence 超时处理 |
| 12 | `test_full_pipeline` | 端到端 5 步完整流水线 | 全链路 |
| 13 | `test_stress_repeated_cycles` | 100 次快速分配-填充-释放循环 | 压力/稳定性 |

---

## 5. 手动测试指导

### 5.1 运行演示程序

```bash
demo_dmabuf_app
```

该程序自动执行完整 9 步流水线：

| 步骤 | 操作 | 预期结果 |
|------|------|---------|
| 1 | 从 `/dev/dma_heap/demo` 分配 4096 字节 | 获得有效 dmabuf fd |
| 2 | mmap + 写入 0xAA + cache flush | `[OK] Wrote 0xAA pattern, cache flushed` |
| 3 | 通过 `/dev/demo_exp` DMA 填充 0x55 | 返回 out_fence_fd |
| 4 | poll 等待显式 fence | `[OK] explicit fence signaled` |
| 5 | 通过 EXPORT_SYNC_FILE 等待隐式 fence | `[OK] no pending resv fences` 或 fence signaled |
| 6 | 读回验证首个字节 | `[OK] First byte verified: 0x55` |
| 7 | 导入到 `/dev/demo_imp` | `[OK] Imported dmabuf to importer` |
| 8 | 处理并等待完成 fence | `[OK] Processing complete, checksum computed` |
| 9 | 验证 READ fence 已清除 | `[OK] READ fences cleared` |

### 5.2 手动分步测试

#### Step A：分配 dma-buf

```bash
# 在目标板上使用 devmem 或编写简单脚本
# 推荐使用 test_dmabuf 或 demo_dmabuf_app 进行测试
# 以下为概念操作流程：

# 1. 打开 heap 设备
exec 3<>/dev/dma_heap/demo

# 2. 分配 dma-buf（需编写小工具或使用 demo_dmabuf_app）
```

#### Step B：使用 sync_file_info 查询 fence

`sync_file_info` 需要一个当前进程持有的 fence fd。通常在自定义测试脚本中使用：

```c
// 示例：分配 → 填充 → 查询 fence（不等待）
int heap_fd = open("/dev/dma_heap/demo", O_RDWR);
struct dma_heap_allocation_data alloc = { .len = 4096, .fd_flags = O_RDWR | O_CLOEXEC };
ioctl(heap_fd, DMA_HEAP_IOCTL_ALLOC, &alloc);
// alloc.fd 现在是 dmabuf fd

int exp_fd = open("/dev/demo_exp", O_RDWR);
struct demo_exp_fill_req fill = { .dmabuf_fd = alloc.fd, .fill_pattern = 0x55,
                                   .delay_ms = 1000, .out_fence_fd = -1 };
ioctl(exp_fd, DEMO_EXP_DMA_FILL, &fill);
// fill.out_fence_fd 现在是 fence fd（1 秒后才 signal）

// 查询 fence 状态（应在另一个终端）：
// sync_file_info <fill.out_fence_fd>

// 等待 fence
struct pollfd pfd = { .fd = fill.out_fence_fd, .events = POLLIN };
poll(&pfd, 1, 5000);
```

---

## 6. 模块加载/卸载测试

### 6.1 手动控制

```bash
# 卸载（反向顺序）
/etc/init.d/S15dmabuf stop

# 检查模块已移除
lsmod | grep demo
# 预期：无输出

# 重新加载
/etc/init.d/S15dmabuf start

# 重启
/etc/init.d/S15dmabuf restart
```

### 6.2 重复加载/卸载测试

```bash
for i in $(seq 1 10); do
    echo "=== Cycle $i ==="
    /etc/init.d/S15dmabuf restart
    test_dmabuf > /tmp/test_result_$i.log 2>&1
    if grep -q "0 failed" /tmp/test_result_$i.log; then
        echo "PASS"
    else
        echo "FAIL"
        cat /tmp/test_result_$i.log
    fi
done
```

---

## 7. 测试检查清单

### 7.1 基础功能

- [ ] 内核模块自动加载成功（`lsmod | grep demo`）
- [ ] 设备节点存在（`/dev/dma_heap/demo`、`/dev/demo_exp`、`/dev/demo_imp`）
- [ ] `test_dmabuf` 全部 13 个用例通过
- [ ] `demo_dmabuf_app` 9 步流水线全部 `[OK]`

### 7.2 同步机制

- [ ] 显式 fence：DMA 填充后 poll 等待成功
- [ ] 隐式 fence：EXPORT_SYNC_FILE 正确返回
- [ ] IMPORT_SYNC_FILE：fence 导入到 reservation 对象成功
- [ ] CPU 缓存同步：SYNC_START/SYNC_END 后数据一致

### 7.3 边界/异常

- [ ] fence 超时测试：短超时正确返回 `-ETIMEDOUT`
- [ ] 多消费者并行：两个 importer 同时处理同一 dma-buf
- [ ] 压力测试：100 次快速分配-填充-释放循环无错误
- [ ] 模块重复加载/卸载后功能正常

### 7.4 稳定性

- [ ] 连续 10 次模块 reload + test_dmabuf 全通过
- [ ] 压力测试后检查 `dmesg` 无内核错误/警告

```bash
# 检查内核日志
dmesg | grep -iE 'error|warn|fault|bug' | grep -v 'Warning.*clocks_property'
```

---

## 8. 故障排查

| 现象 | 可能原因 | 排查方法 |
|------|---------|---------|
| `open /dev/dma_heap/demo: No such file` | demo_heap 模块未加载 | `lsmod \| grep demo`；手动 `/etc/init.d/S15dmabuf start` |
| `insmod: cannot insert module: unknown symbol` | 内核缺少 DMA-BUF 支持 | 检查 `CONFIG_DMA_SHARED_BUFFER=y`、`CONFIG_DMABUF_HEAPS=y` |
| `DMA_HEAP_IOCTL_ALLOC: Invalid argument` | 内核配置缺少 `CONFIG_DMABUF_HEAPS` 或缺补丁 | 检查内核 `.config` 和 patch 是否已应用 |
| fence poll 永久阻塞 | exporter timer 未触发 | `dmesg \| grep demo` 检查内核日志 |
| mmap 返回 `MAP_FAILED` | 分配大小为 0 或模块异常 | 确认 alloc.len > 0；检查 `dmesg` |
| 测试偶发失败 | 定时敏感的 fence 竞态 | 多次运行确认是否可复现；增大 delay_ms |
| `test_export_sync_file: ASSERT failed` | 内核版本不支持 `DMA_BUF_IOCTL_EXPORT_SYNC_FILE` | 确认 `CONFIG_SYNC_FILE=y` |
