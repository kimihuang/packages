# SLT下位机系统架构设计文档

## 1. 系统概述

SLT (System Level Test) 下位机系统用于从串口接收上位机发送的测试命令，解析并执行Shell命令，然后分析执行日志，根据模式匹配返回测试结果。

### 1.1 核心功能
1. **串口通信**：接收上位机命令，返回测试结果
2. **命令解析**：解析特定格式的测试命令
3. **命令执行**：执行Shell命令并监控超时
4. **日志分析**：收集日志并进行模式匹配
5. **结果返回**：返回匹配结果(0表示成功，-1表示失败)

### 1.2 命令格式
```
slt_test_cmd_start:$$
<测试命令>
slt_test_cmd_end:^^
```

测试命令示例：
```
ACPU_TEST_0001 -v -c "echo hello world" -p "hello world" -t 1000
```

参数说明：
- `-v`: verbose模式，打印详细信息
- `-c`: 要执行的Shell命令
- `-p`: 匹配日志的模式
- `-t`: 超时时间(毫秒)

## 2. 系统架构

### 2.1 架构图
```
┌─────────────────────────────────────────────────┐
│                 SLT控制器 (SLTController)       │
├─────────────────────────────────────────────────┤
│  ┌─────────┐  ┌──────────┐  ┌──────────┐       │
│  │串口处理 │  │命令解析器│  │命令执行器│       │
│  │SerialPort│ │Command   │  │Executor  │       │
│  └─────────┘  └──────────┘  └──────────┘       │
│                                                │
│  ┌──────────────────────────────────────────┐  │
│  │            日志处理器 (LogProcessor)      │  │
│  └──────────────────────────────────────────┘  │
└─────────────────────────────────────────────────┘
```

### 2.2 核心组件

#### 2.2.1 SLT控制器 (SLTController)
**职责**：协调所有组件，实现完整的命令处理流程

**主要方法**：
- `start()`: 启动下位机服务
- `stop()`: 停止服务
- `processCommand()`: 处理单个命令
- `getStatus()`: 获取系统状态

#### 2.2.2 串口处理器 (ISerialPort)
**接口定义**：
```cpp
class ISerialPort {
public:
    virtual bool open(const std::string& port, int baudRate) = 0;
    virtual std::string read(int timeoutMs) = 0;
    virtual bool write(const std::string& data) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
};
```

#### 2.2.3 命令解析器 (ICommandParser)
**接口定义**：
```cpp
class ICommandParser {
public:
    virtual std::unique_ptr<TestCommand> parse(const std::string& rawData) = 0;
    virtual bool validate(const TestCommand& cmd) = 0;
    virtual std::string getError() const = 0;
};
```

#### 2.2.4 命令执行器 (ICommandExecutor)
**接口定义**：
```cpp
class ICommandExecutor {
public:
    virtual ExecutionResult execute(const TestCommand& cmd) = 0;
    virtual bool cancel() = 0;
    virtual bool isExecuting() const = 0;
};
```

#### 2.2.5 日志处理器 (ILogProcessor)
**接口定义**：
```cpp
class ILogProcessor {
public:
    virtual std::string collectLogs(const std::string& commandId) = 0;
    virtual bool matchPattern(const std::string& logs, const std::string& pattern) = 0;
    virtual bool saveLogs(const std::string& commandId, const std::string& logs) = 0;
};
```

## 3. 数据模型设计

### 3.1 测试命令 (TestCommand)
```cpp
class TestCommand {
private:
    std::string id_;           // 唯一标识符
    std::string name_;         // 测试名称，如"ACPU_TEST_0001"
    std::string shellCmd_;     // Shell命令
    std::string pattern_;      // 匹配模式
    int timeoutMs_;           // 超时时间(毫秒)
    bool verbose_;            // 详细输出标志
    std::map<std::string, std::string> params_; // 其他参数
    
public:
    // 构造函数
    TestCommand(const std::string& name, const std::string& shellCmd,
                const std::string& pattern, int timeoutMs, bool verbose = false);
    
    // Getters
    const std::string& getId() const { return id_; }
    const std::string& getName() const { return name_; }
    const std::string& getShellCmd() const { return shellCmd_; }
    const std::string& getPattern() const { return pattern_; }
    int getTimeoutMs() const { return timeoutMs_; }
    bool isVerbose() const { return verbose_; }
    
    // 参数管理
    void setParam(const std::string& key, const std::string& value);
    std::string getParam(const std::string& key, const std::string& defaultValue = "") const;
    
    // 生成ID
    static std::string generateId();
};
```

### 3.2 执行结果 (ExecutionResult)
```cpp
class ExecutionResult {
private:
    std::string commandId_;     // 对应的命令ID
    int exitCode_;             // 退出码
    std::string output_;       // 命令输出
    std::string error_;        // 错误信息
    bool patternMatched_;      // 模式是否匹配
    int64_t execTimeMs_;       // 执行时间(毫秒)
    
public:
    ExecutionResult(const std::string& cmdId);
    
    // Setters
    void setExitCode(int code) { exitCode_ = code; }
    void setOutput(const std::string& output) { output_ = output; }
    void setError(const std::string& error) { error_ = error; }
    void setPatternMatched(bool matched) { patternMatched_ = matched; }
    void setExecTimeMs(int64_t time) { execTimeMs_ = time; }
    
    // Getters
    const std::string& getCommandId() const { return commandId_; }
    int getExitCode() const { return exitCode_; }
    const std::string& getOutput() const { return output_; }
    const std::string& getError() const { return error_; }
    bool isPatternMatched() const { return patternMatched_; }
    int64_t getExecTimeMs() const { return execTimeMs_; }
    
    // 结果判断
    bool isSuccess() const { return exitCode_ == 0 && patternMatched_; }
    std::string toString() const;
};
```

## 4. 处理流程设计

### 4.1 完整处理流程
```
1. 串口读取 -> 2. 命令解析 -> 3. 命令执行 -> 4. 日志收集 -> 5. 模式匹配 -> 6. 结果返回
```

### 4.2 详细步骤

#### 步骤1：串口读取
```cpp
// 读取完整命令帧
std::string readCommandFrame() {
    std::string buffer;
    while (true) {
        std::string chunk = serialPort.read(100); // 100ms超时
        if (chunk.empty()) {
            // 超时处理
            break;
        }
        buffer += chunk;
        
        // 检查是否收到完整帧
        if (isCompleteFrame(buffer)) {
            return extractCommand(buffer);
        }
    }
    return "";
}
```

#### 步骤2：命令解析
```cpp
std::unique_ptr<TestCommand> parseSLTCommand(const std::string& rawCmd) {
    // 1. 提取命令部分（去除开始/结束标记）
    std::string cmdText = extractBetweenMarkers(rawCmd, "$$", "^^");
    
    // 2. 解析测试名称
    size_t spacePos = cmdText.find(' ');
    std::string testName = cmdText.substr(0, spacePos);
    
    // 3. 解析参数
    std::string args = cmdText.substr(spacePos + 1);
    auto params = parseParameters(args);
    
    // 4. 创建TestCommand对象
    auto cmd = std::make_unique<TestCommand>(
        testName,
        params["c"],  // shell命令
        params["p"],  // 匹配模式
        std::stoi(params["t"]),  // 超时时间
        params.find("v") != params.end()  // verbose标志
    );
    
    return cmd;
}
```

#### 步骤3：命令执行
```cpp
ExecutionResult executeShellCommand(const TestCommand& cmd) {
    auto startTime = std::chrono::steady_clock::now();
    
    // 使用popen执行命令
    FILE* pipe = popen(cmd.getShellCmd().c_str(), "r");
    if (!pipe) {
        ExecutionResult result(cmd.getId());
        result.setExitCode(-1);
        result.setError("Failed to execute command");
        return result;
    }
    
    // 读取输出
    std::string output;
    char buffer[128];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }
    
    // 获取退出码
    int exitCode = pclose(pipe);
    
    auto endTime = std::chrono::steady_clock::now();
    auto execTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    ExecutionResult result(cmd.getId());
    result.setExitCode(exitCode);
    result.setOutput(output);
    result.setExecTimeMs(execTime.count());
    
    return result;
}
```

#### 步骤4：模式匹配
```cpp
bool matchPatternInLogs(const std::string& logs, const std::string& pattern) {
    // 简单的字符串查找
    if (logs.find(pattern) != std::string::npos) {
        return true;
    }
    
    // 支持正则表达式
    try {
        std::regex regexPattern(pattern);
        return std::regex_search(logs, regexPattern);
    } catch (const std::regex_error& e) {
        // 正则表达式无效，回退到简单查找
        return logs.find(pattern) != std::string::npos;
    }
}
```

## 5. 配置管理

### 5.1 配置文件格式
```yaml
# slt_config.yaml
serial:
  port: "/dev/ttyUSB0"
  baud_rate: 115200
  data_bits: 8
  stop_bits: 1
  parity: "none"

logging:
  directory: "/var/log/slt/"
  max_size_mb: 100
  rotate_count: 5

commands:
  default_timeout_ms: 5000
  max_command_size: 4096
  markers:
    cmd_start: "$$"
    cmd_end: "^^"
    result_start: "$&"
    result_end: "^^"
    log_start: "$@"
    log_end: "^^"
```

### 5.2 配置类设计
```cpp
class SLTConfig {
private:
    struct SerialConfig {
        std::string port = "/dev/ttyUSB0";
        int baudRate = 115200;
        int dataBits = 8;
        int stopBits = 1;
        std::string parity = "none";
    };
    
    struct LogConfig {
        std::string directory = "/var/log/slt/";
        int maxSizeMB = 100;
        int rotateCount = 5;
    };
    
    struct CommandConfig {
        int defaultTimeoutMs = 5000;
        int maxCommandSize = 4096;
        std::map<std::string, std::string> markers;
    };
    
    SerialConfig serial_;
    LogConfig log_;
    CommandConfig command_;
    
public:
    bool loadFromFile(const std::string& filePath);
    bool saveToFile(const std::string& filePath);
    
    // Getters
    const SerialConfig& getSerialConfig() const { return serial_; }
    const LogConfig& getLogConfig() const { return log_; }
    const CommandConfig& getCommandConfig() const { return command_; }
    
    // 便捷方法
    std::string getCmdStartMarker() const {
        auto it = command_.markers.find("cmd_start");
        return it != command_.markers.end() ? it->second : "$$";
    }
    
    std::string getCmdEndMarker() const {
        auto it = command_.markers.find("cmd_end");
        return it != command_.markers.end() ? it->second : "^^";
    }
};
```

## 6. 错误处理设计

### 6.1 错误码定义
```cpp
enum class SLTErrorCode {
    SUCCESS = 0,
    SERIAL_PORT_ERROR = -1,
    COMMAND_PARSE_ERROR = -2,
    COMMAND_EXECUTION_ERROR = -3,
    TIMEOUT_ERROR = -4,
    LOG_PROCESS_ERROR = -5,
    CONFIG_ERROR = -6,
    UNKNOWN_ERROR = -99
};
```

### 6.2 异常类设计
```cpp
class SLTException : public std::runtime_error {
private:
    SLTErrorCode errorCode_;
    std::string context_;
    
public:
    SLTException(SLTErrorCode code, const std::string& message, 
                 const std::string& context = "")
        : std::runtime_error(message), errorCode_(code), context_(context) {}
    
    SLTErrorCode getErrorCode() const { return errorCode_; }
    const std::string& getContext() const { return context_; }
    
    std::string getFullMessage() const {
        return "SLT Error [" + std::to_string(static_cast<int>(errorCode_)) + 
               "]: " + what() + (context_.empty() ? "" : " (" + context_ + ")");
    }
};
```

## 7. 扩展性设计

### 7.1 插件化架构
```cpp
// 插件接口
class ISLTPlugin {
public:
    virtual ~ISLTPlugin() = default;
    virtual std::string getName() const = 0;
    virtual bool initialize(const SLTConfig& config) = 0;
    virtual void processCommand(TestCommand& cmd, ExecutionResult& result) = 0;
    virtual void cleanup() = 0;
};

// 插件管理器
class PluginManager {
private:
    std::vector<std::shared_ptr<ISLTPlugin>> plugins_;
    
public:
    void registerPlugin(std::shared_ptr<ISLTPlugin> plugin);
    bool initializeAll(const SLTConfig& config);
    void processAll(TestCommand& cmd, ExecutionResult& result);
    void cleanupAll();
};
```

### 7.2 可扩展的日志处理器
```cpp
// 日志处理器基类
class BaseLogProcessor : public ILogProcessor {
protected:
    std::string logDir_;
    
public:
    BaseLogProcessor(const std::string& logDir) : logDir_(logDir) {}
    
    virtual bool saveLogs(const std::string& commandId, const std::string& logs) override {
        std::string filePath = logDir_ + "/" + commandId + ".log";
        return writeFile(filePath, logs);
    }
};

// 正则表达式日志处理器
class RegexLogProcessor : public BaseLogProcessor {
public:
    RegexLogProcessor(const std::string& logDir) : BaseLogProcessor(logDir) {}
    
    virtual bool matchPattern(const std::string& logs, const std::string& pattern) override {
        std::regex regexPattern(pattern);
        return std::regex_search(logs, regexPattern);
    }
};

// 通配符日志处理器
class WildcardLogProcessor : public BaseLogProcessor {
public:
    WildcardLogProcessor(const std::string& logDir) : BaseLogProcessor(logDir) {}
    
    virtual bool matchPattern(const std::string& logs, const std::string& pattern) override {
        // 将通配符转换为正则表达式
        std::string regexPattern = wildcardToRegex(pattern);
        std::regex regex(regexPattern);
        return std::regex_search(logs, regex);
    }
    
private:
    std::string wildcardToRegex(const std::string& wildcard) {
        std::string regex;
        for (char c : wildcard) {
            if (c == '*') regex += ".*";
            else if (c == '?') regex += ".";
            else if (c == '.' || c == '[' || c == ']' || c == '(' || c == ')' || 
                     c == '{' || c == '}' || c == '+' || c == '|' || c == '^' || 
                     c == '$' || c == '\\') {
                regex += '\\';
                regex += c;
            } else {
                regex += c;
            }
        }
        return regex;
    }
};
```

## 8. 性能优化考虑

### 8.1 异步处理
```cpp
class AsyncCommandProcessor {
private:
    std::thread workerThread_;
    std::queue<std::unique_ptr<TestCommand>> commandQueue_;
    std::mutex queueMutex_;
    std::condition_variable queueCond_;
    bool stopFlag_ = false;
    
public:
    AsyncCommandProcessor() {
        workerThread_ = std::thread(&AsyncCommandProcessor::workerLoop, this);
    }
    
    ~AsyncCommandProcessor() {
        stopFlag_ = true;
        queueCond_.notify_all();
        if (workerThread_.joinable()) {
            workerThread_.join();
        }
    }
    
    void submitCommand(std::unique_ptr<TestCommand> cmd) {
        std::lock_guard<std::mutex> lock(queueMutex_);
        commandQueue_.push(std::move(cmd));
        queueCond_.notify_one();
    }
    
private:
    void workerLoop() {
        while (!stopFlag_) {
            std::unique_ptr<TestCommand> cmd;
            {
                std::unique_lock<std::mutex> lock(queueMutex_);
                queueCond_.wait(lock, [this]() {
                    return !commandQueue_.empty() || stopFlag_;
                });
                
                if (stopFlag_) break;
                
                cmd = std::move(commandQueue_.front());
                commandQueue_.pop();
            }
            
            if (cmd) {
                processCommand(*cmd);
            }
        }
    }
    
    void processCommand(const TestCommand& cmd) {
        // 实际处理逻辑
    }
};
```

### 8.2 内存池优化
```cpp
class CommandPool {
private:
    std::vector<std::unique_ptr<TestCommand>> pool_;
    size_t maxSize_;
    
public:
    CommandPool(size_t maxSize = 100) : maxSize_(maxSize) {
        pool_.reserve(maxSize);
    }
    
    std::unique_ptr<TestCommand> acquire() {
        if (!pool_.empty()) {
            auto cmd = std::move(pool_.back());
            pool_.pop_back();
            return cmd;
        }
        return std::make_unique<TestCommand>();
    }
    
    void release(std::unique_ptr<TestCommand> cmd) {
        if (pool_.size() < maxSize_) {
            // 重置命令状态
            // cmd->reset();
            pool_.push_back(std::move(cmd));
        }
    }
};
```

## 9. 测试策略

### 9.1 单元测试
- 命令解析器测试
- 模式匹配测试
- 串口模拟测试
- 配置加载测试

### 9.2 集成测试
- 完整流程测试
- 错误恢复测试
- 性能压力测试

### 9.3 模拟测试框架
```cpp
class MockSerialPort : public ISerialPort {
private:
    std::queue<std::string> mockData_;
    
public:
    void pushMockData(const std::string& data) {
        mockData_.push(data);
    }
    
    std::string read(int timeoutMs) override {
        if (mockData_.empty()) {
            return "";
        }
        std::string data = mockData_.front();
        mockData_.pop();
        return data;
    }
    
    // 其他接口实现...
};
```

## 10. 部署与运维

### 10.1 系统服务配置
```ini
# /etc/systemd/system/slt-daemon.service
[Unit]
Description=SLT Test Daemon
After=network.target

[Service]
Type=simple
User=sltuser
Group=sltgroup
WorkingDirectory=/opt/slt/
ExecStart=/usr/bin/slt-daemon --config /etc/slt/config.yaml
Restart=on-failure
RestartSec=5s

[Install]
WantedBy=multi-user.target
```

### 10.2 日志轮转配置
```ini
# /etc/logrotate.d/slt
/var/log/slt/*.log {
    daily
    missingok
    rotate 30
    compress
    delaycompress
    notifempty
    create 0640 sltuser sltgroup
    sharedscripts
    postrotate
        systemctl reload slt-daemon >/dev/null 2>&1 || true
    endscript
}
```

---

## 总结

本架构设计实现了以下目标：

1. **高内聚低耦合**：每个组件职责明确，通过接口进行通信
2. **易于扩展**：支持插件化架构，可添加新的命令处理器、日志处理器等
3. **容错性强**：完善的错误处理机制，支持异常恢复
4. **性能优化**：支持异步处理，内存池优化
5. **易于维护**：清晰的代码结构，完善的文档和测试

这个设计可以满足SLT下位机的需求，并为未来的功能扩展提供了良好的基础。