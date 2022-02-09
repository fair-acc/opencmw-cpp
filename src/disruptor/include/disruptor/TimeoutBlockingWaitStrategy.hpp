#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <ostream>
#include <thread>

#include "Exceptions.hpp"
#include "ISequenceBarrier.hpp"
#include "IWaitStrategy.hpp"
#include "Sequence.hpp"

namespace opencmw::disruptor {

class TimeoutBlockingWaitStrategy : public IWaitStrategy {
    using Clock = std::conditional_t<std::chrono::high_resolution_clock::is_steady, std::chrono::high_resolution_clock, std::chrono::steady_clock>;
    Clock::duration             m_timeout;
    std::recursive_mutex        m_gate;
    std::condition_variable_any m_conditionVariable;

public:
    explicit TimeoutBlockingWaitStrategy(Clock::duration timeout)
        : m_timeout(timeout) {}

    /**
     * \see IWaitStrategy.waitFor
     */
    std::int64_t waitFor(std::int64_t sequence, Sequence &cursor, ISequence &dependentSequence, ISequenceBarrier &barrier) override {
        auto timeSpan = std::chrono::microseconds(std::chrono::duration_cast<std::chrono::microseconds>(m_timeout).count());

        if (cursor.value() < sequence) {
            std::unique_lock uniqueLock(m_gate);

            while (cursor.value() < sequence) {
                barrier.checkAlert();

                if (m_conditionVariable.wait_for(uniqueLock, timeSpan) == std::cv_status::timeout) {
                    DISRUPTOR_THROW_TIMEOUT_EXCEPTION();
                }
            }
        }

        std::int64_t availableSequence;
        while ((availableSequence = dependentSequence.value()) < sequence) {
            barrier.checkAlert();
        }

        return availableSequence;
    }
    /**
     * \see IWaitStrategy.signalAllWhenBlocking
     */
    void signalAllWhenBlocking() override {
        std::unique_lock uniqueLock(m_gate);

        m_conditionVariable.notify_all();
    }

    void writeDescriptionTo(std::ostream &stream) const override {
        stream << "TimeoutBlockingWaitStrategy";
    }
};

} // namespace opencmw::disruptor
