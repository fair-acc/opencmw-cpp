#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <ranges>
#include <vector>

#include "IEventProcessor.hpp"
#include "Sequence.hpp"

namespace opencmw::disruptor {

class IEventProcessor;

namespace util {

/**
 * Get an array of Sequences for the passed IEventProcessors
 *
 * \param processors processors for which to get the sequences
 * \returns the array of\returns <see cref="Sequence"/>\returns s
 */
inline std::vector<std::shared_ptr<Sequence>> getSequencesFor(const std::vector<std::shared_ptr<IEventProcessor>> &processors) {
    // Ah C++20 ranges, no conversions to vector yet
    std::vector<std::shared_ptr<Sequence>> sequences(processors.size());
    std::ranges::transform(processors, sequences.begin(), [](const auto &processor) { return processor->sequence(); });
    return sequences;
}

} // namespace util
} // namespace opencmw::disruptor
