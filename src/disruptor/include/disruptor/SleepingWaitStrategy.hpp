#pragma once

#include <cstdint>

#include "IWaitStrategy.hpp"

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
    explicit SleepingWaitStrategy(std::int32_t retries = m_defaultRetries);

    /**
     * \see IWaitStrategy::waitFor()
     */
    std::int64_t waitFor(std::int64_t sequence,
            Sequence                 &cursor,
            ISequence                &dependentSequence,
            ISequenceBarrier         &barrier) override;

    /**
     * \see IWaitStrategy::signalAllWhenBlocking()
     */
    void signalAllWhenBlocking() override;

    void writeDescriptionTo(std::ostream &stream) const override;

private:
    static std::int32_t applyWaitMethod(ISequenceBarrier &barrier, std::int32_t counter);
};

} // namespace opencmw::disruptor
