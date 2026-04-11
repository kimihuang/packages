// SLT控制器头文件
// 文件名: slt_controller.h

#ifndef SLT_CONTROLLER_H
#define SLT_CONTROLLER_H

#include "slt_interfaces.h"
#include <memory>
#include <atomic>
#include <string>

namespace slt {

class SLTController : public std::enable_shared_from_this<SLTController> {
public:
    SLTController(std::unique_ptr<SerialPortHandler> serialHandler,
                  std::unique_ptr<CommandParser> commandParser,
                  std::unique_ptr<CommandExecutor> commandExecutor,
                  std::unique_ptr<LogProcessor> logProcessor);
    
    ~SLTController();
    
    // 控制器操作
    bool start();
    void stop();
    ExecutionResult processNextCommand();
    
    // 状态查询
    bool isRunning() const { return isRunning_; }
    int getCommandsProcessed() const { return commandsProcessed_; }
    std::string getLastError() const { return lastError_; }
    
    // 组件访问
    SerialPortHandler* getSerialHandler() { return serialHandler_.get(); }
    CommandParser* getCommandParser() { return commandParser_.get(); }
    CommandExecutor* getCommandExecutor() { return commandExecutor_.get(); }
    LogProcessor* getLogProcessor() { return logProcessor_.get(); }
    
private:
    bool initializeComponents();
    void cleanup();
    bool sendResult(const ExecutionResult& result);
    
private:
    std::unique_ptr<SerialPortHandler> serialHandler_;
    std::unique_ptr<CommandParser> commandParser_;
    std::unique_ptr<CommandExecutor> commandExecutor_;
    std::unique_ptr<LogProcessor> logProcessor_;
    
    std::atomic<bool> isRunning_;
    std::atomic<int> commandsProcessed_;
    std::string lastError_;
};

} // namespace slt

#endif // SLT_CONTROLLER_H