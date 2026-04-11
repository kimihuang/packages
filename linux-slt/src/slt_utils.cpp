// SLT工具函数实现
// 文件名: slt_utils.cpp

#include "slt_utils.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <regex>
#include <thread>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>

namespace slt {
namespace utils {

// ================== 字符串工具 ==================
std::string trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) {
        return "";
    }
    
    size_t end = str.find_last_not_of(" \t\n\r");
    return str.substr(start, end - start + 1);
}

std::vector<std::string> split(const std::string& str, char delimiter) {
    std::vector<std::string> result;
    std::stringstream ss(str);
    std::string item;
    
    while (std::getline(ss, item, delimiter)) {
        if (!item.empty()) {
            result.push_back(item);
        }
    }
    
    return result;
}

bool startsWith(const std::string& str, const std::string& prefix) {
    return str.length() >= prefix.length() && 
           str.compare(0, prefix.length(), prefix) == 0;
}

bool endsWith(const std::string& str, const std::string& suffix) {
    return str.length() >= suffix.length() && 
           str.compare(str.length() - suffix.length(), suffix.length(), suffix) == 0;
}

// ================== 文件操作 ==================
bool fileExists(const std::string& path) {
    struct stat buffer;
    return stat(path.c_str(), &buffer) == 0;
}

std::string readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return "";
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

bool writeFile(const std::string& path, const std::string& content) {
    std::ofstream file(path);
    if (!file.is_open()) {
        return false;
    }
    
    file << content;
    return file.good();
}

bool createDirectory(const std::string& path) {
    if (fileExists(path)) {
        struct stat statBuf;
        if (stat(path.c_str(), &statBuf) == 0) {
            return S_ISDIR(statBuf.st_mode);
        }
        return false;
    }
    
    // 递归创建目录
    size_t pos = 0;
    std::string dirPath = path;
    
    // 确保路径以'/'结尾
    if (!dirPath.empty() && dirPath.back() != '/') {
        dirPath += '/';
    }
    
    while ((pos = dirPath.find('/', pos + 1)) != std::string::npos) {
        std::string subDir = dirPath.substr(0, pos);
        if (!fileExists(subDir)) {
            if (mkdir(subDir.c_str(), 0755) != 0) {
                return false;
            }
        }
    }
    
    return true;
}

// ================== 时间工具 ==================
std::string getCurrentTimeString() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm* tm = std::localtime(&time);
    
    char buffer[64];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", tm);
    return std::string(buffer);
}

int64_t getCurrentTimeMs() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
}

// ================== 随机ID生成 ==================
std::string generateId() {
    static std::atomic<int> counter{0};
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 9999);
    
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    
    std::stringstream ss;
    ss << "cmd_" << timestamp << "_" << dis(gen) << "_" << ++counter;
    return ss.str();
}

// ================== 命令行解析 ==================
std::map<std::string, std::string> parseCommandLine(const std::string& cmdLine) {
    std::map<std::string, std::string> result;
    std::string currentKey;
    std::string currentValue;
    bool inQuotes = false;
    
    for (size_t i = 0; i < cmdLine.length(); ++i) {
        char c = cmdLine[i];
        
        if (c == '"') {
            inQuotes = !inQuotes;
        } else if (!inQuotes && c == ' ') {
            if (!currentKey.empty() && !currentValue.empty()) {
                result[currentKey] = currentValue;
                currentKey.clear();
                currentValue.clear();
            }
        } else if (!inQuotes && c == '-') {
            // 新参数开始
            if (!currentKey.empty() && !currentValue.empty()) {
                result[currentKey] = currentValue;
            }
            
            // 查找参数名
            size_t nextSpace = cmdLine.find(' ', i + 1);
            if (nextSpace == std::string::npos) {
                currentKey = cmdLine.substr(i + 1);
                i = cmdLine.length();
            } else {
                // 检查是否是短参数组合如 -vc
                size_t nextDash = cmdLine.find('-', i + 1);
                if (nextDash != std::string::npos && nextDash < nextSpace) {
                    // 找到下一个-，说明是单独的短参数
                    currentKey = cmdLine.substr(i + 1, nextDash - i - 1);
                    i = nextDash - 1;
                } else {
                    currentKey = cmdLine.substr(i + 1, nextSpace - i - 1);
                    i = nextSpace;
                }
            }
            currentValue.clear();
        } else {
            if (currentKey.empty()) {
                // 跳过参数前的空格
                continue;
            } else {
                currentValue += c;
            }
        }
    }
    
    // 添加最后一个参数
    if (!currentKey.empty() && !currentValue.empty()) {
        result[currentKey] = currentValue;
    }
    
    // 处理短参数组合如 -vc
    std::map<std::string, std::string> finalResult;
    for (const auto& entry : result) {
        const std::string& key = entry.first;
        if (key.length() > 1 && key.find_first_of("cptv") == 0) {
            // 这是短参数组合，如-vc，需要拆分为v和c
            for (char c : key) {
                if (c == 'v') {
                    finalResult["v"] = "true";
                } else if (c == 'c' || c == 'p' || c == 't') {
                    finalResult[std::string(1, c)] = entry.second;
                }
            }
        } else {
            finalResult[key] = entry.second;
        }
    }
    
    return finalResult;
}

// ================== 正则表达式匹配 ==================
bool regexMatch(const std::string& text, const std::string& pattern) {
    try {
        std::regex regexPattern(pattern);
        return std::regex_search(text, regexPattern);
    } catch (const std::regex_error& e) {
        // 正则表达式无效，回退到简单字符串查找
        return text.find(pattern) != std::string::npos;
    }
}

// ================== 系统命令执行 ==================
namespace {

struct ChildProcess {
    pid_t pid;
    int stdoutFd;
    int stderrFd;
    std::thread monitorThread;
    std::atomic<bool> running{true};
};

void childProcessMonitor(ChildProcess* process, int timeoutMs, std::atomic<bool>* timedOut) {
    auto startTime = std::chrono::steady_clock::now();
    
    while (process->running) {
        auto currentTime = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            currentTime - startTime).count();
        
        if (elapsed >= timeoutMs) {
            *timedOut = true;
            kill(process->pid, SIGKILL);
            break;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

} // 匿名命名空间

std::pair<int, std::string> executeShellCommand(const std::string& cmd, int timeoutMs) {
    // 创建管道用于读取子进程输出
    int stdoutPipe[2];
    int stderrPipe[2];
    
    if (pipe(stdoutPipe) == -1 || pipe(stderrPipe) == -1) {
        return {-1, "Failed to create pipes: " + std::string(strerror(errno))};
    }
    
    // 创建子进程
    pid_t pid = fork();
    if (pid == -1) {
        close(stdoutPipe[0]);
        close(stdoutPipe[1]);
        close(stderrPipe[0]);
        close(stderrPipe[1]);
        return {-1, "Failed to fork: " + std::string(strerror(errno))};
    }
    
    if (pid == 0) {
        // 子进程
        close(stdoutPipe[0]);
        close(stderrPipe[0]);
        
        // 重定向标准输出到管道
        dup2(stdoutPipe[1], STDOUT_FILENO);
        dup2(stderrPipe[1], STDERR_FILENO);
        close(stdoutPipe[1]);
        close(stderrPipe[1]);
        
        // 执行命令
        execl("/bin/sh", "sh", "-c", cmd.c_str(), (char*)NULL);
        
        // 如果execl失败
        exit(127);
    } else {
        // 父进程
        close(stdoutPipe[1]);
        close(stderrPipe[1]);
        
        ChildProcess process;
        process.pid = pid;
        process.stdoutFd = stdoutPipe[0];
        process.stderrFd = stderrPipe[0];
        
        std::atomic<bool> timedOut{false};
        process.monitorThread = std::thread(childProcessMonitor, &process, timeoutMs, &timedOut);
        
        // 读取输出
        std::string output;
        char buffer[4096];
        ssize_t bytesRead;
        
        // 设置文件描述符为非阻塞
        int flags = fcntl(stdoutPipe[0], F_GETFL, 0);
        fcntl(stdoutPipe[0], F_SETFL, flags | O_NONBLOCK);
        
        while (true) {
            // 检查子进程是否结束
            int status;
            pid_t result = waitpid(pid, &status, WNOHANG);
            
            if (result == pid) {
                // 子进程已结束
                process.running = false;
                break;
            } else if (result == -1) {
                // 错误
                process.running = false;
                break;
            }
            
            // 读取数据
            bytesRead = read(stdoutPipe[0], buffer, sizeof(buffer) - 1);
            if (bytesRead > 0) {
                buffer[bytesRead] = '\0';
                output += buffer;
            } else if (bytesRead == 0) {
                // 管道关闭
                break;
            } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                // 读取错误
                break;
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        // 等待监控线程结束
        if (process.monitorThread.joinable()) {
            process.monitorThread.join();
        }
        
        // 关闭文件描述符
        close(stdoutPipe[0]);
        close(stderrPipe[0]);
        
        // 获取退出状态
        int status;
        waitpid(pid, &status, 0);
        
        int exitCode = 0;
        if (WIFEXITED(status)) {
            exitCode = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            exitCode = 128 + WTERMSIG(status);
            if (timedOut) {
                output += "\n[ERROR] Command execution timeout";
            } else {
                output += "\n[ERROR] Command terminated by signal: " + 
                         std::to_string(WTERMSIG(status));
            }
        }
        
        return {exitCode, output};
    }
}

} // namespace utils
} // namespace slt