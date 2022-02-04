#pragma once

#include <condition_variable>
#include <mutex>
#include <thread>

#include "IWaitStrategy.hpp"

namespace opencmw::disruptor {

/**
 * Blocking strategy that uses a lock and condition variable for IEventProcessor's waiting on a barrier.
 * This strategy should be used when performance and low-latency are not as important as CPU resource.
 */
class BlockingWaitStrategy : public IWaitStrategy {
private:
    std::recursive_mutex        m_gate;
    std::condition_variable_any m_conditionVariable;

public:
    /**
     * \see IWaitStrategy::waitFor
     */
    std::int64_t waitFor(std::int64_t sequence,
            Sequence                 &cursor,
            ISequence                &dependentSequence,
            ISequenceBarrier         &barrier) override;

    /**
     * \see IWaitStrategy::signalAllWhenBlocking
     */
    void signalAllWhenBlocking() override;

    void writeDescriptionTo(std::ostream &stream) const override;
};

} // namespace opencmw::disruptor
