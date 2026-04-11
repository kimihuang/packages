// 模式日志处理器实现
// 文件名: pattern_log_processor.cpp

#include "pattern_log_processor.h"
#include "slt_utils.h"
#include <fstream>
#include <sstream>
#include <algorithm>

namespace slt {

PatternLogProcessor::PatternLogProcessor()
    : logDirectory_("/var/log/slt/")
    , maxLogSizeMB_(100) {
}

PatternLogProcessor::PatternLogProcessor(const std::string& logDirectory)
    : logDirectory_(logDirectory)
    , maxLogSizeMB_(100) {
    
    if (!logDirectory_.empty() && logDirectory_.back() != '/') {
        logDirectory_ += '/';
    }
}

std::string PatternLogProcessor::collectLogs(const std::string& commandId, int timeoutMs) {
    lastError_.clear();
    
    if (logDirectory_.empty()) {
        lastError_ = "Log directory not set";
        return "";
    }
    
    // 构建日志文件路径
    std::string logFile = logDirectory_ + commandId + ".log";
    
    if (!utils::fileExists(logFile)) {
        lastError_ = "Log file not found: " + logFile;
        return "";
    }
    
    // 读取日志文件
    std::string logs = readLogFile(commandId);
    if (logs.empty()) {
        lastError_ = "Failed to read log file or file is empty";
    }
    
    return logs;
}

MatchResult PatternLogProcessor::matchPattern(const std::string& logText, const std::string& pattern) {
    lastError_.clear();
    
    if (logText.empty() || pattern.empty()) {
        lastError_ = "Log text or pattern is empty";
        return MatchResult(false);
    }
    
    return patternSearch(logText, pattern);
}

bool PatternLogProcessor::saveLogs(const std::string& commandId, const std::string& logData) {
    lastError_.clear();
    
    if (logDirectory_.empty()) {
        lastError_ = "Log directory not set";
        return false;
    }
    
    if (commandId.empty()) {
        lastError_ = "Command ID is empty";
        return false;
    }
    
    // 确保日志目录存在
    if (!utils::createDirectory(logDirectory_)) {
        lastError_ = "Failed to create log directory: " + logDirectory_;
        return false;
    }
    
    // 写入日志文件
    return writeLogFile(commandId, logData);
}

std::string PatternLogProcessor::getLastError() const {
    return lastError_;
}

void PatternLogProcessor::setLogDirectory(const std::string& dir) {
    logDirectory_ = dir;
    if (!logDirectory_.empty() && logDirectory_.back() != '/') {
        logDirectory_ += '/';
    }
}

std::string PatternLogProcessor::getLogDirectory() const {
    return logDirectory_;
}

void PatternLogProcessor::setMaxLogSize(int maxSizeMB) {
    maxLogSizeMB_ = maxSizeMB;
}

int PatternLogProcessor::getMaxLogSize() const {
    return maxLogSizeMB_;
}

// ================== 私有方法 ==================
std::string PatternLogProcessor::readLogFile(const std::string& commandId) {
    std::string logFile = logDirectory_ + commandId + ".log";
    return utils::readFile(logFile);
}

bool PatternLogProcessor::writeLogFile(const std::string& commandId, const std::string& data) {
    std::string logFile = logDirectory_ + commandId + ".log";
    return utils::writeFile(logFile, data);
}

MatchResult PatternLogProcessor::patternSearch(const std::string& text, const std::string& pattern) {
    try {
        // 尝试使用正则表达式匹配
        std::regex regexPattern(pattern);
        std::smatch matches;
        
        if (std::regex_search(text, matches, regexPattern)) {
            // 找到匹配
            std::string matchedText = matches[0].str();
            int position = static_cast<int>(matches.position(0));
            int matchCount = 1;
            
            // 计算总匹配数
            auto begin = std::sregex_iterator(text.begin(), text.end(), regexPattern);
            auto end = std::sregex_iterator();
            matchCount = static_cast<int>(std::distance(begin, end));
            
            return MatchResult(true, matchCount, matchedText, position);
        } else {
            return MatchResult(false);
        }
    } catch (const std::regex_error& e) {
        // 正则表达式无效，回退到简单字符串查找
        size_t pos = text.find(pattern);
        if (pos != std::string::npos) {
            // 计算出现次数
            int count = 0;
            size_t start = 0;
            while ((start = text.find(pattern, start)) != std::string::npos) {
                ++count;
                start += pattern.length();
            }
            
            return MatchResult(true, count, pattern, static_cast<int>(pos));
        } else {
            return MatchResult(false);
        }
    }
}

} // namespace slt