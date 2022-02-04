#pragma once

#include <condition_variable>
#include <mutex>
#include <thread>

#include "ClockConfig.hpp"
#include "IWaitStrategy.hpp"

namespace opencmw::disruptor {

class TimeoutBlockingWaitStrategy : public IWaitStrategy {
private:
    ClockConfig::Duration       m_timeout;
    std::recursive_mutex        m_gate;
    std::condition_variable_any m_conditionVariable;

public:
    explicit TimeoutBlockingWaitStrategy(ClockConfig::Duration timeout);

    /**
     * \see IWaitStrategy.waitFor
     */
    std::int64_t waitFor(std::int64_t sequence,
            Sequence                 &cursor,
            ISequence                &dependentSequence,
            ISequenceBarrier         &barrier) override;

    /**
     * \see IWaitStrategy.signalAllWhenBlocking
     */
    void signalAllWhenBlocking() override;

    void writeDescriptionTo(std::ostream &stream) const override;
};

} // namespace opencmw::disruptor
