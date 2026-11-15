#include "notify_benchmark.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <numeric>
#include <thread>

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif

namespace notify_bench {

namespace {

/* =====================================================================
 * 高精度时钟
 * ===================================================================== */

/**
 * @brief 读取当前单调时钟（纳秒）
 *
 * 使用 C++ 标准库 std::chrono::steady_clock：单调时钟，纳秒级分辨率，
 * 不受系统时间（NTP/adjtime）调整影响，适合测量微秒级调度延迟。
 * 通过 duration_cast 将时间点转换为纳秒整数，便于跨线程传递与统计。
 */
inline uint64_t now_ns()
{
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<nanoseconds>(steady_clock::now().time_since_epoch())
            .count());
}

/* =====================================================================
 * CPU 亲和性与实时调度
 * ===================================================================== */

#if defined(__linux__)

/**
 * @brief 将当前线程绑定到指定 CPU
 * @return true 成功，false 失败（打印错误信息）
 */
bool pin_thread_to_cpu(std::thread& t, int cpu, const char* role)
{
    if (cpu < 0) {
        return true; // 不绑定
    }

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);

    int rc = pthread_setaffinity_np(t.native_handle(), sizeof(cpu_set_t), &cpuset);
    if (rc != 0) {
        std::cerr << "[WARN] 绑定 " << role << " 线程到 CPU " << cpu
                  << " 失败: " << std::strerror(rc) << "\n";
        return false;
    }
    return true;
}

/**
 * @brief 将当前线程设置为 SCHED_FIFO 实时调度
 * @return true 成功，false 失败（打印错误信息）
 */
bool set_realtime_sched(std::thread& t, int priority, const char* role)
{
    sched_param param{};  // 值初始化清零，避免残留字段干扰
    param.sched_priority = priority;

    int rc = pthread_setschedparam(t.native_handle(), SCHED_FIFO, &param);
    if (rc != 0) {
        std::cerr << "[WARN] 设置 " << role << " 线程为 SCHED_FIFO(prio="
                  << priority << ") 失败: " << std::strerror(rc)
                  << "（可能需要 root 权限或 CAP_SYS_NICE）\n";
        return false;
    }
    return true;
}

#else

bool pin_thread_to_cpu(std::thread&, int, const char*) { return true; }
bool set_realtime_sched(std::thread&, int, const char*) { return true; }

#endif

/* =====================================================================
 * 自旋等待（pause）
 * ===================================================================== */

/**
 * @brief 等待一小段时间，让消费者线程有机会进入阻塞态
 *
 * 生产者检测到消费者 ready 后执行。通过反复调用 std::this_thread::yield()
 * 让出 CPU，使消费者线程得以运行并进入 go_cv.wait() 的真正阻塞状态，
 * 减少「非阻塞样本」混入。
 *
 * 注意：此处必须「让出 CPU」（yield），而非占用 CPU 的空转（busy-wait）。
 * 否则当生产者与消费者绑定到同一 CPU 时，消费者将永远无法获得运行机会。
 * 整个实现完全基于 C++ 标准库，无平台相关汇编。
 */
inline void settle_wait(uint64_t approx_us)
{
    using namespace std::chrono;
    const auto deadline = steady_clock::now() + microseconds(approx_us);
    while (steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
}

/* =====================================================================
 * 核心测量：双条件变量 rendezvous 握手
 * ===================================================================== */

/**
 * @brief benchmark 运行时的共享状态
 */
struct SharedState {
    // 握手：消费者 -> 生产者 的「就绪」信号
    std::mutex              ready_mtx;
    std::condition_variable ready_cv;
    bool                    ready = false;

    // 握手：生产者 -> 消费者 的「go」信号（承载真正的 notify_one）
    std::mutex              go_mtx;
    std::condition_variable go_cv;
    bool                    go = false;

    // T0 的跨线程传递
    std::atomic<uint64_t>   t0_storage{0};

    // 样本收集（仅消费者写，单生产者单消费者，无并发写）
    std::vector<uint64_t>   samples;
};

/**
 * @brief 消费者线程入口
 *
 * 循环：告知就绪 -> 阻塞等待 go -> 醒来第一时间记录 T1 -> 计算延迟。
 */
void consumer_main(SharedState* st, uint64_t total_iters)
{
    st->samples.reserve(total_iters);

    for (uint64_t i = 0; i < total_iters; ++i) {
        // 1. 告知生产者「我已就绪」
        {
            std::lock_guard<std::mutex> lk(st->ready_mtx);
            st->ready = true;
        }
        st->ready_cv.notify_one();

        // 2. 阻塞等待 go 信号（真正的唤醒点）
        {
            std::unique_lock<std::mutex> lk(st->go_mtx);
            st->go_cv.wait(lk, [st] { return st->go; });
        }

        // 3. 醒来后的第一条指令：记录 T1
        const uint64_t t1 = now_ns();
        const uint64_t t0 = st->t0_storage.load(std::memory_order_acquire);

        // 4. 记录本轮延迟，并复位 go 供下一轮使用
        st->samples.push_back(t1 - t0);
        st->go = false;
    }
}

/**
 * @brief 生产者线程入口
 *
 * 循环：等待就绪 -> 记录 T0 -> notify_one 唤醒消费者。
 */
void producer_main(SharedState* st, uint64_t total_iters, uint64_t settle_us)
{
    for (uint64_t i = 0; i < total_iters; ++i) {
        // 1. 等待消费者就绪
        {
            std::unique_lock<std::mutex> lk(st->ready_mtx);
            st->ready_cv.wait(lk, [st] { return st->ready; });
            st->ready = false;
        }

        // 2. （可选）让出 CPU，确保消费者已进入阻塞态
        if (settle_us > 0) {
            settle_wait(settle_us);
        }

        // 3. 记录 T0（notify_one 调用之前）
        const uint64_t t0 = now_ns();
        st->t0_storage.store(t0, std::memory_order_release);

        // 4. 发送 go 信号，唤醒消费者
        {
            std::lock_guard<std::mutex> lk(st->go_mtx);
            st->go = true;
        }
        st->go_cv.notify_one();
    }
}

} // namespace

/* =====================================================================
 * 对外接口实现
 * ===================================================================== */

std::vector<uint64_t> run_benchmark(const Config& cfg)
{
    SharedState st;
    const uint64_t total_iters = cfg.iterations + cfg.warmup;
    st.samples.reserve(total_iters);

    std::thread producer([&] {
        producer_main(&st, total_iters, cfg.settle_us);
    });
    std::thread consumer([&] {
        consumer_main(&st, total_iters);
    });

    // 实时调度需在 start 之后设置（pthread 参数作用于线程本身）
    if (cfg.use_rt) {
        set_realtime_sched(producer, cfg.rt_priority, "生产者");
        set_realtime_sched(consumer, cfg.rt_priority, "消费者");
    }

    // CPU 亲和性
    pin_thread_to_cpu(producer, cfg.cpu_producer, "生产者");
    pin_thread_to_cpu(consumer, cfg.cpu_consumer, "消费者");

    producer.join();
    consumer.join();

    // 丢弃预热阶段的样本（前 warmup 个）
    std::vector<uint64_t> result;
    if (cfg.warmup > 0) {
        result.assign(st.samples.begin() + cfg.warmup, st.samples.end());
    } else {
        result = std::move(st.samples);
    }
    return result;
}

Stats compute_stats(const std::vector<uint64_t>& samples)
{
    Stats s;
    s.count = samples.size();
    if (s.count == 0) {
        return s;
    }

    const long double n = static_cast<long double>(s.count);

    // min / max：std::minmax_element 一次遍历得到
    const auto [mn_it, mx_it] = std::minmax_element(samples.begin(), samples.end());
    s.min_ns = static_cast<double>(*mn_it);
    s.max_ns = static_cast<double>(*mx_it);

    // 平均值：std::accumulate 求和
    const long double sum = std::accumulate(
        samples.begin(), samples.end(), 0.0L,
        [](long double acc, uint64_t v) {
            return acc + static_cast<long double>(v);
        });
    const long double mean = sum / n;
    s.avg_ns = static_cast<double>(mean);

    // 标准差：std::accumulate + lambda 累计平方差
    const long double sq_sum = std::accumulate(
        samples.begin(), samples.end(), 0.0L,
        [mean](long double acc, uint64_t v) {
            const long double d = static_cast<long double>(v) - mean;
            return acc + d * d;
        });
    s.stddev_ns = static_cast<double>(std::sqrt(sq_sum / n));

    // 百分位：std::nth_element 仅将目标位置就位，避免全量排序
    std::vector<uint64_t> sorted(samples);
    const size_t count = sorted.size();

    // 分位数索引：rank = p * (n - 1)，采用最近秩（nearest-rank）取整
    auto percentile = [&](double p) -> double {
        size_t idx = static_cast<size_t>(std::llround(p * (n - 1)));
        if (idx >= count) idx = count - 1;
        std::nth_element(sorted.begin(), sorted.begin() + idx, sorted.end());
        return static_cast<double>(sorted[idx]);
    };

    s.p50_ns    = percentile(0.50);
    s.median_ns = s.p50_ns;
    s.p90_ns    = percentile(0.90);
    s.p95_ns    = percentile(0.95);
    s.p99_ns    = percentile(0.99);
    s.p999_ns   = percentile(0.999);

    return s;
}

void print_report(const Config& cfg, const Stats& s)
{
    std::cout << std::fixed << std::setprecision(1);
    std::cout << "================ notify_benchmark 报告 ================\n";
    std::cout << "测量目标      : notify_one() 调用 -> 被唤醒线程开始执行\n";
    std::cout << "有效样本数    : " << s.count << "\n";
    std::cout << "预热迭代      : " << cfg.warmup << "\n";
    std::cout << "CPU 绑定      : 生产者="
              << (cfg.cpu_producer >= 0 ? std::to_string(cfg.cpu_producer) : "无")
              << " 消费者="
              << (cfg.cpu_consumer >= 0 ? std::to_string(cfg.cpu_consumer) : "无")
              << "\n";
    std::cout << "实时调度      : "
              << (cfg.use_rt ? ("SCHED_FIFO(prio=" + std::to_string(cfg.rt_priority) + ")")
                             : "无（CFS）")
              << "\n";
    std::cout << "-------------------------------------------------------\n";
    std::cout << "min     : " << std::setw(12) << s.min_ns    << " ns\n";
    std::cout << "max     : " << std::setw(12) << s.max_ns    << " ns\n";
    std::cout << "avg     : " << std::setw(12) << s.avg_ns    << " ns\n";
    std::cout << "stddev  : " << std::setw(12) << s.stddev_ns << " ns\n";
    std::cout << "median  : " << std::setw(12) << s.median_ns << " ns\n";
    std::cout << "P50     : " << std::setw(12) << s.p50_ns    << " ns\n";
    std::cout << "P90     : " << std::setw(12) << s.p90_ns    << " ns\n";
    std::cout << "P95     : " << std::setw(12) << s.p95_ns    << " ns\n";
    std::cout << "P99     : " << std::setw(12) << s.p99_ns    << " ns\n";
    std::cout << "P99.9   : " << std::setw(12) << s.p999_ns   << " ns\n";
    std::cout << "=======================================================\n";

    // 提示：若 min 异常接近 0，可能存在未真正阻塞的样本
    if (s.min_ns < 1000.0) {
        std::cout << "[提示] min 值异常偏小（<1us），可能存在消费者尚未真正阻塞\n"
                  << "       就被通知的样本。可增大 --settle-us 并重新测量。\n";
    }
}

bool write_csv(const std::string& path, const std::vector<uint64_t>& samples)
{
    std::ofstream ofs(path);
    if (!ofs.is_open()) {
        std::cerr << "[ERROR] 无法打开 CSV 文件: " << path << "\n";
        return false;
    }

    ofs << "index,latency_ns\n";
    for (size_t i = 0; i < samples.size(); ++i) {
        ofs << i << "," << samples[i] << "\n";
    }
    ofs.close();
    return ofs.good();
}

} // namespace notify_bench
