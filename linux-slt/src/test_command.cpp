// TestCommand类实现
// 文件名: test_command.cpp

#include "slt_interfaces.h"
#include "slt_utils.h"
#include <sstream>
#include <chrono>

namespace slt {

TestCommand::TestCommand(const std::string& id,
                         const std::string& name,
                         const std::string& shellCommand,
                         const std::string& pattern,
                         int timeoutMs,
                         bool verbose)
    : id_(id.empty() ? utils::generateId() : id)
    , name_(name)
    , shellCommand_(shellCommand)
    , pattern_(pattern)
    , timeoutMs_(timeoutMs)
    , verbose_(verbose) {
}

void TestCommand::setParameter(const std::string& key, const std::string& value) {
    parameters_[key] = value;
}

std::string TestCommand::getParameter(const std::string& key, const std::string& defaultValue) const {
    auto it = parameters_.find(key);
    return it != parameters_.end() ? it->second : defaultValue;
}

bool TestCommand::hasParameter(const std::string& key) const {
    return parameters_.find(key) != parameters_.end();
}

} // namespace slt