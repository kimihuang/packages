// 组件工厂实现
// 文件名: component_factory.cpp

#include "slt_interfaces.h"
#include "linux_serial_port.h"
#include "tcp_server_handler.h"
#include "slt_command_parser.h"
#include "system_command_executor.h"
#include "pattern_log_processor.h"
#include "slt_controller.h"
#include <memory>
#include <iostream>

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
    if (config.isTcpMode()) {
        std::cout << "[INFO] Using TCP mode: " << config.getTcpAddr()
                  << ":" << config.getTcpPort() << std::endl;
        return std::make_unique<TcpServerHandler>();
    }
    return std::make_unique<LinuxSerialPort>();
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