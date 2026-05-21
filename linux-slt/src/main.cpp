// SLT下位机主程序
// 文件名: main.cpp

#include "slt_interfaces.h"
#include "slt_controller.h"
#include "slt_utils.h"
#include <iostream>
#include <csignal>
#include <atomic>
#include <thread>

// 全局变量用于信号处理
std::atomic<bool> g_running{true};

// 信号处理函数
void signalHandler(int signal) {
    std::cout << "\n[INFO] Received signal " << signal << ", shutting down..." << std::endl;
    g_running = false;
}

void printUsage(const char* programName) {
    std::cout << "SLT (System Level Test) 下位机系统 v1.0" << std::endl;
    std::cout << "用法: " << programName << " [选项]" << std::endl;
    std::cout << "选项:" << std::endl;
    std::cout << "  -c, --config <文件>   配置文件路径 (默认: ./slt_config.yaml)" << std::endl;
    std::cout << "  -p, --port <端口>     串口设备 (默认: /dev/ttyUSB0)" << std::endl;
    std::cout << "  -b, --baud <波特率>   串口波特率 (默认: 115200)" << std::endl;
    std::cout << "  -l, --log <目录>      日志目录 (默认: /var/log/slt/)" << std::endl;
    std::cout << "  --tcp <addr:port>     启用 TCP 模式 (如 0.0.0.0:9999)" << std::endl;
    std::cout << "  -h, --help            显示此帮助信息" << std::endl;
    std::cout << "  -v, --verbose         详细输出模式" << std::endl;
    std::cout << std::endl;
    std::cout << "示例:" << std::endl;
    std::cout << "  " << programName << " --port /dev/ttyS0 --baud 115200" << std::endl;
    std::cout << "  " << programName << " --tcp 0.0.0.0:9999" << std::endl;
    std::cout << "  " << programName << " --config /etc/slt/config.yaml" << std::endl;
}

bool parseCommandLine(int argc, char* argv[], slt::SLTConfig& config, bool& verbose) {
    std::string configFile;
    verbose = false;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return false;
        } else if (arg == "-v" || arg == "--verbose") {
            verbose = true;
        } else if (arg == "-c" || arg == "--config") {
            if (i + 1 < argc) {
                configFile = argv[++i];
            } else {
                std::cerr << "[ERROR] --config 选项需要参数" << std::endl;
                return false;
            }
        } else if (arg == "-p" || arg == "--port") {
            if (i + 1 < argc) {
                config.setSerialPort(argv[++i]);
            } else {
                std::cerr << "[ERROR] --port 选项需要参数" << std::endl;
                return false;
            }
        } else if (arg == "-b" || arg == "--baud") {
            if (i + 1 < argc) {
                try {
                    config.setBaudRate(std::stoi(argv[++i]));
                } catch (...) {
                    std::cerr << "[ERROR] 无效的波特率: " << argv[i] << std::endl;
                    return false;
                }
            } else {
                std::cerr << "[ERROR] --baud 选项需要参数" << std::endl;
                return false;
            }
        } else if (arg == "-l" || arg == "--log") {
            if (i + 1 < argc) {
                config.setLogDirectory(argv[++i]);
            } else {
                std::cerr << "[ERROR] --log 选项需要参数" << std::endl;
                return false;
            }
        } else if (arg == "--tcp") {
            if (i + 1 < argc) {
                std::string tcpArg = argv[++i];
                config.setTcpMode(true);
                size_t colonPos = tcpArg.rfind(':');
                if (colonPos != std::string::npos) {
                    config.setTcpAddr(tcpArg.substr(0, colonPos));
                    try {
                        config.setTcpPort(std::stoi(tcpArg.substr(colonPos + 1)));
                    } catch (...) {
                        std::cerr << "[ERROR] 无效的TCP端口: " << tcpArg << std::endl;
                        return false;
                    }
                } else {
                    try {
                        config.setTcpPort(std::stoi(tcpArg));
                    } catch (...) {
                        std::cerr << "[ERROR] 无效的TCP地址: " << tcpArg << std::endl;
                        return false;
                    }
                }
            } else {
                std::cerr << "[ERROR] --tcp 选项需要参数 (如 0.0.0.0:9999)" << std::endl;
                return false;
            }
        } else {
            std::cerr << "[ERROR] 未知选项: " << arg << std::endl;
            printUsage(argv[0]);
            return false;
        }
    }
    
    // 尝试加载配置文件（按优先级搜索多个位置）
    if (configFile.empty()) {
        // 命令行未指定，自动搜索: 工作目录 → 系统目录
        const char* candidates[] = {
            "./slt_config.yaml",
            "/etc/slt/config.yaml",
            nullptr
        };
        for (int i = 0; candidates[i] != nullptr; ++i) {
            if (slt::utils::fileExists(candidates[i])) {
                configFile = candidates[i];
                break;
            }
        }
    }

    if (!configFile.empty()) {
        if (slt::utils::fileExists(configFile)) {
            if (!config.loadFromFile(configFile)) {
                std::cerr << "[WARNING] 配置文件加载失败，使用默认配置" << std::endl;
            } else if (verbose) {
                std::cout << "[INFO] 已加载配置文件: " << configFile << std::endl;
            }
        }
    } else if (verbose) {
        std::cout << "[INFO] 配置文件不存在，使用默认配置: " << configFile << std::endl;
    }
    
    return true;
}

int main(int argc, char* argv[]) {
    // 设置信号处理
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    
    std::cout << "=== SLT下位机系统启动 ===" << std::endl;
    
    // 解析命令行参数
    slt::SLTConfig config;
    bool verbose = false;
    
    if (!parseCommandLine(argc, argv, config, verbose)) {
        return 1;
    }
    
    // 显示配置信息
    if (verbose) {
        std::cout << "[INFO] 配置信息:" << std::endl;
        std::cout << "  串口: " << config.getSerialPort() << std::endl;
        std::cout << "  波特率: " << config.getBaudRate() << std::endl;
        std::cout << "  日志目录: " << config.getLogDirectory() << std::endl;
        std::cout << "  默认超时: " << config.getDefaultTimeout() << "ms" << std::endl;
        std::cout << "  最大命令大小: " << config.getMaxCommandSize() << "字节" << std::endl;
        if (config.isTcpMode()) {
            std::cout << "  通信模式: TCP" << std::endl;
            std::cout << "  TCP地址: " << config.getTcpAddr() << ":" << config.getTcpPort() << std::endl;
        } else {
            std::cout << "  通信模式: 串口" << std::endl;
        }
    }
    
    try {
        // 创建控制器
        auto controller = slt::ComponentFactory::createController(config);
        if (!controller) {
            std::cerr << "[ERROR] 无法创建控制器" << std::endl;
            return 1;
        }
        
        // 启动控制器
        if (!controller->start(config)) {
            std::cerr << "[ERROR] 无法启动控制器: " << controller->getLastError() << std::endl;
            return 1;
        }
        
        std::cout << "[INFO] SLT控制器已启动，等待命令..." << std::endl;
        std::cout << "[INFO] 按 Ctrl+C 停止程序" << std::endl;
        
        // 主循环
        int commandCount = 0;
        while (g_running) {
            try {
                // 处理下一个命令
                auto result = controller->processNextCommand();
                commandCount++;
                
                if (verbose || result.getExitCode() != 0) {
                    std::cout << "[INFO] 命令处理完成 #" << commandCount << std::endl;
                    std::cout << "  命令ID: " << result.getCommandId() << std::endl;
                    std::cout << "  退出码: " << result.getExitCode() << std::endl;
                    std::cout << "  模式匹配: " << (result.isPatternMatched() ? "成功" : "失败") << std::endl;
                    std::cout << "  执行时间: " << result.getExecutionTime() << "ms" << std::endl;
                    
                    if (!result.getErrorMessage().empty()) {
                        std::cout << "  错误信息: " << result.getErrorMessage() << std::endl;
                    }
                    
                    if (verbose && !result.getOutput().empty()) {
                        std::cout << "  输出预览: " 
                                 << result.getOutput().substr(0, 100)
                                 << (result.getOutput().length() > 100 ? "..." : "")
                                 << std::endl;
                    }
                }
                
                // 短暂休眠，避免CPU占用过高
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                
            } catch (const std::exception& e) {
                std::cerr << "[ERROR] 处理命令时发生异常: " << e.what() << std::endl;
                // 继续处理下一个命令
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            }
        }
        
        // 停止控制器
        controller->stop();
        
        std::cout << "\n[INFO] SLT控制器已停止" << std::endl;
        std::cout << "[INFO] 总共处理命令数: " << controller->getCommandsProcessed() << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] 程序异常: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "=== SLT下位机系统退出 ===" << std::endl;
    return 0;
}