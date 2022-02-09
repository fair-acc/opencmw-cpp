#pragma once

#include <cstdint>
#include <ostream>
#include <thread>

#include "ISequenceBarrier.hpp"
#include "IWaitStrategy.hpp"
#include "Sequence.hpp"

namespace opencmw::disruptor {

/**
 * Sleeping strategy that initially spins, then uses a std::this_thread::yield(), and eventually sleep. This strategy is a good compromise between performance and CPU resource.
 * Latency spikes can occur after quiet periods.
 */
class SleepingWaitStrategy : public IWaitStrategy {
private:
    static const std::int32_t m_defaultRetries = 200;
    std::int32_t              m_retries        = 0;

public:
    explicit SleepingWaitStrategy(std::int32_t retries = m_defaultRetries)
        : m_retries(retries) {
    }

    /**
     * \see IWaitStrategy::waitFor()
     */
    std::int64_t waitFor(std::int64_t sequence,
            Sequence & /*cursor*/,
            ISequence        &dependentSequence,
            ISequenceBarrier &barrier) override {
        std::int64_t availableSequence;
        auto         counter = m_retries;

        while ((availableSequence = dependentSequence.value()) < sequence) {
            counter = applyWaitMethod(barrier, counter);
        }

        return availableSequence;
    }

    /**
     * \see IWaitStrategy::signalAllWhenBlocking()
     */
    void signalAllWhenBlocking() override {}
    void writeDescriptionTo(std::ostream &stream) const override { stream << "SleepingWaitStrategy"; }

private:
    static std::int32_t applyWaitMethod(ISequenceBarrier &barrier, std::int32_t counter) {
        barrier.checkAlert();

        if (counter > 100) {
            --counter;
        } else if (counter > 0) {
            --counter;
            std::this_thread::yield();
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(0));
        }

        return counter;
    }
};

} // namespace opencmw::disruptor
