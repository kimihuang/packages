// SLT命令解析器头文件
// 文件名: slt_command_parser.h

#ifndef SLT_COMMAND_PARSER_H
#define SLT_COMMAND_PARSER_H

#include "slt_interfaces.h"
#include <string>
#include <memory>

namespace slt {

class SLTCommandParser : public CommandParser {
public:
    SLTCommandParser();
    
    // CommandParser接口实现
    std::unique_ptr<TestCommand> parseCommand(const std::string& rawData) override;
    bool validateCommand(const TestCommand& command) override;
    std::string getLastError() const override;
    
    // 标记符设置
    void setMarkers(const std::string& cmdStart,
                    const std::string& cmdEnd,
                    const std::string& resultStart,
                    const std::string& resultEnd,
                    const std::string& logStart,
                    const std::string& logEnd);
    
private:
    std::unique_ptr<TestCommand> parseCommandContent(const std::string& content);
    
private:
    std::string commandStartMarker_;
    std::string commandEndMarker_;
    std::string resultStartMarker_;
    std::string resultEndMarker_;
    std::string logStartMarker_;
    std::string logEndMarker_;
    std::string lastError_;
};

} // namespace slt

#endif // SLT_COMMAND_PARSER_H