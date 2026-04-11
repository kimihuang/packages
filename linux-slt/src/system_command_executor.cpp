// 系统命令执行器实现
// 文件名: system_command_executor.cpp

#include "system_command_executor.h"
#include "slt_utils.h"
#include <chrono>
#include <cstring>
#include <signal.h>
#include <sys/wait.h>
#include <thread>

namespace slt {

SystemCommandExecutor::SystemCommandExecutor() 
    : isRunning_(false)
    , currentPid_(0) {
}

SystemCommandExecutor::~SystemCommandExecutor() {
    cancelExecution();
}

ExecutionResult SystemCommandExecutor::execute(const TestCommand& command) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (isRunning_) {
        lastError_ = "Another command is already executing";
        ExecutionResult result(command.getId());
        result.setExitCode(-1);
        result.setErrorMessage(lastError_);
        return result;
    }
    
    isRunning_ = true;
    auto startTime = std::chrono::steady_clock::now();
    
    // 执行命令
    auto [exitCode, output] = executeShellCommand(
        command.getShellCommand(), 
        command.getTimeout()
    );
    
    auto endTime = std::chrono::steady_clock::now();
    auto execTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        endTime - startTime);
    
    ExecutionResult result(command.getId());
    result.setExitCode(exitCode);
    result.setOutput(output);
    result.setExecutionTime(static_cast<int>(execTime.count()));
    
    if (exitCode == 0) {
        result.setErrorMessage("");
    } else {
        result.setErrorMessage("Command execution failed with exit code " + 
                             std::to_string(exitCode));
    }
    
    isRunning_ = false;
    currentPid_ = 0;
    lastError_.clear();
    
    return result;
}

bool SystemCommandExecutor::isExecuting() const {
    return isRunning_;
}

bool SystemCommandExecutor::cancelExecution() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!isRunning_ || currentPid_ == 0) {
        return false;
    }
    
    // 发送SIGTERM信号
    if (kill(currentPid_, SIGTERM) == 0) {
        // 等待进程结束
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // 如果还在运行，发送SIGKILL
        if (kill(currentPid_, 0) == 0) {
            kill(currentPid_, SIGKILL);
        }
        
        isRunning_ = false;
        currentPid_ = 0;
        lastError_ = "Command execution cancelled";
        return true;
    }
    
    lastError_ = "Failed to cancel command execution";
    return false;
}

std::string SystemCommandExecutor::getLastError() const {
    return lastError_;
}

void SystemCommandExecutor::setWorkingDirectory(const std::string& dir) {
    std::lock_guard<std::mutex> lock(mutex_);
    workingDir_ = dir;
}

void SystemCommandExecutor::setEnvironment(const std::map<std::string, std::string>& env) {
    std::lock_guard<std::mutex> lock(mutex_);
    environment_ = env;
}

// ================== 私有方法 ==================
std::pair<int, std::string> SystemCommandExecutor::executeShellCommand(
    const std::string& cmd, int timeoutMs) {
    
    return utils::executeShellCommand(cmd, timeoutMs);
}

bool SystemCommandExecutor::killProcess(pid_t pid) {
    if (pid <= 0) {
        return false;
    }
    
    // 发送SIGTERM信号
    if (kill(pid, SIGTERM) == 0) {
        // 等待进程结束
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // 如果还在运行，发送SIGKILL
        if (kill(pid, 0) == 0) {
            kill(pid, SIGKILL);
        }
        return true;
    }
    
    return false;
}

} // namespace slt