#pragma once

#include <condition_variable>
#include <mutex>
#include <ostream>
#include <thread>

#include "ISequenceBarrier.hpp"
#include "IWaitStrategy.hpp"
#include "Sequence.hpp"

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
            ISequenceBarrier         &barrier) override {
        if (cursor.value() < sequence) {
            std::unique_lock uniqueLock(m_gate);

            while (cursor.value() < sequence) {
                barrier.checkAlert();

                m_conditionVariable.wait(uniqueLock);
            }
        }

        std::int64_t availableSequence;
        while ((availableSequence = dependentSequence.value()) < sequence) {
            barrier.checkAlert();
        }

        return availableSequence;
    }

    /**
     * \see IWaitStrategy::signalAllWhenBlocking
     */
    void signalAllWhenBlocking() override {
        std::unique_lock uniqueLock(m_gate);

        m_conditionVariable.notify_all();
    }

    void writeDescriptionTo(std::ostream &stream) const override {
        stream << "BlockingWaitStrategy";
    }
};

} // namespace opencmw::disruptor
