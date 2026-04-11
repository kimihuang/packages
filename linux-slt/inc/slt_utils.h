// SLT工具函数头文件
// 文件名: slt_utils.h

#ifndef SLT_UTILS_H
#define SLT_UTILS_H

#include <string>
#include <vector>
#include <map>

namespace slt {
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
bool createDirectory(const std::string& path);

// 时间工具
std::string getCurrentTimeString();
int64_t getCurrentTimeMs();

// 随机ID生成
std::string generateId();

// 命令行解析
std::map<std::string, std::string> parseCommandLine(const std::string& cmdLine);

// 正则表达式匹配
bool regexMatch(const std::string& text, const std::string& pattern);

// 系统命令执行
std::pair<int, std::string> executeShellCommand(const std::string& cmd, int timeoutMs = 5000);

} // namespace utils
} // namespace slt

#endif // SLT_UTILS_H