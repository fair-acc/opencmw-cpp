#pragma once

#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>

#include "IEventProcessor.hpp"
#include "IEventReleaseAware.hpp"
#include "IEventReleaser.hpp"
#include "IExceptionHandler.hpp"
#include "ILifecycleAware.hpp"
#include "ISequenceBarrier.hpp"
#include "ITimeoutHandler.hpp"
#include "IWorkHandler.hpp"
#include "RingBuffer.hpp"
#include "Sequence.hpp"

namespace opencmw::disruptor {
/**
 * A WorkProcessor<T> wraps a single IWorkHandler<T>, effectively consuming the sequence and ensuring appropriate barriers.
 * Generally, this will be used as part of a WorkerPool<T>
 *
 * \tparam T event implementation storing the details for the work to processed.
 */
template<typename T, std::size_t SIZE>
class WorkProcessor : public IEventProcessor {
private:
    class EventReleaser;

    struct PrivateKey {};

    std::atomic<std::int32_t>             _running{ 0 };
    std::shared_ptr<Sequence>             _sequence = std::make_shared<Sequence>();
    std::shared_ptr<RingBuffer<T, SIZE>>  _ringBuffer;
    std::shared_ptr<ISequenceBarrier>     _sequenceBarrier;
    std::shared_ptr<IWorkHandler<T>>      _workHandler;
    std::shared_ptr<IExceptionHandler<T>> _exceptionHandler;
    std::shared_ptr<Sequence>             _workSequence;
    std::shared_ptr<IEventReleaser>       _eventReleaser;
    std::shared_ptr<ITimeoutHandler>      _timeoutHandler;

public:
    /**
     * Construct a WorkProcessor<T>.
     *
     * \param ringBuffer ringBuffer to which events are published.
     * \param sequenceBarrier sequenceBarrier on which it is waiting.
     * \param workHandler workHandler is the delegate to which events are dispatched.
     * \param exceptionHandler exceptionHandler to be called back when an error occurs
     * \param workSequence workSequence from which to claim the next event to be worked on. It should always be initialised Disruptor.Sequence.InitialCursorValue
     */
    static std::shared_ptr<WorkProcessor<T, SIZE>> create(const std::shared_ptr<RingBuffer<T, SIZE>> &ringBuffer,
            const std::shared_ptr<ISequenceBarrier>                                                  &sequenceBarrier,
            const std::shared_ptr<IWorkHandler<T>>                                                   &workHandler,
            const std::shared_ptr<IExceptionHandler<T>>                                              &exceptionHandler,
            const std::shared_ptr<Sequence>                                                          &workSequence) {
        auto processor            = std::make_shared<WorkProcessor<T, SIZE>>(ringBuffer, sequenceBarrier, workHandler, exceptionHandler, workSequence, PrivateKey());
        processor->_eventReleaser = std::make_shared<EventReleaser>(processor);

        if (auto eventReleaseAwareHandler = std::dynamic_pointer_cast<IEventReleaseAware>(processor->_workHandler); eventReleaseAwareHandler != nullptr) {
            eventReleaseAwareHandler->setEventReleaser(processor->_eventReleaser);
        }

        return processor;
    }

    /**
     * Construct a WorkProcessor<T>.
     *
     * \param ringBuffer ringBuffer to which events are published.
     * \param sequenceBarrier sequenceBarrier on which it is waiting.
     * \param workHandler workHandler is the delegate to which events are dispatched.
     * \param exceptionHandler exceptionHandler to be called back when an error occurs
     * \param workSequence workSequence from which to claim the next event to be worked on.  It should always be initialised Disruptor.Sequence.InitialCursorValue
     */
    WorkProcessor(const std::shared_ptr<RingBuffer<T, SIZE>> &ringBuffer,
            const std::shared_ptr<ISequenceBarrier>          &sequenceBarrier,
            const std::shared_ptr<IWorkHandler<T>>           &workHandler,
            const std::shared_ptr<IExceptionHandler<T>>      &exceptionHandler,
            const std::shared_ptr<Sequence>                  &workSequence,
            PrivateKey)
        : _ringBuffer(ringBuffer)
        , _sequenceBarrier(sequenceBarrier)
        , _workHandler(workHandler)
        , _exceptionHandler(exceptionHandler)
        , _workSequence(workSequence)
        , _timeoutHandler(std::dynamic_pointer_cast<ITimeoutHandler>(_workHandler)) {}

    /**
     * Return a reference to the IEventProcessor.Sequence being used by this IEventProcessor
     */
    [[nodiscard]] std::shared_ptr<Sequence> sequence() const override {
        return _sequence;
    }

    /**
     * Signal that this IEventProcessor should stop when it has finished consuming at the next clean break. It will call ISequenceBarrier::alert() to notify the thread to check status.
     */
    void halt() override {
        _running = 0;
        _sequenceBarrier->alert();
    }

    [[nodiscard]] bool isRunning() const override { return _running == 1; }

    /**
     * It is ok to have another thread re-run this method after a halt().
     */
    void run() override {
        if (std::atomic_exchange(&_running, 1) != 0) {
            throw std::logic_error("Thread is already running");
        }
        _sequenceBarrier->clearAlert();

        notifyStart();

        auto processedSequence       = true;
        auto cachedAvailableSequence = std::numeric_limits<std::int64_t>::min();
        auto nextSequence            = _sequence->value();

        T   *eventRef                = nullptr;

        while (true) {
            try {
                if (processedSequence) {
                    processedSequence = false;
                    do {
                        nextSequence = _workSequence->value() + 1L;
                        _sequence->setValue(nextSequence - 1L);
                    } while (!_workSequence->compareAndSet(nextSequence - 1L, nextSequence));
                }

                if (cachedAvailableSequence >= nextSequence) {
                    eventRef = &(*_ringBuffer)[nextSequence];
                    _workHandler->onEvent(*eventRef);
                    processedSequence = true;
                } else {
                    cachedAvailableSequence = _sequenceBarrier->waitFor(nextSequence);
                }
            } catch (const TimeoutException &) {
                notifyTimeout(_sequence->value());
            } catch (const AlertException &) {
                if (_running == 0) {
                    break;
                }
            } catch (std::exception &ex) {
                _exceptionHandler->handleEventException(ex, nextSequence, *eventRef);
                processedSequence = true;
            }
        }

        notifyShutdown();

        _running = 0;
    }

private:
    void notifyTimeout(std::int64_t availableSequence) {
        try {
            if (_timeoutHandler != nullptr)
                _timeoutHandler->onTimeout(availableSequence);
        } catch (std::exception &ex) {
            _exceptionHandler->handleOnTimeoutException(ex, availableSequence);
        }
    }

    void notifyStart() {
        auto lifecycleAware = std::dynamic_pointer_cast<ILifecycleAware>(_workHandler);
        if (lifecycleAware != nullptr) {
            try {
                lifecycleAware->onStart();
            } catch (std::exception &ex) {
                _exceptionHandler->handleOnStartException(ex);
            }
        }
    }

    void notifyShutdown() {
        auto lifecycleAware = std::dynamic_pointer_cast<ILifecycleAware>(_workHandler);
        if (lifecycleAware != nullptr) {
            try {
                lifecycleAware->onShutdown();
            } catch (std::exception &ex) {
                _exceptionHandler->handleOnShutdownException(ex);
            }
        }
    }
};

template<typename T, std::size_t SIZE>
class WorkProcessor<T, SIZE>::EventReleaser : public IEventReleaser {
public:
    explicit EventReleaser(const std::shared_ptr<WorkProcessor<T, SIZE>> &workProcessor)
        : _workProcessor(workProcessor) {
    }

    void release() override {
        auto workProcessor = _workProcessor.lock();
        if (workProcessor != nullptr)
            workProcessor->sequence()->setValue(std::numeric_limits<std::int64_t>::max());
    }

private:
    std::weak_ptr<WorkProcessor<T, SIZE>> _workProcessor;
};

} // namespace opencmw::disruptor
