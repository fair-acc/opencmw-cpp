#pragma once

#include <mutex>

#include "ISequenceBarrier.hpp"
#include "IWaitStrategy.hpp"
#include "Sequence.hpp"

namespace opencmw::disruptor {

/**
 * Busy Spin strategy that uses a busy spin loop for IEventProcessor's waiting on a barrier.
 * This strategy will use CPU resource to avoid syscalls which can introduce latency jitter.  It is best used when threads can be bound to specific CPU cores.
 */
class BusySpinWaitStrategy : public IWaitStrategy {
public:
    /**
     * \see IWaitStrategy::waitFor
     */
    std::int64_t waitFor(std::int64_t sequence,
            Sequence & /*cursor*/,
            ISequence        &dependentSequence,
            ISequenceBarrier &barrier) override {
        std::int64_t availableSequence;

        while ((availableSequence = dependentSequence.value()) < sequence) {
            barrier.checkAlert();
        }

        return availableSequence;
    }

    /**
     * \see IWaitStrategy::signalAllWhenBlocking
     */
    void signalAllWhenBlocking() override {}

    void writeDescriptionTo(std::ostream &stream) const override {
        stream << "BusySpinWaitStrategy";
    }
};

} // namespace opencmw::disruptor
