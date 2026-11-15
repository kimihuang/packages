# notify_benchmark

测量 Linux 用户态线程从 `notify_one()` 调用到被唤醒线程真正开始执行的时间，用于评估系统**线程唤醒 / 调度延迟**。

详细设计见 [DESIGN.md](./DESIGN.md)。

## 构建

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

或直接用 g++：

```bash
g++ -O2 -std=c++17 -pthread -o notify_benchmark main.cpp notify_benchmark.cpp
```

## 运行

```bash
# 默认：10 万次测量、1 万次预热
./build/notify_benchmark

# 指定迭代与预热
./build/notify_benchmark -n 1000000 -w 100000

# 绑定 CPU 并以实时调度运行（需 root/CAP_SYS_NICE）
sudo ./build/notify_benchmark --cpu-producer 0 --cpu-consumer 1 --rt

# 导出原始样本 CSV 供绘图分析
./build/notify_benchmark --csv latency.csv
```

## 常用参数

| 参数 | 说明 |
|------|------|
| `-n, --iterations N` | 有效测量迭代次数（默认 100000） |
| `-w, --warmup N` | 预热迭代次数（默认 10000） |
| `--cpu-producer N` | 生产者线程 CPU 亲和性 |
| `--cpu-consumer N` | 消费者线程 CPU 亲和性 |
| `--rt` | SCHED_FIFO 实时调度 |
| `--rt-priority N` | 实时优先级 1~99（默认 80） |
| `--settle-us N` | 生产者收到 ready 后让出 CPU 的微秒数（默认 10） |
| `--csv FILE` | 原始样本输出到 CSV |

## 测量建议

- 测量前固定 CPU 频率：`sudo cpupower frequency-set -g performance`
- 关闭 irqbalance、减少后台负载，或使用 `isolcpus` 隔离 CPU
- 同一组对比实验中保持相同的 CPU 绑定策略
- 关注 `P99.9` 等长尾指标，而非仅平均值

## 输出示例

```
================ notify_benchmark 报告 ================
测量目标      : notify_one() 调用 -> 被唤醒线程开始执行
有效样本数    : 100000
...
min     :     3200.0 ns
max     :   152300.0 ns
avg     :     5230.0 ns
stddev  :     2120.0 ns
median  :     4800.0 ns
P50     :     4800.0 ns
P90     :     6100.0 ns
P95     :     7100.0 ns
P99     :    12300.0 ns
P99.9   :    45600.0 ns
=======================================================
```
