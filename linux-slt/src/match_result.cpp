// MatchResult类实现
// 文件名: match_result.cpp

#include "slt_interfaces.h"

namespace slt {

MatchResult::MatchResult(bool matched, int matchCount,
                         const std::string& matchedText, int position)
    : matched_(matched)
    , matchCount_(matchCount)
    , matchedText_(matchedText)
    , position_(position) {
}

} // namespace slt