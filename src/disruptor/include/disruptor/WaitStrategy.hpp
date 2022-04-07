#ifndef WAIT_STRATEGY_CPP
#define WAIT_STRATEGY_CPP

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

#include <SpinWait.hpp>

#include "Exception.hpp"
#include "ISequenceBarrier.hpp"
#include "Sequence.hpp"

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
inline constexpr bool isWaitStrategy = requires(T /*const*/ t, const std::int64_t sequence, const Sequence &cursor, std::vector<std::shared_ptr<Sequence>> &dependentSequences, ISequenceBarrier &barrier) {
    { t.waitFor(sequence, cursor, dependentSequences, barrier) } -> std::same_as<std::int64_t>;
};
static_assert(!isWaitStrategy<int>);

/**
 * signal those waiting that the cursor has advanced.
 */
template<typename T>
inline constexpr bool hasSignalAllWhenBlocking = requires(T /*const*/ t) {
    { t.signalAllWhenBlocking() } -> std::same_as<void>;
};
static_assert(!hasSignalAllWhenBlocking<int>);

template<typename T>
concept WaitStrategy = isWaitStrategy<T>;

/**
 * Blocking strategy that uses a lock and condition variable for IEventProcessor's waiting on a barrier.
 * This strategy should be used when performance and low-latency are not as important as CPU resource.
 */
class BlockingWaitStrategy {
    std::recursive_mutex        _gate;
    std::condition_variable_any _conditionVariable;

public:
    std::int64_t waitFor(const std::int64_t sequence, const Sequence &cursor, const std::vector<std::shared_ptr<Sequence>> &dependentSequences, ISequenceBarrier &barrier) {
        if (cursor.value() < sequence) {
            std::unique_lock uniqueLock(_gate);

            while (cursor.value() < sequence) {
                barrier.checkAlert();
                _conditionVariable.wait(uniqueLock);
            }
        }

        std::int64_t availableSequence;
        while ((availableSequence = detail::getMinimumSequence(dependentSequences)) < sequence) {
            barrier.checkAlert();
        }

        return availableSequence;
    }

    void signalAllWhenBlocking() {
        std::unique_lock uniqueLock(_gate);
        _conditionVariable.notify_all();
    }
};
static_assert(WaitStrategy<BlockingWaitStrategy>);

/**
 * Busy Spin strategy that uses a busy spin loop for IEventProcessor's waiting on a barrier.
 * This strategy will use CPU resource to avoid syscalls which can introduce latency jitter.  It is best used when threads can be bound to specific CPU cores.
 */
struct BusySpinWaitStrategy {
    std::int64_t waitFor(const std::int64_t sequence, const Sequence & /*cursor*/, const std::vector<std::shared_ptr<Sequence>> &dependentSequences, ISequenceBarrier &barrier) const {
        std::int64_t availableSequence;
        while ((availableSequence = detail::getMinimumSequence(dependentSequences)) < sequence) {
            barrier.checkAlert();
        }
        return availableSequence;
    }
};
static_assert(WaitStrategy<BusySpinWaitStrategy>);
static_assert(!hasSignalAllWhenBlocking<BusySpinWaitStrategy>);

/**
 * Sleeping strategy that initially spins, then uses a std::this_thread::yield(), and eventually sleep. This strategy is a good compromise between performance and CPU resource.
 * Latency spikes can occur after quiet periods.
 */
class SleepingWaitStrategy {
    static const std::int32_t _defaultRetries = 200;
    std::int32_t              _retries        = 0;

public:
    explicit SleepingWaitStrategy(std::int32_t retries = _defaultRetries)
        : _retries(retries) {
    }

    std::int64_t waitFor(const std::int64_t sequence, const Sequence & /*cursor*/, const std::vector<std::shared_ptr<Sequence>> &dependentSequences, ISequenceBarrier &barrier) const {
        auto       counter    = _retries;
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
        while ((availableSequence = detail::getMinimumSequence(dependentSequences)) < sequence) {
            counter = waitMethod();
        }

        return availableSequence;
    }
};
static_assert(WaitStrategy<SleepingWaitStrategy>);
static_assert(!hasSignalAllWhenBlocking<SleepingWaitStrategy>);

/**
 * Spin strategy that uses a SpinWait for IEventProcessors waiting on a barrier.
 * This strategy is a good compromise between performance and CPU resource. Latency spikes can occur after quiet periods.
 */
struct SpinWaitWaitStrategy {
    std::int64_t waitFor(const std::int64_t sequence, const Sequence & /*cursor*/, const std::vector<std::shared_ptr<Sequence>> &dependentSequences, ISequenceBarrier &barrier) const {
        std::int64_t availableSequence;

        SpinWait     spinWait;
        while ((availableSequence = detail::getMinimumSequence(dependentSequences)) < sequence) {
            barrier.checkAlert();
            spinWait.spinOnce();
        }

        return availableSequence;
    }
};
static_assert(WaitStrategy<SpinWaitWaitStrategy>);
static_assert(!hasSignalAllWhenBlocking<SpinWaitWaitStrategy>);

class TimeoutBlockingWaitStrategy {
    using Clock = std::conditional_t<std::chrono::high_resolution_clock::is_steady, std::chrono::high_resolution_clock, std::chrono::steady_clock>;
    Clock::duration             _timeout;
    std::recursive_mutex        _gate;
    std::condition_variable_any _conditionVariable;

public:
    explicit TimeoutBlockingWaitStrategy(Clock::duration timeout)
        : _timeout(timeout) {}

    std::int64_t waitFor(const std::int64_t sequence, const Sequence &cursor, const std::vector<std::shared_ptr<Sequence>> &dependentSequences, ISequenceBarrier &barrier) {
        auto timeSpan = std::chrono::microseconds(std::chrono::duration_cast<std::chrono::microseconds>(_timeout).count());

        if (cursor.value() < sequence) {
            std::unique_lock uniqueLock(_gate);

            while (cursor.value() < sequence) {
                barrier.checkAlert();

                if (_conditionVariable.wait_for(uniqueLock, timeSpan) == std::cv_status::timeout) {
                    throw TimeoutException();
                }
            }
        }

        std::int64_t availableSequence;
        while ((availableSequence = detail::getMinimumSequence(dependentSequences)) < sequence) {
            barrier.checkAlert();
        }

        return availableSequence;
    }

    void signalAllWhenBlocking() {
        std::unique_lock uniqueLock(_gate);
        _conditionVariable.notify_all();
    }
};
static_assert(WaitStrategy<TimeoutBlockingWaitStrategy>);
static_assert(hasSignalAllWhenBlocking<TimeoutBlockingWaitStrategy>);

/**
 * Yielding strategy that uses a Thread.Yield() for IEventProcessors waiting on a barrier after an initially spinning.
 * This strategy is a good compromise between performance and CPU resource without incurring significant latency spikes.
 */
class YieldingWaitStrategy {
    const std::size_t _spinTries = 100;

public:
    std::int64_t waitFor(const std::int64_t sequence, const Sequence & /*cursor*/, const std::vector<std::shared_ptr<Sequence>> &dependentSequences, ISequenceBarrier &barrier) const {
        auto       counter    = _spinTries;
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
        while ((availableSequence = detail::getMinimumSequence(dependentSequences)) < sequence) {
            counter = waitMethod();
        }

        return availableSequence;
    }
};
static_assert(WaitStrategy<YieldingWaitStrategy>);
static_assert(!hasSignalAllWhenBlocking<YieldingWaitStrategy>);

struct NoWaitStrategy {
    std::int64_t waitFor(const std::int64_t sequence, const Sequence & /*cursor*/, const std::vector<std::shared_ptr<Sequence>> & /*dependentSequences*/, const ISequenceBarrier & /*barrier*/) const {
        // wait for nothing
        return sequence;
    }
};
static_assert(WaitStrategy<NoWaitStrategy>);
static_assert(!hasSignalAllWhenBlocking<NoWaitStrategy>);

} // namespace opencmw::disruptor

#endif // WAIT_STRATEGY_CPP
