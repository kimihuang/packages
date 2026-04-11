// ExecutionResult类实现
// 文件名: execution_result.cpp

#include "slt_interfaces.h"

namespace slt {

ExecutionResult::ExecutionResult(const std::string& commandId)
    : commandId_(commandId)
    , exitCode_(0)
    , executionTimeMs_(0)
    , patternMatchResult_(false) {
}

} // namespace slt