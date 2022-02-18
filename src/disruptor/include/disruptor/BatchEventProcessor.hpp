#pragma once

#include "Exception.hpp"
#include "IDataProvider.hpp"
#include "IEventHandler.hpp"
#include "IEventProcessor.hpp"
#include "IEventProcessorSequenceAware.hpp"
#include "IExceptionHandler.hpp"
#include "ILifecycleAware.hpp"
#include "ISequenceBarrier.hpp"
#include "ITimeoutHandler.hpp"
#include "Sequence.hpp"

namespace opencmw::disruptor {

/**
 * Convenience class for handling the batching semantics of consuming events from a RingBuffer<T>
 * and delegating the available events to an IEventHandler<T>. If the BatchEventProcessor<T>
 * also implements ILifecycleAware it will be notified just after the thread is started and just before the thread is shutdown.
 *
 * \tparam T Event implementation storing the data for sharing during exchange or parallel coordination of an event.
 */
template<typename T>
class BatchEventProcessor : public IEventProcessor {
private:
    std::atomic<bool>                     _running;
    std::shared_ptr<IDataProvider<T>>     _dataProvider;
    IDataProvider<T>                     &_dataProviderRef;
    std::shared_ptr<ISequenceBarrier>     _sequenceBarrier;
    ISequenceBarrier                     &_sequenceBarrierRef;
    std::shared_ptr<IEventHandler<T>>     _eventHandler;
    IEventHandler<T>                     &_eventHandlerRef;
    std::shared_ptr<Sequence>             _sequence;
    Sequence                             &_sequenceRef;
    std::shared_ptr<ITimeoutHandler>      _timeoutHandler;
    std::shared_ptr<IExceptionHandler<T>> _exceptionHandler;

public:
    /**
     * Construct a BatchEventProcessor<T>
     * that will automatically track the progress by updating its sequence when the IEventHandler<T>.OnEvent returns.
     *
     * \param dataProvider dataProvider to which events are published
     * \param sequenceBarrier SequenceBarrier on which it is waiting.
     * \param eventHandler eventHandler is the delegate to which events are dispatched.
     */
    BatchEventProcessor(const std::shared_ptr<IDataProvider<T>> &dataProvider,
            const std::shared_ptr<ISequenceBarrier>             &sequenceBarrier,
            const std::shared_ptr<IEventHandler<T>>             &eventHandler)
        : _running(false)
        , _dataProvider(dataProvider)
        , _dataProviderRef(*_dataProvider)
        , _sequenceBarrier(sequenceBarrier)
        , _sequenceBarrierRef(*_sequenceBarrier)
        , _eventHandler(eventHandler)
        , _eventHandlerRef(*_eventHandler)
        , _sequence(std::make_shared<Sequence>())
        , _sequenceRef(*_sequence) {
        auto processorSequenceAware = std::dynamic_pointer_cast<IEventProcessorSequenceAware>(eventHandler);
        if (processorSequenceAware != nullptr)
            processorSequenceAware->setSequenceCallback(_sequence);

        _timeoutHandler = std::dynamic_pointer_cast<ITimeoutHandler>(eventHandler);
    }

    /**
     * \see IEventProcessor::Sequence
     */
    std::shared_ptr<ISequence> sequence() const override {
        return _sequence;
    };

    /**
     * Signal that this IEventProcessor should stop when it has finished consuming at the next clean break. It will call ISequenceBarrier::Alert
     * to notify the thread to check status.
     * \see IEventProcessor
     * \see ISequenceBarrier::Alert
     */
    void halt() override {
        _running = false;
        _sequenceBarrier->alert();
    }

    /**
     * \see IEventProcessor::IsRunning
     */
    bool isRunning() const override {
        return _running;
    }

    /**
     * Set a new IExceptionHandler<T> for handling exceptions propagated out of the BatchEventProcessor<T>
     *
     * \param exceptionHandler exceptionHandler to replace the existing exceptionHandler.
     */
    void setExceptionHandler(const std::shared_ptr<IExceptionHandler<T>> &exceptionHandler) {
        if (exceptionHandler == nullptr)
            throw std::invalid_argument("exception handler cannot be nullptr"); // TODO: do not use shared_ptr argument?

        _exceptionHandler = exceptionHandler;
    }

    /**
     * It is ok to have another thread rerun this method after a halt().
     */
    void run() override {
        if (_running.exchange(true) != false) {
            throw std::runtime_error("Thread is already running");
        }

        _sequenceBarrierRef.clearAlert();

        notifyStart();

        auto nextSequence = _sequenceRef.value() + 1;

        T   *evt          = nullptr;

        while (true) {
            try {
                auto availableSequence = _sequenceBarrierRef.waitFor(nextSequence);

                while (nextSequence <= availableSequence) {
                    evt = &_dataProviderRef[nextSequence];
                    _eventHandlerRef.onEvent(*evt, nextSequence, nextSequence == availableSequence);
                    nextSequence++;
                }

                _sequenceRef.setValue(availableSequence);
            } catch (const TimeoutException &) {
                notifyTimeout(_sequenceRef.value());
            } catch (const AlertException &) {
                if (_running == false) {
                    break;
                }
            } catch (const std::exception &ex) {
                _exceptionHandler->handleEventException(ex, nextSequence, *evt);
                _sequenceRef.setValue(nextSequence);
                nextSequence++;
            }
        }

        notifyShutdown();
        _running = false;
    }

private:
    void notifyTimeout(std::int64_t availableSequence) const {
        try {
            if (_timeoutHandler)
                _timeoutHandler->onTimeout(availableSequence);
        } catch (std::exception &ex) {
            if (_exceptionHandler)
                _exceptionHandler->handleOnTimeoutException(ex, availableSequence);
        }
    }

    void notifyStart() {
        auto sequenceReportingHandler = std::dynamic_pointer_cast<ILifecycleAware>(_eventHandler);
        if (sequenceReportingHandler != nullptr) {
            try {
                sequenceReportingHandler->onStart();
            } catch (std::exception &ex) {
                if (_exceptionHandler)
                    _exceptionHandler->handleOnStartException(ex);
            }
        }
    }

    void notifyShutdown() {
        auto sequenceReportingHandler = std::dynamic_pointer_cast<ILifecycleAware>(_eventHandler);
        if (sequenceReportingHandler != nullptr) {
            try {
                sequenceReportingHandler->onShutdown();
            } catch (std::exception &ex) {
                if (_exceptionHandler)
                    _exceptionHandler->handleOnShutdownException(ex);
            }
        }
    }
};

} // namespace opencmw::disruptor
