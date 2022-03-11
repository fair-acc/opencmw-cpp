#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <ranges>
#include <vector>

#include "IEventProcessor.hpp"
#include "ISequence.hpp"

namespace opencmw::disruptor {

class IEventProcessor;
class ISequence;

namespace util {

/**
 * Get the minimum sequence from an array of Sequences.
 *
 * \param sequences sequences to compare.
 * \param minimum an initial default minimum.  If the array is empty this value will returned.
 * \returns the minimum sequence found or lon.MaxValue if the array is empty.
 */
inline std::int64_t getMinimumSequence(const std::vector<std::shared_ptr<ISequence>> &sequences, std::int64_t minimum = std::numeric_limits<std::int64_t>::max()) {
    if (sequences.empty()) {
        return minimum;
    } else {
        return std::min(minimum, std::ranges::min(sequences, std::less{}, [](const auto &sequence) { return sequence->value(); })->value());
    }
}

/**
 * Get an array of Sequences for the passed IEventProcessors
 *
 * \param processors processors for which to get the sequences
 * \returns the array of\returns <see cref="Sequence"/>\returns s
 */
inline std::vector<std::shared_ptr<ISequence>> getSequencesFor(const std::vector<std::shared_ptr<IEventProcessor>> &processors) {
    // Ah C++20 ranges, no conversions to vector yet
    std::vector<std::shared_ptr<ISequence>> sequences(processors.size());
    std::ranges::transform(processors, sequences.begin(), [](const auto &processor) { return processor->sequence(); });
    return sequences;
}

} // namespace util
} // namespace opencmw::disruptor
