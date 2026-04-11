// 组件工厂实现
// 文件名: component_factory.cpp

#include "slt_interfaces.h"
#include "linux_serial_port.h"
#include "slt_command_parser.h"
#include "system_command_executor.h"
#include "pattern_log_processor.h"
#include "slt_controller.h"
#include <memory>

namespace slt {

// ================== 基础组件创建 ==================
std::unique_ptr<SerialPortHandler> ComponentFactory::createSerialPortHandler() {
    return std::make_unique<LinuxSerialPort>();
}

std::unique_ptr<CommandParser> ComponentFactory::createCommandParser() {
    return std::make_unique<SLTCommandParser>();
}

std::unique_ptr<CommandExecutor> ComponentFactory::createCommandExecutor() {
    return std::make_unique<SystemCommandExecutor>();
}

std::unique_ptr<LogProcessor> ComponentFactory::createLogProcessor() {
    return std::make_unique<PatternLogProcessor>();
}

// ================== 带配置的组件创建 ==================
std::unique_ptr<SerialPortHandler> ComponentFactory::createSerialPortHandler(const SLTConfig& config) {
    auto handler = std::make_unique<LinuxSerialPort>();
    // 这里可以设置串口配置
    // handler->setBaudRate(config.getBaudRate());
    return handler;
}

std::unique_ptr<LogProcessor> ComponentFactory::createLogProcessor(const SLTConfig& config) {
    return std::make_unique<PatternLogProcessor>(config.getLogDirectory());
}

// ================== 控制器创建 ==================
std::unique_ptr<SLTController> ComponentFactory::createController(const SLTConfig& config) {
    // 创建所有组件
    auto serialHandler = createSerialPortHandler(config);
    auto commandParser = createCommandParser();
    auto commandExecutor = createCommandExecutor();
    auto logProcessor = createLogProcessor(config);
    
    // 创建控制器
    return std::make_unique<SLTController>(
        std::move(serialHandler),
        std::move(commandParser),
        std::move(commandExecutor),
        std::move(logProcessor)
    );
}

} // namespace slt