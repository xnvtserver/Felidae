#pragma once

#include <algorithm>
#include <cstddef>

namespace Felidae {

// Runtime counters shared by direct integer parse entry points.  They are
// intentionally independent of any token-classification implementation.
struct ParserMetrics {
    std::size_t tokensLexed = 0;
    std::size_t iterations = 0;
    std::size_t peakRecursionDepth = 0;
    std::size_t backtrackingAttempts = 0;
    std::size_t operatorCandidateLookups = 0;
    std::size_t operatorCandidatesScored = 0;

    ParserMetrics& operator+=(const ParserMetrics& other) {
        tokensLexed += other.tokensLexed;
        iterations += other.iterations;
        peakRecursionDepth = std::max(peakRecursionDepth, other.peakRecursionDepth);
        backtrackingAttempts += other.backtrackingAttempts;
        operatorCandidateLookups += other.operatorCandidateLookups;
        operatorCandidatesScored += other.operatorCandidatesScored;
        return *this;
    }
};

} // namespace Felidae
