// 系统命令执行器头文件
// 文件名: system_command_executor.h

#ifndef SYSTEM_COMMAND_EXECUTOR_H
#define SYSTEM_COMMAND_EXECUTOR_H

#include "slt_interfaces.h"
#include <atomic>
#include <mutex>
#include <string>
#include <map>

namespace slt {

class SystemCommandExecutor : public CommandExecutor {
public:
    SystemCommandExecutor();
    ~SystemCommandExecutor() override;
    
    // CommandExecutor接口实现
    ExecutionResult execute(const TestCommand& command) override;
    bool isExecuting() const override;
    bool cancelExecution() override;
    
    std::string getLastError() const override;
    
    // 配置选项
    void setWorkingDirectory(const std::string& dir);
    void setEnvironment(const std::map<std::string, std::string>& env);
    
private:
    std::pair<int, std::string> executeShellCommand(const std::string& cmd, int timeoutMs);
    bool killProcess(pid_t pid);
    
private:
    mutable std::mutex mutex_;
    std::atomic<bool> isRunning_;
    std::atomic<pid_t> currentPid_;
    std::string workingDir_;
    std::map<std::string, std::string> environment_;
    std::string lastError_;
};

} // namespace slt

#endif // SYSTEM_COMMAND_EXECUTOR_H