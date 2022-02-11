#pragma once

#include "IDataProvider.hpp"
#include "IEventHandler.hpp"
#include "IEventProcessor.hpp"
#include "IEventProcessorSequenceAware.hpp"
#include "IExceptionHandler.hpp"
#include "ILifecycleAware.hpp"
#include "ISequenceBarrier.hpp"
#include "ITimeoutHandler.hpp"
#include "Sequence.hpp"
#include "exception.hpp"

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
    std::atomic<bool>                     m_running;
    std::shared_ptr<IDataProvider<T>>     m_dataProvider;
    IDataProvider<T>                     &m_dataProviderRef;
    std::shared_ptr<ISequenceBarrier>     m_sequenceBarrier;
    ISequenceBarrier                     &m_sequenceBarrierRef;
    std::shared_ptr<IEventHandler<T>>     m_eventHandler;
    IEventHandler<T>                     &m_eventHandlerRef;
    std::shared_ptr<Sequence>             m_sequence;
    Sequence                             &m_sequenceRef;
    std::shared_ptr<ITimeoutHandler>      m_timeoutHandler;
    std::shared_ptr<IExceptionHandler<T>> m_exceptionHandler;

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
        : m_running(false)
        , m_dataProvider(dataProvider)
        , m_dataProviderRef(*m_dataProvider)
        , m_sequenceBarrier(sequenceBarrier)
        , m_sequenceBarrierRef(*m_sequenceBarrier)
        , m_eventHandler(eventHandler)
        , m_eventHandlerRef(*m_eventHandler)
        , m_sequence(std::make_shared<Sequence>())
        , m_sequenceRef(*m_sequence) {
        auto processorSequenceAware = std::dynamic_pointer_cast<IEventProcessorSequenceAware>(eventHandler);
        if (processorSequenceAware != nullptr)
            processorSequenceAware->setSequenceCallback(m_sequence);

        m_timeoutHandler = std::dynamic_pointer_cast<ITimeoutHandler>(eventHandler);
    }

    /**
     * \see IEventProcessor::Sequence
     */
    std::shared_ptr<ISequence> sequence() const override {
        return m_sequence;
    };

    /**
     * Signal that this IEventProcessor should stop when it has finished consuming at the next clean break. It will call ISequenceBarrier::Alert
     * to notify the thread to check status.
     * \see IEventProcessor
     * \see ISequenceBarrier::Alert
     */
    void halt() override {
        m_running = false;
        m_sequenceBarrier->alert();
    }

    /**
     * \see IEventProcessor::IsRunning
     */
    bool isRunning() const override {
        return m_running;
    }

    /**
     * Set a new IExceptionHandler<T> for handling exceptions propagated out of the BatchEventProcessor<T>
     *
     * \param exceptionHandler exceptionHandler to replace the existing exceptionHandler.
     */
    void setExceptionHandler(const std::shared_ptr<IExceptionHandler<T>> &exceptionHandler) {
        if (exceptionHandler == nullptr)
            throw std::invalid_argument("exception handler cannot be nullptr"); // TODO: do not use shared_ptr argument?

        m_exceptionHandler = exceptionHandler;
    }

    /**
     * It is ok to have another thread rerun this method after a halt().
     */
    void run() override {
        if (m_running.exchange(true) != false) {
            throw std::runtime_error("Thread is already running");
        }

        m_sequenceBarrierRef.clearAlert();

        notifyStart();

        auto nextSequence = m_sequenceRef.value() + 1;

        T   *evt          = nullptr;

        while (true) {
            try {
                auto availableSequence = m_sequenceBarrierRef.waitFor(nextSequence);

                while (nextSequence <= availableSequence) {
                    evt = &m_dataProviderRef[nextSequence];
                    m_eventHandlerRef.onEvent(*evt, nextSequence, nextSequence == availableSequence);
                    nextSequence++;
                }

                m_sequenceRef.setValue(availableSequence);
            } catch (const timeout_exception&) {
                notifyTimeout(m_sequenceRef.value());
            } catch (const alert_exception&) {
                if (m_running == false) {
                    break;
                }
            } catch (const std::exception &ex) {
                m_exceptionHandler->handleEventException(ex, nextSequence, *evt);
                m_sequenceRef.setValue(nextSequence);
                nextSequence++;
            }
        }

        notifyShutdown();
        m_running = false;
    }

private:
    void notifyTimeout(std::int64_t availableSequence) const {
        try {
            if (m_timeoutHandler)
                m_timeoutHandler->onTimeout(availableSequence);
        } catch (std::exception &ex) {
            if (m_exceptionHandler)
                m_exceptionHandler->handleOnTimeoutException(ex, availableSequence);
        }
    }

    void notifyStart() {
        auto sequenceReportingHandler = std::dynamic_pointer_cast<ILifecycleAware>(m_eventHandler);
        if (sequenceReportingHandler != nullptr) {
            try {
                sequenceReportingHandler->onStart();
            } catch (std::exception &ex) {
                if (m_exceptionHandler)
                    m_exceptionHandler->handleOnStartException(ex);
            }
        }
    }

    void notifyShutdown() {
        auto sequenceReportingHandler = std::dynamic_pointer_cast<ILifecycleAware>(m_eventHandler);
        if (sequenceReportingHandler != nullptr) {
            try {
                sequenceReportingHandler->onShutdown();
            } catch (std::exception &ex) {
                if (m_exceptionHandler)
                    m_exceptionHandler->handleOnShutdownException(ex);
            }
        }
    }
};

} // namespace opencmw::disruptor
