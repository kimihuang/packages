// SLTCommandParser类实现
// 文件名: slt_command_parser.cpp

#include "slt_command_parser.h"
#include "slt_utils.h"
#include <sstream>
#include <iostream>

namespace slt {

SLTCommandParser::SLTCommandParser() 
    : commandStartMarker_("$$")
    , commandEndMarker_("^^")
    , resultStartMarker_("$&")
    , resultEndMarker_("^^")
    , logStartMarker_("$@")
    , logEndMarker_("^^") {
}

std::unique_ptr<TestCommand> SLTCommandParser::parseCommand(const std::string& rawData) {
    lastError_.clear();
    
    // 检查开始和结束标记
    size_t startPos = rawData.find(commandStartMarker_);
    size_t endPos = rawData.find(commandEndMarker_, startPos);
    
    if (startPos == std::string::npos || endPos == std::string::npos) {
        lastError_ = "Invalid command format: missing start or end markers";
        return nullptr;
    }
    
    // 提取命令内容
    std::string cmdContent = rawData.substr(
        startPos + commandStartMarker_.length(),
        endPos - startPos - commandStartMarker_.length()
    );
    
    cmdContent = utils::trim(cmdContent);
    
    // 解析命令内容
    return parseCommandContent(cmdContent);
}

bool SLTCommandParser::validateCommand(const TestCommand& command) {
    lastError_.clear();
    
    if (command.getName().empty()) {
        lastError_ = "Test name cannot be empty";
        return false;
    }
    
    if (command.getShellCommand().empty()) {
        lastError_ = "Shell command cannot be empty";
        return false;
    }
    
    if (command.getPattern().empty()) {
        lastError_ = "Pattern cannot be empty";
        return false;
    }
    
    if (command.getTimeout() <= 0) {
        lastError_ = "Timeout must be positive";
        return false;
    }
    
    return true;
}

std::string SLTCommandParser::getLastError() const {
    return lastError_;
}

void SLTCommandParser::setMarkers(const std::string& cmdStart,
                                  const std::string& cmdEnd,
                                  const std::string& resultStart,
                                  const std::string& resultEnd,
                                  const std::string& logStart,
                                  const std::string& logEnd) {
    commandStartMarker_ = cmdStart;
    commandEndMarker_ = cmdEnd;
    resultStartMarker_ = resultStart;
    resultEndMarker_ = resultEnd;
    logStartMarker_ = logStart;
    logEndMarker_ = logEnd;
}

// ================== 私有方法 ==================
std::unique_ptr<TestCommand> SLTCommandParser::parseCommandContent(const std::string& content) {
    if (content.empty()) {
        lastError_ = "Command content is empty";
        return nullptr;
    }
    
    // 分离测试名称和参数
    size_t spacePos = content.find(' ');
    if (spacePos == std::string::npos) {
        lastError_ = "No parameters found in command";
        return nullptr;
    }
    
    std::string testName = content.substr(0, spacePos);
    std::string argsStr = content.substr(spacePos + 1);
    
    // 解析参数
    auto params = utils::parseCommandLine(argsStr);
    
    // 检查必需参数
    if (params.find("c") == params.end()) {
        lastError_ = "Missing required parameter: -c (shell command)";
        return nullptr;
    }
    
    if (params.find("p") == params.end()) {
        lastError_ = "Missing required parameter: -p (pattern)";
        return nullptr;
    }
    
    // 创建TestCommand对象
    bool verbose = params.find("v") != params.end();
    int timeout = 5000; // 默认超时
    
    if (params.find("t") != params.end()) {
        try {
            timeout = std::stoi(params["t"]);
        } catch (const std::exception& e) {
            lastError_ = "Invalid timeout value: " + std::string(e.what());
            return nullptr;
        }
    }
    
    auto cmd = std::make_unique<TestCommand>(
        "",  // ID由TestCommand自动生成
        testName,
        params["c"],
        params["p"],
        timeout,
        verbose
    );
    
    // 设置其他参数
    for (const auto& param : params) {
        if (param.first != "c" && param.first != "p" && 
            param.first != "t" && param.first != "v") {
            cmd->setParameter(param.first, param.second);
        }
    }
    
    return cmd;
}

} // namespace slt