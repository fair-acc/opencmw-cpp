#include <algorithm>
#include <ranges>

#include "stdafx.hpp"
#include "Util.hpp"

#include "IEventProcessor.hpp"
#include "ISequence.hpp"

namespace opencmw::disruptor::Util {

std::int32_t ceilingNextPowerOfTwo(std::int32_t x) {
    std::int32_t result = 2;

    while (result < x) {
        result <<= 1;
    }

    return result;
}

bool isPowerOf2(std::int32_t x) {
    return x > 0 && (x & (x - 1)) == 0;
}

std::int32_t log2(std::int32_t i) {
    std::int32_t r = 0;
    while ((i >>= 1) != 0) {
        ++r;
    }
    return r;
}

std::int64_t getMinimumSequence(const std::vector<std::shared_ptr<ISequence>> &sequences, std::int64_t minimum) {
    if (sequences.empty()) {
        return minimum;
    } else {
        return std::min(minimum,
                std::ranges::min(sequences, std::less{}, [](auto &&sequence) { return sequence->value(); })->value());
    }
}

std::vector<std::shared_ptr<ISequence>> getSequencesFor(const std::vector<std::shared_ptr<IEventProcessor>> &processors) {
    // Ah C++20 ranges, no conversions to vector yet
    std::vector<std::shared_ptr<ISequence>> sequences(processors.size());
    std::ranges::transform(processors, sequences.begin(), [](auto &&processor) { return processor->sequence(); });
    return sequences;
}

} // namespace opencmw::disruptor::Util
