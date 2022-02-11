#ifndef WAIT_STRATEGY_CPP
#define WAIT_STRATEGY_CPP

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

#include "ISequence.hpp"
#include "ISequenceBarrier.hpp"
#include "Sequence.hpp"
#include "SpinWait.hpp"

namespace opencmw::disruptor {

/**
 * Wait for the given sequence to be available.  It is possible for this method to return a value less than the sequence number supplied depending on the implementation of the WaitStrategy.
 * A common use for this is to signal a timeout.Any EventProcessor that is using a WaitStrategy to get notifications about message becoming available should remember to handle this case.
 * The BatchEventProcessor<T> explicitly handles this case and will signal a timeout if required.
 *
 * \param sequence sequence to be waited on.
 * \param cursor Ring buffer cursor on which to wait.
 * \param dependentSequence on which to wait.
 * \param barrier barrier the IEventProcessor is waiting on.
 * \returns the sequence that is available which may be greater than the requested sequence.
 */
template<typename T>
constexpr bool isWaitStrategy = requires(T /*const*/ t, const std::int64_t sequence, const Sequence &cursor, ISequence &dependentSequence, ISequenceBarrier &barrier) {
    { t.waitFor(sequence, cursor, dependentSequence, barrier) } -> std::same_as<std::int64_t>;
};
static_assert(!isWaitStrategy<int>);

template<typename T>
concept WaitStrategyConcept = isWaitStrategy<T>;

struct WaitStrategy {
    virtual ~WaitStrategy() = default;

    /**
     * Wait for the given sequence to be available.  It is possible for this method to return a value less than the sequence number supplied depending on the implementation of the WaitStrategy.
     * A common use for this is to signal a timeout.Any EventProcessor that is using a WaitStrategy to get notifications about message becoming available should remember to handle this case.
     * The BatchEventProcessor<T> explicitly handles this case and will signal a timeout if required.
     *
     * \param sequence sequence to be waited on.
     * \param cursor Ring buffer cursor on which to wait.
     * \param dependentSequence on which to wait.
     * \param barrier barrier the IEventProcessor is waiting on.
     * \returns the sequence that is available which may be greater than the requested sequence.
     */
    virtual std::int64_t waitFor(const std::int64_t sequence, const Sequence &cursor, ISequence &dependentSequence, ISequenceBarrier &barrier) = 0;

    /**
     * Signal those IEventProcessor waiting that the cursor has advanced.
     */
    virtual void signalAllWhenBlocking() = 0;
};

/**
 * Blocking strategy that uses a lock and condition variable for IEventProcessor's waiting on a barrier.
 * This strategy should be used when performance and low-latency are not as important as CPU resource.
 */
class BlockingWaitStrategy : public WaitStrategy {
    std::recursive_mutex        m_gate;
    std::condition_variable_any m_conditionVariable;

public:
    std::int64_t waitFor(const std::int64_t sequence, const Sequence &cursor, ISequence &dependentSequence, ISequenceBarrier &barrier) override {
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

    void signalAllWhenBlocking() override {
        std::unique_lock uniqueLock(m_gate);
        m_conditionVariable.notify_all();
    }
};
static_assert(WaitStrategyConcept<BlockingWaitStrategy>);

/**
 * Busy Spin strategy that uses a busy spin loop for IEventProcessor's waiting on a barrier.
 * This strategy will use CPU resource to avoid syscalls which can introduce latency jitter.  It is best used when threads can be bound to specific CPU cores.
 */
struct BusySpinWaitStrategy : public WaitStrategy {
    std::int64_t waitFor(const std::int64_t sequence, const Sequence & /*cursor*/, ISequence &dependentSequence, ISequenceBarrier &barrier) override {
        std::int64_t availableSequence;
        while ((availableSequence = dependentSequence.value()) < sequence) {
            barrier.checkAlert();
        }
        return availableSequence;
    }

    void signalAllWhenBlocking() override { /* does not block by design */
    }
};
static_assert(WaitStrategyConcept<BusySpinWaitStrategy>);

/**
 * Sleeping strategy that initially spins, then uses a std::this_thread::yield(), and eventually sleep. This strategy is a good compromise between performance and CPU resource.
 * Latency spikes can occur after quiet periods.
 */
class SleepingWaitStrategy : public WaitStrategy {
    static const std::int32_t m_defaultRetries = 200;
    std::int32_t              m_retries        = 0;

public:
    explicit SleepingWaitStrategy(std::int32_t retries = m_defaultRetries)
        : m_retries(retries) {
    }

    std::int64_t waitFor(const std::int64_t sequence, const Sequence & /*cursor*/, ISequence &dependentSequence, ISequenceBarrier &barrier) override {
        auto       counter    = m_retries;
        const auto waitMethod = [&]() {
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
        };

        std::int64_t availableSequence;
        while ((availableSequence = dependentSequence.value()) < sequence) {
            counter = waitMethod();
        }

        return availableSequence;
    }
    void signalAllWhenBlocking() override { /* does not block by design */
    }
};
static_assert(WaitStrategyConcept<SleepingWaitStrategy>);

/**
 * Spin strategy that uses a SpinWait for IEventProcessors waiting on a barrier.
 * This strategy is a good compromise between performance and CPU resource. Latency spikes can occur after quiet periods.
 */
struct SpinWaitWaitStrategy : public WaitStrategy {
    std::int64_t waitFor(const std::int64_t sequence, const Sequence & /*cursor*/, ISequence &dependentSequence, ISequenceBarrier &barrier) override {
        std::int64_t availableSequence;

        SpinWait     spinWait;
        while ((availableSequence = dependentSequence.value()) < sequence) {
            barrier.checkAlert();
            spinWait.spinOnce();
        }

        return availableSequence;
    }

    void signalAllWhenBlocking() override { /* does not block by design */
    }
};
static_assert(WaitStrategyConcept<SpinWaitWaitStrategy>);

class TimeoutBlockingWaitStrategy : public WaitStrategy {
    using Clock = std::conditional_t<std::chrono::high_resolution_clock::is_steady, std::chrono::high_resolution_clock, std::chrono::steady_clock>;
    Clock::duration             m_timeout;
    std::recursive_mutex        m_gate;
    std::condition_variable_any m_conditionVariable;

public:
    explicit TimeoutBlockingWaitStrategy(Clock::duration timeout)
        : m_timeout(timeout) {}

    std::int64_t waitFor(const std::int64_t sequence, const Sequence &cursor, ISequence &dependentSequence, ISequenceBarrier &barrier) override {
        auto timeSpan = std::chrono::microseconds(std::chrono::duration_cast<std::chrono::microseconds>(m_timeout).count());

        if (cursor.value() < sequence) {
            std::unique_lock uniqueLock(m_gate);

            while (cursor.value() < sequence) {
                barrier.checkAlert();

                if (m_conditionVariable.wait_for(uniqueLock, timeSpan) == std::cv_status::timeout) {
                    throw timeout_exception();
                }
            }
        }

        std::int64_t availableSequence;
        while ((availableSequence = dependentSequence.value()) < sequence) {
            barrier.checkAlert();
        }

        return availableSequence;
    }

    void signalAllWhenBlocking() override {
        std::unique_lock uniqueLock(m_gate);
        m_conditionVariable.notify_all();
    }
};
static_assert(WaitStrategyConcept<TimeoutBlockingWaitStrategy>);

/**
 * Yielding strategy that uses a Thread.Yield() for IEventProcessors waiting on a barrier after an initially spinning.
 * This strategy is a good compromise between performance and CPU resource without incurring significant latency spikes.
 */
class YieldingWaitStrategy : public WaitStrategy {
    const std::size_t m_spinTries = 100;

public:
    std::int64_t waitFor(const std::int64_t sequence, const Sequence & /*cursor*/, ISequence &dependentSequence, ISequenceBarrier &barrier) override {
        auto       counter    = m_spinTries;
        const auto waitMethod = [&]() {
            barrier.checkAlert();

            if (counter == 0) {
                std::this_thread::yield();
            } else {
                --counter;
            }
            return counter;
        };

        std::int64_t availableSequence;
        while ((availableSequence = dependentSequence.value()) < sequence) {
            counter = waitMethod();
        }

        return availableSequence;
    }

    void signalAllWhenBlocking() override { /* does not block by design */
    }
};
static_assert(WaitStrategyConcept<YieldingWaitStrategy>);

struct NoWaitStrategy {
    std::int64_t waitFor(const std::int64_t sequence, const Sequence & /*cursor*/, const ISequence &/*dependentSequence*/, const ISequenceBarrier &/*barrier*/) const {
        // wait for nothing
        return sequence;
    }
};
static_assert(WaitStrategyConcept<NoWaitStrategy>);

} // namespace opencmw::disruptor

#endif // WAIT_STRATEGY_CPP
