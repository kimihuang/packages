# elog 集成测试操作文档

## 1. 构建准备

```bash
cd /home/lion/workdir/sourcecode/quantum_main/src/packages/embeded_logging
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
```

构建产物：

| 文件 | 说明 |
|------|------|
| `build/libelog.so` | 日志库 |
| `build/elogd` | 守护进程 |
| `build/elogcat` | 日志查看 CLI |
| `build/elog_app` | 示例应用 |
| `build/elog_test` | 单元测试 |
| `build/elog_integration_test` | 集成测试 |

---

## 2. 架构概览

```
  elog_app (日志生产者)
       |
       | SOCK_DGRAM (/var/run/elogdw)
       v
     elogd (守护进程, 4 个线程)
       |-- listener:  接收日志 → 写入 ring buffer
       |-- cmd:       管理命令 (clear/stats/exit)
       |-- flusher:   500ms 周期刷盘 → /var/log/elog_*.log
       |-- reader:    SOCK_SEQPACKET 推送到 elogcat
       v
  elogcat (日志消费者)
```

六个独立 ring buffer：`main`(256KB) `radio`(64KB) `events`(256KB) `system`(64KB) `crash`(1MB) `kernel`(64KB)

---

## 3. 基础功能测试

### 3.1 启动/停止 elogd

```bash
# 默认路径启动 (需要 root)
sudo ./build/elogd

# 自定义路径启动 (不需要 root)
mkdir -p /tmp/elog_test
./build/elogd -w/tmp/elog_test/w.sock -c/tmp/elog_test/c.sock -r/tmp/elog_test/r.sock

# 优雅停止 (SIGTERM/SIGINT)
kill -TERM <pid>
```

验证：进程启动后创建 3 个 socket 文件，监听端口正常。

### 3.2 elogcat dump 模式

```bash
# 启动 elogd
sudo ./build/elogd &

# 在另一个终端启动示例应用写入日志
LD_LIBRARY_PATH=./build ./build/elog_app -n 3

# dump 全部日志
LD_LIBRARY_PATH=./build ./build/elogcat -d

# 只看最近 10 条
LD_LIBRARY_PATH=./build ./build/elogcat -d -t 10

# 清理
sudo kill %1
```

验证项：
- 日志格式正确：`时间戳 级别 PID TID TAG: 消息`
- 6 种级别颜色区分 (V/D/I/W/E/F)
- tag 过滤 (`-s app`) 只显示指定 tag
- 多 buffer 订阅 (`-b main -b crash`) 只显示指定 buffer

### 3.3 elogcat 实时跟踪模式

```bash
sudo ./build/elogd &

# 终端 1: 实时跟踪
LD_LIBRARY_PATH=./build ./build/elogcat -b main

# 终端 2: 写日志
LD_LIBRARY_PATH=./build ./build/elog_app -n 5

# 验证 Ctrl+C 能正常终止
# (已修复 bug: sigaction 替代 signal, 不设 SA_RESTART)
```

验证项：
- 日志实时输出
- `Ctrl+C` 能立即终止 elogcat
- `--pid <PID>` 只显示指定进程的日志

### 3.4 多 buffer 隔离

```bash
sudo ./build/elogd &
LD_LIBRARY_PATH=./build ./build/elog_app -n 1

# 只看 main buffer
LD_LIBRARY_PATH=./build ./build/elogcat -d -b main

# 只看 events buffer
LD_LIBRARY_PATH=./build ./build/elogcat -d -b events

# 只看 kernel buffer
LD_LIBRARY_PATH=./build ./build/elogcat -d -b kernel
```

验证：每个 buffer 只包含对应路由的日志，无交叉。

---

## 4. 管理命令测试

### 4.1 清空 buffer

```bash
# 先写入日志
LD_LIBRARY_PATH=./build ./build/elog_app -n 3

# 确认有日志
LD_LIBRARY_PATH=./build ./build/elogcat -d | wc -l

# 清空全部
LD_LIBRARY_PATH=./build ./build/elogcat -c

# 确认已清空
LD_LIBRARY_PATH=./build ./build/elogcat -d
# 应无输出
```

### 4.2 查看 buffer 统计

```bash
LD_LIBRARY_PATH=./build ./build/elog_app -n 3
LD_LIBRARY_PATH=./build ./build/elogcat -g
```

预期输出包含每个 buffer 的 `capacity=` `count=` 等字段。

### 4.3 设置 buffer 大小

```bash
LD_LIBRARY_PATH=./build ./build/elogcat -G 131072
# 返回确认信息
```

---

## 5. 编译配置测试

通过 cmake 参数覆盖默认配置，验证不同配置组合下的功能：

### 5.1 小内存配置

```bash
cd build
cmake .. -DCMAKE_C_FLAGS="-DELOG_BUFFER_SIZE=8192 -DELOG_MAX_MSG_LEN=128 -DELOG_MAX_TAG_LEN=16"
make -j$(nproc)
```

测试项：
- 短 tag 和短消息正常工作
- buffer 溢出时 overwrite 行为正确 (旧日志被覆盖)
- 日志截断处理正确

### 5.2 裁剪功能配置

```bash
# 关闭事件日志
cmake .. -DCMAKE_C_FLAGS="-DELOG_EVENT_ENABLE=0"
make -j$(nproc)

# 关闭统计
cmake .. -DCMAKE_C_FLAGS="-DELOG_STATS_ENABLE=0"
make -j$(nproc)

# 关闭裁剪
cmake .. -DCMAKE_C_FLAGS="-DELOG_PRUNE_ENABLE=0"
make -j$(nproc)

# 全部关闭 (最小化构建)
cmake .. -DCMAKE_C_FLAGS="-DELOG_EVENT_ENABLE=0 -DELOG_STATS_ENABLE=0 -DELOG_PRUNE_ENABLE=0 -DELOG_COLOR_ENABLE=0 -DELOG_TIMESTAMP_ENABLE=0"
make -j$(nproc)
```

### 5.3 调试输出

```bash
# 开启全部调试输出
cmake .. -DCMAKE_C_FLAGS="-DELOG_DBG_ALL"
make -j$(nproc)

# 开启指定模块
cmake .. -DCMAKE_C_FLAGS="-DELOG_DBG_RING -DELOG_DBG_LISTENER -DELOG_DBG_READER"
make -j$(nproc)
```

启动 elogd 后观察 stdout 调试输出：
- `[RING]` ring buffer 操作
- `[LISTEN]` 日志接收/路由
- `[READER]` reader 客户端管理
- `[FLUSH]` 刷盘操作
- `[CLIENT]` 客户端连接/发送
- `[EVENT]` 事件编解码
- `[PRUNE]` 裁剪决策

---

## 6. 单元测试

```bash
cd build

# 通过 CTest 运行
ctest --output-on-failure

# 或直接运行
./elog_test
```

9 个测试套件：elog(buf/format/stats/prune/event/port/reader/elogd)。

---

## 7. 集成测试 (自动化)

```bash
cd build
LD_LIBRARY_PATH=. ./elog_integration_test
```

自动执行 15 个端到端测试 (fork elogd + 写日志 + 读验证)：

| 测试 | 说明 |
|------|------|
| `test_e2e_basic` | 基本写入/读取，验证消息内容 |
| `test_e2e_level_filter` | 6 种日志级别正确传递 |
| `test_e2e_header_fields` | header 字段 (level/log_id/tag) 正确 |
| `test_e2e_multi_entries` | 连续 10 条消息顺序和完整性 |
| `test_e2e_cmd_stats` | cmd socket stats 命令响应 |
| `test_e2e_multi_buffer` | main/radio/system 三 buffer 隔离 |
| `test_e2e_log_mask` | reader 按 buffer bitmask 过滤 |
| `test_e2e_events_kernel` | events + kernel buffer 端到端 |
| `test_e2e_buffer_stats` | stats 命令返回全部 6 个 buffer |
| `test_concurrent_writers` | 4 进程 x 50 条并发写 |
| `test_concurrent_readers` | 2 个 reader 同时订阅不同 buffer |
| `test_concurrent_write_read` | 写+读并发无崩溃无丢失 |
| `test_concurrent_write_cmd` | 写+stats 命令并发无崩溃 |
| `test_concurrent_multi_buffer` | 多进程写不同 buffer 隔离 |
| `test_e2e_binary_event` | 二进制事件含嵌入 NUL 完整保留 |

---

## 8. 压力测试

### 8.1 高频写入

```bash
sudo ./build/elogd &
LD_LIBRARY_PATH=./build ./build/elog_app -n 10000

# 检查是否丢失/乱序
LD_LIBRARY_PATH=./build ./build/elogcat -d -t 100 | grep "round" | tail -5
```

### 8.2 buffer 溢出 (小 buffer)

```bash
cd build
cmake .. -DCMAKE_C_FLAGS="-DELOG_BUFFER_SIZE=2048 -DELOG_DBG_RING"
make -j$(nproc)

sudo ./build/elogd &
LD_LIBRARY_PATH=./build ./build/elog_app -n 5000

# 观察调试输出中的 overwrite 日志
# 确认 buffer 满后旧条目被覆盖, 无崩溃
```

### 8.3 并发生产者

```bash
sudo ./build/elogd &

# 4 个进程同时写日志
for i in 1 2 3 4; do
    LD_LIBRARY_PATH=./build ./build/elog_app -n 500 &
done
wait

# 检查总数
LD_LIBRARY_PATH=./build ./build/elogcat -d | wc -l
# 应 >= 4*500 = 2000 (受 buffer 容量限制, 可能更少)
```

### 8.4 多 reader 同时消费

```bash
sudo ./build/elogd &
LD_LIBRARY_PATH=./build ./build/elog_app -n 3 &

# 3 个 elogcat 同时订阅
LD_LIBRARY_PATH=./build ./build/elogcat -d -b main > /tmp/out1.txt &
LD_LIBRARY_PATH=./build ./build/elogcat -d -b main > /tmp/out2.txt &
LD_LIBRARY_PATH=./build ./build/elogcat -d -b main > /tmp/out3.txt &
wait

# 比较输出一致性
diff /tmp/out1.txt /tmp/out2.txt
diff /tmp/out2.txt /tmp/out3.txt
```

---

## 9. 守护进程稳定性测试

### 9.1 优雅关闭

```bash
sudo ./build/elogd &
PID=$!
LD_LIBRARY_PATH=./build ./build/elog_app -n 3
sudo kill -TERM $PID
wait $PID
echo "exit code: $?"
# 应为 0
```

### 9.2 客户端断开/重连

```bash
sudo ./build/elogd &

# elogcat 断开后再重连
LD_LIBRARY_PATH=./build ./build/elogcat -d
LD_LIBRARY_PATH=./build ./build/elogcat -d -t 5
LD_LIBRARY_PATH=./build ./build/elogcat -d

# elog_app 反复启停
for i in $(seq 10); do
    LD_LIBRARY_PATH=./build ./build/elog_app -n 5
done

# 确认 elogd 仍在运行
ps aux | grep elogd
```

### 9.3 残留 socket 清理

```bash
# elogd 异常退出后残留 socket
sudo ./build/elogd &
sudo kill -9 $(pgrep elogd)

# 再次启动应正常 (覆盖残留 socket)
sudo ./build/elogd &
LD_LIBRARY_PATH=./build ./build/elogcat -d
sudo kill -TERM $(pgrep elogd)
```

---

## 10. 刷盘验证

```bash
sudo ./build/elogd &
LD_LIBRARY_PATH=./build ./build/elog_app -n 5

# 等 flusher 周期 (500ms)
sleep 1

# 检查日志文件
ls -la /var/log/elog_*.log

# 验证文件内容
cat /var/log/elog_main.log
cat /var/log/elog_events.log
cat /var/log/elog_kernel.log

sudo kill -TERM $(pgrep elogd)
```

---

## 11. 二进制/事件日志测试

```bash
sudo ./build/elogd &
LD_LIBRARY_PATH=./build ./build/elog_app -n 3

# events buffer 文本输出
LD_LIBRARY_PATH=./build ./build/elogcat -d -b events

# 二进制原始输出
LD_LIBRARY_PATH=./build ./build/elogcat -d -b events -B | xxd | head -20

# 验证 TLV 编解码: event_id=1001, 包含 int32+string+float
sudo kill -TERM $(pgrep elogd)
```

---

## 12. 回归测试检查清单

每次代码变更后执行：

```bash
cd /home/lion/workdir/sourcecode/quantum_main/src/packages/embeded_logging/build

# 1. 编译无警告
cmake .. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_FLAGS="-Wall -Wextra -Werror -DELOG_DBG_ALL"
make -j$(nproc) 2>&1 | grep -i "warning\|error"

# 2. 单元测试全通过
ctest --output-on-failure

# 3. 集成测试全通过
LD_LIBRARY_PATH=. ./elog_integration_test

# 4. 手动 smoke test
sudo ./elogd &
LD_LIBRARY_PATH=. ./elog_app -n 3
LD_LIBRARY_PATH=. ./elogcat -d
LD_LIBRARY_PATH=. ./elogcat -c
sudo kill -TERM $(pgrep elogd)
```

---

## 13. Socket 路径速查

| 用途 | 默认路径 | 类型 |
|------|---------|------|
| 日志写入 | `/var/run/elogdw` | `SOCK_DGRAM` |
| 管理命令 | `/var/run/elogd` | `SOCK_STREAM` |
| 日志推送 | `/var/run/elogdr` | `SOCK_SEQPACKET` |
| 刷盘目录 | `/var/log/elog_*.log` | 文件 |

自定义路径方式：
- elogd 命令行: `elogd -w <path> -c <path> -r <path>`
- 客户端全局变量: `g_daemon_write_sock`, `g_daemon_cmd_sock`, `g_daemon_reader_sock`
