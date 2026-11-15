#include "notify_benchmark.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

namespace {

/**
 * @brief 打印使用说明
 */
void print_usage(const char* prog)
{
    std::cout <<
        "用法: " << prog << " [选项]\n"
        "\n"
        "测量 Linux 用户态线程从 notify_one() 调用到被唤醒线程开始执行的延迟。\n"
        "\n"
        "选项:\n"
        "  -n, --iterations N     有效测量迭代次数（默认 100000）\n"
        "  -w, --warmup N         预热迭代次数（默认 10000，不计入统计）\n"
        "  --cpu-producer N       绑定生产者线程到 CPU N（默认不绑定）\n"
        "  --cpu-consumer N       绑定消费者线程到 CPU N（默认不绑定）\n"
        "  --rt                   使用 SCHED_FIFO 实时调度（需 root/CAP_SYS_NICE）\n"
        "  --rt-priority N        实时优先级 1~99（默认 80）\n"
        "  --settle-us N          生产者收到 ready 后让出 CPU 的微秒数（默认 10）\n"
        "  --csv FILE             将原始样本（纳秒）写入 CSV 文件\n"
        "  -h, --help             显示本帮助\n"
        "\n"
        "示例:\n"
        "  " << prog << " -n 1000000 -w 100000\n"
        "  " << prog << " --cpu-producer 0 --cpu-consumer 1 --rt --csv lat.csv\n";
}

/**
 * @brief 解析非负整数参数
 * @return 解析成功返回 true，失败返回 false
 */
bool parse_uint(const char* s, uint64_t& out)
{
    if (!s || *s == '\0') {
        return false;
    }
    char* end = nullptr;
    unsigned long long v = std::strtoull(s, &end, 10);
    if (end == s || *end != '\0') {
        return false;
    }
    out = static_cast<uint64_t>(v);
    return true;
}

/**
 * @brief 解析整数参数（可带负号，用于 CPU 编号 -1）
 */
bool parse_int(const char* s, int& out)
{
    if (!s || *s == '\0') {
        return false;
    }
    char* end = nullptr;
    long v = std::strtol(s, &end, 10);
    if (end == s || *end != '\0') {
        return false;
    }
    out = static_cast<int>(v);
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    notify_bench::Config cfg;

    // 解析命令行参数
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        auto need_value = [&](const char* opt) -> const char* {
            if (i + 1 >= argc) {
                std::cerr << "[ERROR] 选项 " << opt << " 缺少参数\n";
                return nullptr;
            }
            return argv[++i];
        };

        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "-n" || arg == "--iterations") {
            const char* v = need_value(arg.c_str());
            if (!v || !parse_uint(v, cfg.iterations)) {
                std::cerr << "[ERROR] 无效的迭代次数: " << (v ? v : "") << "\n";
                return 1;
            }
        } else if (arg == "-w" || arg == "--warmup") {
            const char* v = need_value(arg.c_str());
            if (!v || !parse_uint(v, cfg.warmup)) {
                std::cerr << "[ERROR] 无效的预热次数: " << (v ? v : "") << "\n";
                return 1;
            }
        } else if (arg == "--cpu-producer") {
            const char* v = need_value(arg.c_str());
            if (!v || !parse_int(v, cfg.cpu_producer)) {
                std::cerr << "[ERROR] 无效的 CPU 编号: " << (v ? v : "") << "\n";
                return 1;
            }
        } else if (arg == "--cpu-consumer") {
            const char* v = need_value(arg.c_str());
            if (!v || !parse_int(v, cfg.cpu_consumer)) {
                std::cerr << "[ERROR] 无效的 CPU 编号: " << (v ? v : "") << "\n";
                return 1;
            }
        } else if (arg == "--rt") {
            cfg.use_rt = true;
        } else if (arg == "--rt-priority") {
            const char* v = need_value(arg.c_str());
            uint64_t p = 0;
            if (!v || !parse_uint(v, p) || p < 1 || p > 99) {
                std::cerr << "[ERROR] 无效的实时优先级（需 1~99）: " << (v ? v : "") << "\n";
                return 1;
            }
            cfg.rt_priority = static_cast<int>(p);
        } else if (arg == "--settle-us") {
            const char* v = need_value(arg.c_str());
            if (!v || !parse_uint(v, cfg.settle_us)) {
                std::cerr << "[ERROR] 无效的 settle-us: " << (v ? v : "") << "\n";
                return 1;
            }
        } else if (arg == "--csv") {
            const char* v = need_value(arg.c_str());
            if (!v) {
                return 1;
            }
            cfg.csv_path = v;
        } else {
            std::cerr << "[ERROR] 未知选项: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    if (cfg.iterations == 0) {
        std::cerr << "[ERROR] 迭代次数必须大于 0\n";
        return 1;
    }

    std::cout << "开始测量：迭代 " << cfg.iterations
              << " 次，预热 " << cfg.warmup << " 次 ...\n";

    // 运行 benchmark
    auto samples = notify_bench::run_benchmark(cfg);

    // 统计并输出报告
    auto stats = notify_bench::compute_stats(samples);
    notify_bench::print_report(cfg, stats);

    // 可选：输出原始样本 CSV
    if (!cfg.csv_path.empty()) {
        if (notify_bench::write_csv(cfg.csv_path, samples)) {
            std::cout << "原始样本已写入: " << cfg.csv_path << "\n";
        } else {
            return 1;
        }
    }

    return 0;
}
