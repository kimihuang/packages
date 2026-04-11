// 模式日志处理器头文件
// 文件名: pattern_log_processor.h

#ifndef PATTERN_LOG_PROCESSOR_H
#define PATTERN_LOG_PROCESSOR_H

#include "slt_interfaces.h"
#include <string>
#include <regex>

namespace slt {

class PatternLogProcessor : public LogProcessor {
public:
    PatternLogProcessor();
    explicit PatternLogProcessor(const std::string& logDirectory);
    
    // LogProcessor接口实现
    std::string collectLogs(const std::string& commandId, int timeoutMs) override;
    MatchResult matchPattern(const std::string& logText, const std::string& pattern) override;
    bool saveLogs(const std::string& commandId, const std::string& logData) override;
    
    std::string getLastError() const override;
    
    // 配置选项
    void setLogDirectory(const std::string& dir);
    std::string getLogDirectory() const;
    
    void setMaxLogSize(int maxSizeMB);
    int getMaxLogSize() const;
    
private:
    std::string readLogFile(const std::string& commandId);
    bool writeLogFile(const std::string& commandId, const std::string& data);
    MatchResult patternSearch(const std::string& text, const std::string& pattern);
    
private:
    std::string logDirectory_;
    int maxLogSizeMB_;
    std::string lastError_;
};

} // namespace slt

#endif // PATTERN_LOG_PROCESSOR_H