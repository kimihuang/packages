// SLT控制器实现
// 文件名: slt_controller.cpp

#include "slt_controller.h"
#include "linux_serial_port.h"
#include "slt_command_parser.h"
#include "system_command_executor.h"
#include "pattern_log_processor.h"
#include "slt_utils.h"
#include <iostream>

namespace slt {

SLTController::SLTController(std::unique_ptr<SerialPortHandler> serialHandler,
                             std::unique_ptr<CommandParser> commandParser,
                             std::unique_ptr<CommandExecutor> commandExecutor,
                             std::unique_ptr<LogProcessor> logProcessor)
    : serialHandler_(std::move(serialHandler))
    , commandParser_(std::move(commandParser))
    , commandExecutor_(std::move(commandExecutor))
    , logProcessor_(std::move(logProcessor))
    , isRunning_(false)
    , commandsProcessed_(0) {
}

SLTController::~SLTController() {
    stop();
}

bool SLTController::start() {
    if (isRunning_) {
        lastError_ = "Controller is already running";
        return false;
    }
    
    if (!initializeComponents()) {
        return false;
    }
    
    isRunning_ = true;
    commandsProcessed_ = 0;
    lastError_.clear();
    
    std::cout << "[INFO] SLT Controller started" << std::endl;
    return true;
}

void SLTController::stop() {
    if (!isRunning_) {
        return;
    }
    
    isRunning_ = false;
    cleanup();
    
    std::cout << "[INFO] SLT Controller stopped" << std::endl;
}

ExecutionResult SLTController::processNextCommand() {
    if (!isRunning_) {
        lastError_ = "Controller is not running";
        ExecutionResult result("unknown");
        result.setExitCode(-1);
        result.setErrorMessage(lastError_);
        return result;
    }
    
    // 1. 从串口读取数据
    std::string rawCommand = serialHandler_->readData(1000); // 1秒超时
    if (rawCommand.empty()) {
        lastError_ = "No command received from serial port";
        ExecutionResult result("unknown");
        result.setExitCode(-1);
        result.setErrorMessage(lastError_);
        return result;
    }
    
    if (rawCommand.find("$$") == std::string::npos) {
        // 不是有效的命令格式
        lastError_ = "Invalid command format: missing start marker";
        ExecutionResult result("unknown");
        result.setExitCode(-1);
        result.setErrorMessage(lastError_);
        return result;
    }
    
    // 2. 解析命令
    auto cmd = commandParser_->parseCommand(rawCommand);
    if (!cmd) {
        lastError_ = "Failed to parse command: " + commandParser_->getLastError();
        ExecutionResult result("unknown");
        result.setExitCode(-1);
        result.setErrorMessage(lastError_);
        return result;
    }
    
    // 3. 验证命令
    if (!commandParser_->validateCommand(*cmd)) {
        lastError_ = "Command validation failed: " + commandParser_->getLastError();
        ExecutionResult result(cmd->getId());
        result.setExitCode(-1);
        result.setErrorMessage(lastError_);
        return result;
    }
    
    // 4. 执行命令
    ExecutionResult execResult = commandExecutor_->execute(*cmd);
    
    // 5. 保存日志
    if (execResult.getExitCode() == 0) {
        logProcessor_->saveLogs(cmd->getId(), execResult.getOutput());
        
        // 6. 模式匹配
        std::string logs = logProcessor_->collectLogs(cmd->getId(), 1000);
        if (!logs.empty()) {
            MatchResult matchResult = logProcessor_->matchPattern(logs, cmd->getPattern());
            execResult.setPatternMatchResult(matchResult.isMatched());
        } else {
            execResult.setPatternMatchResult(false);
        }
    }
    
    // 7. 发送结果
    if (!sendResult(execResult)) {
        std::cerr << "[WARNING] Failed to send result back" << std::endl;
    }
    
    // 8. 更新统计
    commandsProcessed_++;
    
    // 9. 清理
    lastError_.clear();
    
    return execResult;
}

// ================== 私有方法 ==================
bool SLTController::initializeComponents() {
    // 检查所有组件是否已创建
    if (!serialHandler_ || !commandParser_ || !commandExecutor_ || !logProcessor_) {
        lastError_ = "Components not properly initialized";
        return false;
    }
    
    // 初始化串口（这里假设配置已设置）
    // 在实际应用中，应该从配置文件加载串口配置
    return true;
}

void SLTController::cleanup() {
    if (serialHandler_ && serialHandler_->isOpen()) {
        serialHandler_->close();
    }
}

bool SLTController::sendResult(const ExecutionResult& result) {
    if (!serialHandler_ || !serialHandler_->isOpen()) {
        return false;
    }
    
    // 构建结果字符串
    std::string resultStr = "$&"; // 结果开始标记
    
    resultStr += "CMD_ID:" + result.getCommandId() + ";";
    resultStr += "EXIT_CODE:" + std::to_string(result.getExitCode()) + ";";
    resultStr += "PATTERN_MATCHED:" + std::string(result.isPatternMatched() ? "1" : "0") + ";";
    resultStr += "EXEC_TIME:" + std::to_string(result.getExecutionTime()) + "ms";
    
    resultStr += "^^"; // 结果结束标记
    
    // 发送结果
    return serialHandler_->writeData(resultStr);
}

} // namespace slt