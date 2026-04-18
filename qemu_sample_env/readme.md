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
    ├── labgrid-env.yaml   # labgrid 环境配置
    ├── test_qemu.py       # pytest 测试用例
    └── readme.md          # 本文件
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

### 运行测试

```bash
cd labgrid
pytest test_qemu.py -v --lg-env labgrid-env.yaml -s
```

### 测试用例

| 测试 | 说明 |
|------|------|
| test_qemu_monitor_version | 通过 QMP monitor 查询 QEMU 版本 |
| test_cpu_info | 查看 CPU 信息（/proc/cpuinfo） |
| test_kernel_version | 查看内核版本（uname -a） |
