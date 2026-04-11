// SLT系统测试程序
// 文件名: test_main.cpp

#include "slt_interfaces.h"
#include "test_mock_serial_port.h"
#include "slt_command_parser.h"
#include "system_command_executor.h"
#include "pattern_log_processor.h"
#include "slt_controller.h"
#include "slt_utils.h"
#include <iostream>
#include <cassert>
#include <memory>
#include <thread>
#include <chrono>

using namespace slt;
using namespace slt::test;

// 测试用例定义
void testCommandParser() {
    std::cout << "\n=== 测试命令解析器 ===" << std::endl;
    
    auto parser = std::make_unique<SLTCommandParser>();
    
    // 测试用例1: 正常命令
    std::string testCmd1 = "$$ACPU_TEST_0001 -v -c \"echo 'Hello, SLT!'\" -p \"Hello\" -t 1000^^";
    auto cmd1 = parser->parseCommand(testCmd1);
    
    assert(cmd1 != nullptr);
    assert(cmd1->getName() == "ACPU_TEST_0001");
    assert(cmd1->getShellCommand() == "echo 'Hello, SLT!'");
    assert(cmd1->getPattern() == "Hello");
    assert(cmd1->getTimeout() == 1000);
    assert(cmd1->isVerbose() == true);
    
    std::cout << "✓ 测试用例1通过: 正常命令解析" << std::endl;
    
    // 测试用例2: 不带verbose的命令
    std::string testCmd2 = "$$ACPU_TEST_0002 -c \"ls -la\" -p \"total\" -t 500^^";
    auto cmd2 = parser->parseCommand(testCmd2);
    
    assert(cmd2 != nullptr);
    assert(cmd2->getName() == "ACPU_TEST_0002");
    assert(cmd2->getShellCommand() == "ls -la");
    assert(cmd2->getPattern() == "total");
    assert(cmd2->getTimeout() == 500);
    assert(cmd2->isVerbose() == false);
    
    std::cout << "✓ 测试用例2通过: 不带verbose的命令解析" << std::endl;
    
    // 测试用例3: 无效命令格式
    std::string testCmd3 = "INVALID_COMMAND_FORMAT";
    auto cmd3 = parser->parseCommand(testCmd3);
    
    assert(cmd3 == nullptr);
    assert(!parser->getLastError().empty());
    
    std::cout << "✓ 测试用例3通过: 无效命令格式检测" << std::endl;
    
    // 测试用例4: 短参数组合
    std::string testCmd4 = "$$ACPU_TEST_0003 -vc \"echo test\" -p \"test\" -t 2000^^";
    auto cmd4 = parser->parseCommand(testCmd4);
    
    assert(cmd4 != nullptr);
    assert(cmd4->isVerbose() == true);
    assert(cmd4->getShellCommand() == "echo test");
    
    std::cout << "✓ 测试用例4通过: 短参数组合解析" << std::endl;
    
    std::cout << "=== 命令解析器测试完成 ===" << std::endl;
}

void testCommandExecutor() {
    std::cout << "\n=== 测试命令执行器 ===" << std::endl;
    
    auto executor = std::make_unique<SystemCommandExecutor>();
    
    // 创建测试命令
    TestCommand testCmd("test_id", "TEST_0001", "echo 'Hello, World!'", "Hello", 1000, true);
    
    // 测试用例1: 执行成功命令
    auto result1 = executor->execute(testCmd);
    
    assert(result1.getExitCode() == 0);
    assert(result1.getOutput().find("Hello, World!") != std::string::npos);
    assert(result1.getExecutionTime() > 0);
    
    std::cout << "✓ 测试用例1通过: 成功执行命令" << std::endl;
    
    // 测试用例2: 执行失败命令
    TestCommand testCmd2("test_id2", "TEST_0002", "invalid_command_xyz", "pattern", 1000);
    auto result2 = executor->execute(testCmd2);
    
    assert(result2.getExitCode() != 0);
    assert(!result2.getErrorMessage().empty());
    
    std::cout << "✓ 测试用例2通过: 失败命令执行" << std::endl;
    
    // 测试用例3: 超时测试
    TestCommand testCmd3("test_id3", "TEST_0003", "sleep 3", "pattern", 100); // 100ms超时
    auto result3 = executor->execute(testCmd3);
    
    assert(result3.getExitCode() != 0);
    assert(result3.getExecutionTime() >= 100);
    
    std::cout << "✓ 测试用例3通过: 命令超时检测" << std::endl;
    
    std::cout << "=== 命令执行器测试完成 ===" << std::endl;
}

void testLogProcessor() {
    std::cout << "\n=== 测试日志处理器 ===" << std::endl;
    
    // 使用临时目录
    std::string tempDir = "/tmp/slt_test_" + utils::generateId();
    auto processor = std::make_unique<PatternLogProcessor>(tempDir);
    
    // 测试用例1: 保存和读取日志
    std::string testLog = "This is a test log\nLine 1\nLine 2\nPattern found here\nLine 3";
    std::string commandId = "test_cmd_001";
    
    assert(processor->saveLogs(commandId, testLog));
    
    std::string readLog = processor->collectLogs(commandId, 1000);
    assert(readLog == testLog);
    
    std::cout << "✓ 测试用例1通过: 日志保存和读取" << std::endl;
    
    // 测试用例2: 模式匹配（字符串）
    auto matchResult1 = processor->matchPattern(testLog, "Pattern found");
    assert(matchResult1.isMatched());
    assert(matchResult1.getMatchedText() == "Pattern found");
    
    std::cout << "✓ 测试用例2通过: 字符串模式匹配" << std::endl;
    
    // 测试用例3: 模式匹配（正则表达式）
    auto matchResult2 = processor->matchPattern(testLog, "Line \\d+");
    assert(matchResult2.isMatched());
    assert(matchResult2.getMatchCount() >= 3);
    
    std::cout << "✓ 测试用例3通过: 正则表达式模式匹配" << std::endl;
    
    // 测试用例4: 不匹配的情况
    auto matchResult3 = processor->matchPattern(testLog, "NonExistentPattern");
    assert(!matchResult3.isMatched());
    
    std::cout << "✓ 测试用例4通过: 模式不匹配检测" << std::endl;
    
    // 清理临时目录
    std::string cleanupCmd = "rm -rf " + tempDir;
    if (system(cleanupCmd.c_str()) != 0) {}
    
    std::cout << "=== 日志处理器测试完成 ===" << std::endl;
}

void testIntegration() {
    std::cout << "\n=== 测试集成功能 ===" << std::endl;
    
    // 创建模拟串口
    auto mockSerial = std::make_unique<MockSerialPort>();
    mockSerial->open("/dev/ttyTEST", 115200);
    
    // 创建其他组件
    auto parser = std::make_unique<SLTCommandParser>();
    auto executor = std::make_unique<SystemCommandExecutor>();
    
    // 使用临时目录作为日志目录
    std::string tempDir = "/tmp/slt_integration_test_" + utils::generateId();
    auto logProcessor = std::make_unique<PatternLogProcessor>(tempDir);
    
    // 创建控制器
    auto controller = std::make_unique<SLTController>(
        std::move(mockSerial),
        std::move(parser),
        std::move(executor),
        std::move(logProcessor)
    );
    
    // 启动控制器
    assert(controller->start());
    
    // 测试用例1: 完整的命令处理流程
    std::cout << "开始集成测试..." << std::endl;
    
    // 向模拟串口发送测试命令
    auto mockSerialPtr = dynamic_cast<MockSerialPort*>(controller->getSerialHandler());
    assert(mockSerialPtr != nullptr);
    
    std::string testCommand = "$$INTEGRATION_TEST_001 -v -c \"echo 'Integration Test Passed'\" -p \"Passed\" -t 2000^^";
    mockSerialPtr->pushMockData(testCommand);
    
    // 处理命令
    auto result = controller->processNextCommand();
    
    // 验证结果
    assert(result.getExitCode() == 0);
    assert(result.isPatternMatched());
    assert(result.getExecutionTime() > 0);
    
    std::cout << "✓ 集成测试通过: 完整命令处理流程" << std::endl;
    std::cout << "  命令ID: " << result.getCommandId() << std::endl;
    std::cout << "  退出码: " << result.getExitCode() << std::endl;
    std::cout << "  模式匹配: " << (result.isPatternMatched() ? "成功" : "失败") << std::endl;
    std::cout << "  执行时间: " << result.getExecutionTime() << "ms" << std::endl;
    
    // 停止控制器
    controller->stop();
    
    // 清理临时目录
    std::string cleanupCmd = "rm -rf " + tempDir;
    if (system(cleanupCmd.c_str()) != 0) {}
    
    std::cout << "=== 集成测试完成 ===" << std::endl;
}

void testSLTCommandFormat() {
    std::cout << "\n=== 测试SLT命令格式兼容性 ===" << std::endl;
    
    auto parser = std::make_unique<SLTCommandParser>();
    
    // 根据slt_cmd_format.txt中的示例进行测试
    std::string exampleCmd = "$$ACPU_TEST_0001 -v -c \"echo hello world\" -p \"hello world\" -t 1000^^";
    
    auto cmd = parser->parseCommand(exampleCmd);
    assert(cmd != nullptr);
    assert(cmd->getName() == "ACPU_TEST_0001");
    assert(cmd->getShellCommand() == "echo hello world");
    assert(cmd->getPattern() == "hello world");
    assert(cmd->getTimeout() == 1000);
    assert(cmd->isVerbose() == true);
    
    std::cout << "✓ SLT命令格式兼容性测试通过" << std::endl;
    std::cout << "  测试名称: " << cmd->getName() << std::endl;
    std::cout << "  Shell命令: " << cmd->getShellCommand() << std::endl;
    std::cout << "  匹配模式: " << cmd->getPattern() << std::endl;
    std::cout << "  超时时间: " << cmd->getTimeout() << "ms" << std::endl;
    std::cout << "  详细模式: " << (cmd->isVerbose() ? "是" : "否") << std::endl;
    
    std::cout << "=== SLT命令格式测试完成 ===" << std::endl;
}

int main(int argc, char* argv[]) {
    std::cout << "=== SLT下位机系统单元测试 ===" << std::endl;
    
    try {
        // 运行所有测试
        testCommandParser();
        testCommandExecutor();
        testLogProcessor();
        testIntegration();
        testSLTCommandFormat();
        
        std::cout << "\n=== 所有测试通过 ===" << std::endl;
        std::cout << "✓ 系统功能验证完成" << std::endl;
        std::cout << "✓ 接口设计合理" << std::endl;
        std::cout << "✓ 组件交互正常" << std::endl;
        std::cout << "✓ SLT命令格式兼容" << std::endl;
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "\n[ERROR] 测试失败: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "\n[ERROR] 未知测试错误" << std::endl;
        return 1;
    }
}