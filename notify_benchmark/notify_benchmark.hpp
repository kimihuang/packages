#ifndef NOTIFY_BENCHMARK_HPP
#define NOTIFY_BENCHMARK_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace notify_bench {

/**
 * @brief benchmark 运行配置
 *
 * 所有字段均可通过命令行参数覆盖，见 main.cpp。
 */
struct Config {
    uint64_t iterations = 100000;   // 有效测量迭代次数
    uint64_t warmup     = 10000;    // 预热迭代次数（不计入统计）
    int      cpu_producer = -1;     // 生产者线程 CPU 亲和性，-1 表示不绑定
    int      cpu_consumer = -1;     // 消费者线程 CPU 亲和性，-1 表示不绑定
    bool     use_rt       = false;  // 是否使用 SCHED_FIFO 实时调度
    int      rt_priority  = 80;     // 实时优先级（1~99）
    uint64_t settle_us    = 10;     // 生产者收到 ready 后的自旋等待微秒数
    std::string csv_path;           // 原始样本 CSV 输出路径，空表示不输出
};

/**
 * @brief 统计汇总结果
 *
 * 时间单位统一为纳秒（ns）。
 */
struct Stats {
    uint64_t count = 0;         // 有效样本数
    double   min_ns   = 0.0;    // 最小值
    double   max_ns   = 0.0;    // 最大值
    double   avg_ns   = 0.0;    // 平均值
    double   stddev_ns= 0.0;    // 标准差
    double   median_ns= 0.0;    // 中位数（P50）
    double   p50_ns   = 0.0;
    double   p90_ns   = 0.0;
    double   p95_ns   = 0.0;
    double   p99_ns   = 0.0;
    double   p999_ns  = 0.0;
};

/**
 * @brief 运行唤醒延迟 benchmark
 *
 * 启动生产者 / 消费者两个线程，执行 rendezvous 握手并采集
 * 「notify_one 调用到被唤醒线程开始执行」的延迟样本。
 *
 * @param cfg 运行配置
 * @return 原始样本（单位：纳秒），长度等于 cfg.iterations
 */
std::vector<uint64_t> run_benchmark(const Config& cfg);

/**
 * @brief 计算统计汇总
 */
Stats compute_stats(const std::vector<uint64_t>& samples);

/**
 * @brief 打印统计报告到 stdout
 */
void print_report(const Config& cfg, const Stats& stats);

/**
 * @brief 将原始样本按行写入 CSV 文件（单位：纳秒）
 * @return true 成功，false 失败
 */
bool write_csv(const std::string& path, const std::vector<uint64_t>& samples);

} // namespace notify_bench

#endif // NOTIFY_BENCHMARK_HPP
