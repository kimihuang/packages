// SLT下位机系统接口定义
// 文件名: slt_interfaces.h
// 描述: 定义SLT系统的核心接口和数据结构

#ifndef SLT_INTERFACES_H
#define SLT_INTERFACES_H

#include <memory>
#include <string>
#include <map>
#include <vector>
#include <chrono>

namespace slt {

// ================== 错误码定义 ==================
enum class ErrorCode {
    SUCCESS = 0,
    SERIAL_PORT_ERROR = -1,
    COMMAND_PARSE_ERROR = -2,
    COMMAND_EXECUTION_ERROR = -3,
    TIMEOUT_ERROR = -4,
    LOG_PROCESS_ERROR = -5,
    CONFIG_ERROR = -6,
    UNKNOWN_ERROR = -99
};

// ================== 前向声明 ==================
class TestCommand;
class ExecutionResult;
class MatchResult;

// ================== 核心接口 ==================
class CommandParser {
public:
    virtual ~CommandParser() = default;
    
    virtual std::unique_ptr<TestCommand> parseCommand(const std::string& rawData) = 0;
    virtual bool validateCommand(const TestCommand& command) = 0;
    
    virtual std::string getLastError() const = 0;
};

class SerialPortHandler {
public:
    virtual ~SerialPortHandler() = default;
    
    virtual bool open(const std::string& portName, int baudRate) = 0;
    virtual std::string readData(int timeoutMs) = 0;
    virtual bool writeData(const std::string& data) = 0;
    virtual void close() = 0;
    
    virtual bool isOpen() const = 0;
    virtual std::string getLastError() const = 0;
};

class CommandExecutor {
public:
    virtual ~CommandExecutor() = default;
    
    virtual ExecutionResult execute(const TestCommand& command) = 0;
    virtual bool isExecuting() const = 0;
    virtual bool cancelExecution() = 0;
    
    virtual std::string getLastError() const = 0;
};

class LogProcessor {
public:
    virtual ~LogProcessor() = default;
    
    virtual std::string collectLogs(const std::string& commandId, int timeoutMs) = 0;
    virtual MatchResult matchPattern(const std::string& logText, const std::string& pattern) = 0;
    virtual bool saveLogs(const std::string& commandId, const std::string& logData) = 0;
    
    virtual std::string getLastError() const = 0;
};

// ================== 数据模型类 ==================
class TestCommand {
public:
    TestCommand(const std::string& id, 
                const std::string& name,
                const std::string& shellCommand,
                const std::string& pattern,
                int timeoutMs,
                bool verbose = false);
    
    // Getters
    std::string getId() const { return id_; }
    std::string getName() const { return name_; }
    std::string getShellCommand() const { return shellCommand_; }
    std::string getPattern() const { return pattern_; }
    int getTimeout() const { return timeoutMs_; }
    bool isVerbose() const { return verbose_; }
    
    // 参数管理
    void setParameter(const std::string& key, const std::string& value);
    std::string getParameter(const std::string& key, const std::string& defaultValue = "") const;
    
    // 检查是否存在参数
    bool hasParameter(const std::string& key) const;
    
    // 获取所有参数
    std::map<std::string, std::string> getParameters() const { return parameters_; }
    
private:
    std::string id_;
    std::string name_;
    std::string shellCommand_;
    std::string pattern_;
    int timeoutMs_;
    bool verbose_;
    std::map<std::string, std::string> parameters_;
};

class ExecutionResult {
public:
    ExecutionResult(const std::string& commandId);
    
    // Setters
    void setExitCode(int code) { exitCode_ = code; }
    void setOutput(const std::string& output) { output_ = output; }
    void setErrorMessage(const std::string& errorMessage) { errorMessage_ = errorMessage; }
    void setExecutionTime(int executionTimeMs) { executionTimeMs_ = executionTimeMs; }
    void setPatternMatchResult(bool matched) { patternMatchResult_ = matched; }
    
    // Getters
    std::string getCommandId() const { return commandId_; }
    int getExitCode() const { return exitCode_; }
    std::string getOutput() const { return output_; }
    std::string getErrorMessage() const { return errorMessage_; }
    int getExecutionTime() const { return executionTimeMs_; }
    bool isPatternMatched() const { return patternMatchResult_; }
    
    // 结果判断
    bool isSuccess() const { return exitCode_ == 0 && patternMatchResult_; }
    
private:
    std::string commandId_;
    int exitCode_;
    std::string output_;
    std::string errorMessage_;
    int executionTimeMs_;
    bool patternMatchResult_;
};

class MatchResult {
public:
    MatchResult(bool matched, int matchCount = 0, 
                const std::string& matchedText = "", int position = -1);
    
    // Getters
    bool isMatched() const { return matched_; }
    int getMatchCount() const { return matchCount_; }
    std::string getMatchedText() const { return matchedText_; }
    int getPosition() const { return position_; }
    
private:
    bool matched_;
    int matchCount_;
    std::string matchedText_;
    int position_;
};

// ================== 配置类 ==================
class SLTConfig {
public:
    SLTConfig();
    
    // 加载和保存配置
    bool loadFromFile(const std::string& configPath);
    bool saveToFile(const std::string& configPath);
    
    // Getters
    std::string getSerialPort() const { return serialPort_; }
    int getBaudRate() const { return baudRate_; }
    std::string getLogDirectory() const { return logDirectory_; }
    int getDefaultTimeout() const { return defaultTimeout_; }
    int getMaxCommandSize() const { return maxCommandSize_; }
    
    // Setters
    void setSerialPort(const std::string& port) { serialPort_ = port; }
    void setBaudRate(int baudRate) { baudRate_ = baudRate; }
    void setLogDirectory(const std::string& dir) { logDirectory_ = dir; }
    void setDefaultTimeout(int timeout) { defaultTimeout_ = timeout; }
    void setMaxCommandSize(int size) { maxCommandSize_ = size; }
    
private:
    std::string serialPort_;
    int baudRate_;
    std::string logDirectory_;
    int defaultTimeout_;
    int maxCommandSize_;
};

// ================== 控制器类 ==================
// 完整定义见 slt_controller.h
class SLTController;

// ================== 工厂类 ==================
class ComponentFactory {
public:
    static std::unique_ptr<SerialPortHandler> createSerialPortHandler();
    static std::unique_ptr<CommandParser> createCommandParser();
    static std::unique_ptr<CommandExecutor> createCommandExecutor();
    static std::unique_ptr<LogProcessor> createLogProcessor();
    static std::unique_ptr<SLTController> createController(const SLTConfig& config);
    
    // 创建带配置的组件
    static std::unique_ptr<SerialPortHandler> createSerialPortHandler(const SLTConfig& config);
    static std::unique_ptr<LogProcessor> createLogProcessor(const SLTConfig& config);
};

// ================== 工具函数 ==================
namespace utils {
    // 字符串工具
    std::string trim(const std::string& str);
    std::vector<std::string> split(const std::string& str, char delimiter);
    bool startsWith(const std::string& str, const std::string& prefix);
    bool endsWith(const std::string& str, const std::string& suffix);
    
    // 文件操作
    bool fileExists(const std::string& path);
    std::string readFile(const std::string& path);
    bool writeFile(const std::string& path, const std::string& content);
    
    // 时间工具
    std::string getCurrentTimeString();
    int64_t getCurrentTimeMs();
    
    // 随机ID生成
    std::string generateId();
    
    // 命令行解析
    std::map<std::string, std::string> parseCommandLine(const std::string& cmdLine);
}

} // namespace slt

#endif // SLT_INTERFACES_H