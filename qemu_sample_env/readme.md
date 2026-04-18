# QEMU Sample Env

QEMU aarch64 虚拟机启动环境，基于 Buildroot rootfs。

## 目录结构

```
qemu_sample_env/
├── rootfs.cpio            # cpio 格式根文件系统
├── rootfs.ext2            # ext2 格式根文件系统
├── quantum_qemu.dtb       # 自定义设备树
├── qemu_cpio.sh           # cpio 方式启动 QEMU
├── qemu_ext2.sh           # ext2 方式启动 QEMU（待修复 virtio）
├── unpack_rootfs.sh       # 解包 rootfs.cpio
├── pack_rootfs.sh         # 打包 rootfs.cpio（fakeroot 保持 root 权限）
└── labgrid/               # labgrid 自动化测试
    ├── conftest.py        # 共享 fixture（QEMU 启动/关闭，console 日志）
    ├── labgrid-env.yaml   # labgrid 环境配置
    ├── test_monitor.py    # QMP Monitor 测试
    ├── test_system.py     # 系统信息测试（CPU、内核、内存）
    ├── test_filesystem.py # 文件系统测试
    ├── test_process.py    # 进程和服务测试
    └── run_report.sh      # 测试报告生成脚本
```

## QEMU 启动

### cpio 方式（推荐）

```bash
./qemu_cpio.sh                     # 默认 monitor: unix:qemu-monitor.sock,server,nowait
./qemu_cpio.sh none                # 关闭 monitor
./qemu_cpio.sh stdio               # monitor 走 stdio
./qemu_cpio.sh unix:/tmp/q.sock    # 自定义 socket 路径
```

### 通过 monitor socket 交互

```bash
# 启动 QEMU（后台）
./qemu_cpio.sh </dev/null &>/tmp/qemu.log &

# 发送 monitor 命令
echo "info version" | socat - UNIX-CONNECT:qemu-monitor.sock
```

## Rootfs 打包/解包

```bash
./unpack_rootfs.sh   # 解包到 rootfs/ 目录
# 修改 rootfs/ 中的文件...
./pack_rootfs.sh     # 用 fakeroot 重新打包
```

## Labgrid 测试

使用 labgrid + pytest 对 QEMU 虚拟机进行自动化测试。

### 前置依赖

```bash
pip install labgrid pytest
```

### 运行全部测试

```bash
cd labgrid
pytest -v --lg-env labgrid-env.yaml -s
```

### 查看完整 Linux 启动日志

```bash
cd labgrid
# 日志输出到文件
pytest -v --lg-env labgrid-env.yaml -s --log-cli-level=INFO 2>&1 | tee test.log

# 直接显示日志
pytest -v --lg-env labgrid-env.yaml -s --log-cli-level=INFO 
```

`labgrid.console` logger 会输出 QEMU console 的全部内容（内核启动日志、init 脚本输出等）。

### 运行指定测试文件

```bash
pytest test_monitor.py -v --lg-env labgrid-env.yaml -s
pytest test_system.py -v --lg-env labgrid-env.yaml -s
```

### 运行指定测试用例

```bash
pytest test_system.py::TestCPU::test_cpu_architecture -v --lg-env labgrid-env.yaml -s
```

### 测试用例

| 文件 | 类 | 测试 | 说明 |
|------|----|------|------|
| test_monitor.py | TestQEMUMonitor | test_query_version | 查询 QEMU 版本 |
| test_monitor.py | TestQEMUMonitor | test_query_status | 查询 VM 运行状态 |
| test_monitor.py | TestQEMUMonitor | test_query_cpus | 查询 CPU 拓扑 |
| test_system.py | TestCPU | test_cpu_info | 查看 CPU 信息 |
| test_system.py | TestCPU | test_cpu_architecture | 确认 aarch64 架构 |
| test_system.py | TestCPU | test_cpu_online_count | 确认 CPU 在线数量 |
| test_system.py | TestKernel | test_kernel_version | 查看内核版本 |
| test_system.py | TestKernel | test_kernel_release | 确认内核版本号格式 |
| test_system.py | TestMemory | test_meminfo | 查看内存信息 |
| test_system.py | TestMemory | test_total_memory | 确认总内存大于 0 |
| test_filesystem.py | TestFilesystem | test_root_mount | 确认根文件系统已挂载 |
| test_filesystem.py | TestFilesystem | test_root_writable | 确认根文件系统可写 |
| test_filesystem.py | TestFilesystem | test_proc_mounts | 查看 /proc/mounts |
| test_filesystem.py | TestFilesystem | test_tmpfs_available | 确认 /tmp 可读写 |
| test_process.py | TestProcess | test_init_process | 确认 init 进程存在 |
| test_process.py | TestProcess | test_process_count | 查看进程数量 |
| test_process.py | TestProcess | test_sh_available | 确认 sh 可用 |
| test_process.py | TestService | test_syslogd_running | 确认 syslogd 运行 |
| test_process.py | TestService | test_cron_running | 确认 crond 运行 |
| test_process.py | TestService | test_klogd_running | 确认 klogd 运行 |

### 生成测试报告

```bash
cd labgrid
./run_report.sh
```

生成的报告文件：

```
labgrid/report/
├── report.html   # HTML 看板（浏览器打开）
├── report.json   # JSON 结构化结果（CI/CD 消费）
└── console.log   # 完整控制台输出（含 Linux 启动日志）
```

- **report.html** — 交互式看板，包含通过/失败统计、耗时、每个用例的详细日志
- **report.json** — 机器可读的 JSON，适合 CI/CD pipeline 解析

CI/CD 集成：

```bash
cd labgrid && ./run_report.sh
echo $?  # 0=全部通过，非0=有失败
```
