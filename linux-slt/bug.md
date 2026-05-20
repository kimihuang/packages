# linux-slt Bug 报告

测试时间：2026-04-22
测试环境：QEMU aarch64 / Buildroot rootfs / labgrid 自动化测试
测试人员：labgrid automated test

---

## Bug 1: slt_test 未安装到 rootfs

**严重级别**：Medium
**组件**：Build / Packaging

**现象**：
`slt_test` 单元测试可执行文件未安装到 rootfs 中。`which slt_test` 返回空，无法在目标系统上运行单元测试。

**期望**：
`slt_test` 应随 `slt_daemon` 一起安装到 rootfs（如 `/usr/bin/slt_test`），以便在目标系统上验证功能正确性。

**复现步骤**：
1. 启动 QEMU
2. 执行 `which slt_test`
3. 返回空

**建议**：
在 Buildroot 的 `linux-slt.mk` 中将 `slt_test` 加入安装目标：
```makefile
$(INSTALL) -D -m 0755 $(@D)/build/bin/slt_test $(TARGET_DIR)/usr/bin/slt_test
```

---

## Bug 2: slt_daemon --help 不退出

**严重级别**：Low
**组件**：slt_daemon

**现象**：
执行 `slt_daemon --help` 后，程序打印帮助信息但仍继续运行（尝试打开串口并阻塞），不退出。在 CI/CD 场景中需要 `timeout` 命令来限制。

**期望**：
`--help` 应打印帮助信息后立即退出（exit code 0），不继续初始化。

**复现步骤**：
1. 执行 `/usr/bin/slt_daemon --help`
2. 观察程序打印帮助信息后不退出

**建议**：
在参数解析中，当识别到 `-h`/`--help` 时，打印帮助后直接 `return 0`，不进入 `start()` 流程。

---

## Bug 3: /opt/slt 目录未创建

**严重级别**：Low
**组件**：Build / Packaging

**现象**：
文档（slt_architecture.md）中描述 slt_daemon 的工作目录为 `/opt/slt/`，但 rootfs 中该目录不存在。

**期望**：
安装时应创建 `/opt/slt/` 目录，或 systemd service 文件中的 `WorkingDirectory` 应更新为实际存在的路径。

**建议**：
在 Buildroot 打包中创建目录，或在 systemd service 中移除 `WorkingDirectory=/opt/slt/` 依赖。

---

## Bug 4: 配置文件格式与文档不一致

**严重级别**：Low
**组件**：Documentation

**现象**：
文档（USAGE.md、slt_architecture.md）描述配置文件格式为 YAML，但实际部署的 `/etc/slt/config.yaml` 使用的是 INI 格式（`[section]` / `key: value`）。

**期望**：
文档与实际配置格式保持一致。

**建议**：
更新文档说明支持 INI 格式，或修改代码统一使用 YAML 格式。
