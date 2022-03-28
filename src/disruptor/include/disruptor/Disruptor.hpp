#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <ratio>
#include <type_traits>

#include "BasicExecutor.hpp"
#include "BatchEventProcessor.hpp"
#include "ClaimStrategy.hpp"
#include "ConsumerRepository.hpp"
#include "EventHandlerGroup.hpp"
#include "ExceptionHandlerSetting.hpp"
#include "ExceptionHandlerWrapper.hpp"
#include "IEventHandler.hpp"
#include "IEventProcessorFactory.hpp"
#include "ITaskScheduler.hpp"
#include "IWorkHandler.hpp"
#include "ProcessingSequenceBarrier.hpp"
#include "RingBuffer.hpp"
#include "Util.hpp"
#include "WorkerPool.hpp"

namespace opencmw::disruptor {

/**
 * A DSL-style API for setting up the disruptor pattern around a ring buffer (aka the Builder pattern). A simple example of setting up the disruptor with two event handlers that
 * must process events in order:
 * <code>
 *      Disruptor{MyEvent} disruptor = new Disruptor{MyEvent}(MyEvent.FACTORY, 32, Executors.NewCachedThreadPool());
 *      EventHandler{MyEvent} handler1 = new EventHandler{MyEvent}() { ... };
 *      EventHandler{MyEvent} handler2 = new EventHandler{MyEvent}() { ... };
 *      disruptor.HandleEventsWith(handler1);
 *      disruptor.After(handler1).HandleEventsWith(handler2);
 *      RingBuffer ringBuffer = disruptor.Start();
 * </code>
 *
 * \tparam T the type of event used.
 */
template<typename T, std::size_t SIZE, WaitStrategy WAIT_STRATEGY, template<std::size_t, typename> typename CLAIM_STRATEGY = MultiThreadedStrategy>
requires opencmw::is_power2_v<SIZE>
class DisruptorCore : public std::enable_shared_from_this<DisruptorCore<T, SIZE, WAIT_STRATEGY, CLAIM_STRATEGY>> {
    using Clock                 = std::conditional_t<std::chrono::high_resolution_clock::is_steady, std::chrono::high_resolution_clock, std::chrono::steady_clock>;
    using Duration              = Clock::duration;
    using EventHandlerGroupType = EventHandlerGroup<T, SIZE, WAIT_STRATEGY, CLAIM_STRATEGY, ::opencmw::disruptor::DisruptorCore>;

    std::shared_ptr<RingBuffer<T, SIZE, WAIT_STRATEGY, CLAIM_STRATEGY>> _ringBuffer;
    std::shared_ptr<IExecutor>                                          _executor;
    std::shared_ptr<ConsumerRepository<T>>                              _consumerRepository = std::make_shared<ConsumerRepository<T>>();
    std::shared_ptr<IExceptionHandler<T>>                               _exceptionHandler   = std::make_shared<ExceptionHandlerWrapper<T>>();
    std::atomic<int>                                                    _started{ 0 };

public:
    /**
     * Create a new Disruptor.
     *
     * \param ringBufferSize the size of the ring buffer, must be power of 2
     * \param taskScheduler a TaskScheduler to create threads to for processors
     */
    DisruptorCore(const std::shared_ptr<ITaskScheduler> &taskScheduler)
        : DisruptorCore(std::make_shared<RingBuffer<T, SIZE, WAIT_STRATEGY, CLAIM_STRATEGY>>(), std::make_shared<BasicExecutor>(taskScheduler)) {
    }

    /**
     * Set up event handlers to handle events from the ring buffer. These handlers will process events as soon as they become available, in parallel.
     * <code>
     *  dw.HandleEventsWith(A).Then(B);
     * </code>
     *
     * \param handlers the event handlers that will process events
     * \returns EventHandlerGroupType that can be used to chain dependencies.
     */
    std::shared_ptr<EventHandlerGroupType> handleEventsWith(const std::vector<std::shared_ptr<IEventHandler<T>>> &handlers) {
        return createEventProcessors({}, handlers);
    }

    /**
     * Set up event handlers to handle events from the ring buffer. These handlers will process events as soon as they become available, in parallel.
     * <code>
     *  dw.HandleEventsWith(A).Then(B);
     * </code>
     *
     * \param handler the event handler that will process events
     * \returns EventHandlerGroupType that can be used to chain dependencies.
     */
    std::shared_ptr<EventHandlerGroupType> handleEventsWith(const std::shared_ptr<IEventHandler<T>> &handler) {
        return handleEventsWith(std::vector<std::shared_ptr<IEventHandler<T>>>{ handler });
    }

    /**
     * Set up custom event processors to handle events from the ring buffer. The Disruptor will automatically start these processors when Start is called.
     * This method can be used as the start of a chain. For example if the handler A must process events before handler B: dw.handleEventsWith(A).Then(B);
     * Since this is the start of the chain, the processor factories will always be passed an empty Sequence array, so the factory isn't necessary in this case.
     * This method is provided for consistency with EventHandlerGroup<T>::HandleEventsWith(IEventProcessorFactory<T>) and EventHandlerGroup<T>.Then(IEventProcessorFactory<T>)
     * which do have barrier sequences to provide.
     *
     * \param eventProcessorFactories the event processor factories to use to create the event processors that will process events.
     * \returns EventHandlerGroupType that can be used to chain dependencies.
     */
    std::shared_ptr<EventHandlerGroupType> handleEventsWith(const std::vector<std::shared_ptr<IEventProcessorFactory<T>>> &eventProcessorFactories) {
        return createEventProcessors({}, eventProcessorFactories);
    }

    /**
     * Set up custom event processors to handle events from the ring buffer. The Disruptor will automatically start these processors when Start
     * is called. This method can be used as the start of a chain. For example if the handler A must process events before handler B: dw.handleEventsWith(A).then(B).
     * Since this is the start of the chain, the processor factories will always be passed an empty Sequence
     * array, so the factory isn't necessary in this case. This method is provided for consistency with EventHandlerGroupType::handleEventsWith(IEventProcessorFactory<T>)
     * and EventHandlerGroupType::Then(IEventProcessorFactory<T>) which do have barrier sequences to provide.
     *
     * \param eventProcessorFactory the event processor factory to use to create the event processors that will process events.
     * \returns EventHandlerGroupType that can be used to chain dependencies.
     */
    std::shared_ptr<EventHandlerGroupType> handleEventsWith(const std::shared_ptr<IEventProcessorFactory<T>> &eventProcessorFactory) {
        return handleEventsWith(std::vector<std::shared_ptr<IEventProcessorFactory<T>>>{ eventProcessorFactory });
    }

    /**
     * Set up custom event processors to handle events from the ring buffer. The Disruptor will automatically start this processors when Start is called.
     * This method can be used as the start of a chain. For example if the processor A must process events before handler B: dw.handleEventsWith(A).then(B).
     *
     * \param processors the event processors that will process events
     * \returns EventHandlerGroupType that can be used to chain dependencies.
     */
    std::shared_ptr<EventHandlerGroupType> handleEventsWith(const std::vector<std::shared_ptr<IEventProcessor>> &processors) {
        for (auto &&processor : processors) {
            _consumerRepository->add(processor);
        }
        return std::make_shared<EventHandlerGroupType>(this->shared_from_this(), _consumerRepository, util::getSequencesFor(processors));
    }

    /**
     * Set up custom event processors to handle events from the ring buffer. The Disruptor will automatically start this processors when Start is called.
     * This method can be used as the start of a chain. For example if the processor A must process events before handler B: dw.handleEventsWith(A).then(B).
     *
     * \param processor the event processor that will process events
     * \returns EventHandlerGroupType that can be used to chain dependencies.
     */
    std::shared_ptr<EventHandlerGroupType> handleEventsWith(const std::shared_ptr<IEventProcessor> &processor) {
        return handleEventsWith(std::vector<std::shared_ptr<IEventProcessor>>{ processor });
    }

    /**
     * Set up a WorkerPool to distribute an event to one of a pool of work handler threads. Each event will only be processed by one of the work handlers.
     * The Disruptor will automatically start this processors when Start is called.
     *
     * \param workHandlers the work handlers that will process events.
     * \returns EventHandlerGroupType that can be used to chain dependencies.
     */
    std::shared_ptr<EventHandlerGroupType> handleEventsWithWorkerPool(const std::vector<std::shared_ptr<IWorkHandler<T>>> &workHandlers) {
        return createWorkerPool({}, workHandlers);
    }

    /**
     * Set up a WorkerPool to distribute an event to one of a pool of work handler threads. Each event will only be processed by one of the work handlers.
     * The Disruptor will automatically start this processors when Start is called.
     *
     * \param workHandler the work handler that will process events.
     * \returns EventHandlerGroupType that can be used to chain dependencies.
     */
    std::shared_ptr<EventHandlerGroupType> handleEventsWithWorkerPool(const std::shared_ptr<IWorkHandler<T>> &workHandler) {
        return handleEventsWithWorkerPool(std::vector<std::shared_ptr<IWorkHandler<T>>>{ workHandler });
    }

    /**
     * Specify an exception handler to be used for any future event handlers. Note that only event handlers set up after calling this method will use the exception handler.
     *
     * \param exceptionHandler the exception handler to use for any future IEventProcessor
     */
    //[Obsolete("This method only applies to future event handlers. Use setDefaultExceptionHandler instead which applies to existing and new event handlers.")]
    void handleExceptionsWith(const std::shared_ptr<IExceptionHandler<T>> &exceptionHandler) {
        _exceptionHandler = exceptionHandler;
    }

    /**
     * Specify an exception handler to be used for event handlers and worker pools created by this Disruptor.
     * The exception handler will be used by existing and future event handlers and worker pools created by this Disruptor instance.
     *
     * \param exceptionHandler the exception handler to use
     */
    void setDefaultExceptionHandler(const std::shared_ptr<IExceptionHandler<T>> &exceptionHandler) {
        checkNotStarted();
        std::dynamic_pointer_cast<ExceptionHandlerWrapper<T>>(_exceptionHandler)->switchTo(exceptionHandler);
    }

    /**
     * Override the default exception handler for a specific handler.
     *
     * \param eventHandler eventHandler the event handler to set a different exception handler for
     * \returns ExceptionHandlerSetting<T> dsl object - intended to be used by chaining the with method call
     */
    std::shared_ptr<ExceptionHandlerSetting<T>> handleExceptionsFor(const std::shared_ptr<IEventHandler<T>> &eventHandler) {
        return std::make_shared<ExceptionHandlerSetting<T>>(eventHandler, _consumerRepository);
    }

    /**
     * Create a group of event handlers to be used as a dependency. For example if the handler A must process events before handler B: dw.after(A).handleEventsWith(B)
     *
     * \param handlers the event handlers, previously set up with handleEventsWith(Disruptor.IEventHandler{T}[]), that will form the barrier for subsequent handlers or processors.
     * \returns EventHandlerGroupType that can be used to setup a dependency barrier over the specified event handlers.
     */
    std::shared_ptr<EventHandlerGroupType> after(const std::vector<std::shared_ptr<IEventHandler<T>>> &handlers) {
        std::vector<std::shared_ptr<Sequence>> sequences;
        for (auto &&handler : handlers)
            sequences.push_back(_consumerRepository->getSequenceFor(handler));

        return std::make_shared<EventHandlerGroupType>(this->shared_from_this(), _consumerRepository, sequences);
    }

    /**
     * Create a group of event handlers to be used as a dependency. For example if the handler A must process events before handler B: dw.after(A).handleEventsWith(B)
     *
     * \param handler the event handler, previously set up with HandleEventsWith(IEventHandler<T>), that will form the barrier for subsequent handlers or processors.
     * \returns EventHandlerGroupType that can be used to setup a dependency barrier over the specified event handlers.
     */
    std::shared_ptr<EventHandlerGroupType> after(const std::shared_ptr<IEventHandler<T>> &handler) {
        return after(std::vector<std::shared_ptr<IEventHandler<T>>>{ handler });
    }

    /**
     * Create a group of event processors to be used as a dependency.
     *
     * \param processors the event processors, previously set up with handleEventsWith(IEventHandler<T>), that will form the barrier for subsequent handlers or processors.
     * \returns EventHandlerGroupType that can be used to setup a ISequenceBarrier over the specified event processors.
     */
    std::shared_ptr<EventHandlerGroupType> after(const std::vector<std::shared_ptr<IEventProcessor>> &processors) {
        for (auto &&processor : processors) {
            _consumerRepository->add(processor);
        }

        return std::make_shared<EventHandlerGroupType>(this->shared_from_this(), _consumerRepository, util::getSequencesFor(processors));
    }

    /**
     * Create a group of event processors to be used as a dependency.
     *
     * \param processor the event processor, previously set up with handleEventsWith(IEventHandler<T>), that will form the barrier for subsequent handlers or processors.
     * \returns EventHandlerGroupType that can be used to setup a ISequenceBarrier over the specified event processors.
     */
    std::shared_ptr<EventHandlerGroupType> after(const std::shared_ptr<IEventProcessor> &processor) {
        return after(std::vector<std::shared_ptr<IEventProcessor>>{ processor });
    }

    /**
     * Publish an event to the ring buffer.
     *
     * \param eventTranslator the translator that will load data into the event
     */
    void publishEvent(const std::invocable<T, std::int64_t> auto &eventTranslator) {
        _ringBuffer->publishEvent(eventTranslator);
    }

    /**
     * Starts the event processors and returns the fully configured ring buffer. The ring buffer is set up to prevent overwriting any entry that
     * is yet to be processed by the slowest event processor. This method must only be called once after all event processors have been added.
     *
     * \returns the configured ring buffer
     */
    std::shared_ptr<EventStore<T>> start() {
        _ringBuffer->addGatingSequences(_consumerRepository->getLastSequenceInChain(true));

        checkOnlyStartedOnce();
        for (auto &&consumerInfo : *_consumerRepository) {
            consumerInfo->start(_executor);
        }

        return _ringBuffer;
    }

    /**
     * Calls IEventProcessor.halt on all of the event processors created via this disruptor.
     */
    void halt() {
        for (auto &&consumerInfo : *_consumerRepository) {
            consumerInfo->halt();
        }
    }

    /**
     * Waits until all events currently in the disruptor have been processed by all event processors and then halts the processors.
     * It is critical that publishing to the ring buffer has stopped before calling this method, otherwise it may never return.
     * This method will not shutdown the executor, nor will it await the final termination of the processor threads.
     */
    void shutdown() {
        try {
            shutdown(Duration::max());
        } catch (TimeoutException &ex) {
            _exceptionHandler->handleOnShutdownException(ex);
        }
    }

    /**
     * Waits until all events currently in the disruptor have been processed by all event processors and then halts the processors.
     * This method will not shutdown the executor, nor will it await the final termination of the processor threads
     *
     * \param timeout the amount of time to wait for all events to be processed. Duration::max() will give an infinite timeout
     */
    void shutdown(Duration timeout) {
        const auto waitInfinitely = timeout == Duration::max();

        const auto timeoutAt      = Clock::now() + timeout;
        while (hasBacklog()) {
            if (!waitInfinitely && timeout.count() >= 0 && Clock::now() > timeoutAt)
                throw std::runtime_error("timeout encountered during shutdown");
            std::this_thread::yield();
        }
        halt();
    }

    /**
     * The RingBuffer<T> used by this Disruptor. This is useful for creating custom event processors if the behaviour of BatchEventProcessor<T> is not suitable.
     */
    std::shared_ptr<RingBuffer<T, SIZE, WAIT_STRATEGY, CLAIM_STRATEGY>> ringBuffer() const {
        return _ringBuffer;
    }

    /**
     * The EventStore<T> used by this Disruptor. This is useful for creating custom event processors if the behaviour of BatchEventProcessor<T> is not suitable.
     */
    std::shared_ptr<EventStore<T>> eventStore() const {
        return _ringBuffer;
    }

    /**
     * Get the value of the cursor indicating the published sequence.
     */
    std::int64_t cursor() const {
        return _ringBuffer->cursor();
    }

    /**
     * The capacity of the data structure to hold entries.
     */
    std::int64_t bufferSize() const {
        return _ringBuffer->bufferSize();
    }

    /**
     * Get the event for a given sequence in the RingBuffer
     *
     * \param sequence sequence for the event
     * \returns event for the sequence
     */
    T &operator[](std::int64_t sequence) const {
        return _ringBuffer[sequence];
    }

    /**
     * Get the ISequenceBarrier used by a specific handler. Note that the ISequenceBarrier may be shared by multiple event handlers.
     *
     * \param handler the handler to get the barrier for
     * \returns the SequenceBarrier used by the given handler
     */
    std::shared_ptr<ISequenceBarrier> getBarrierFor(const std::shared_ptr<IEventHandler<T>> &handler) {
        return _consumerRepository->getBarrierFor(handler);
    }

    std::shared_ptr<EventHandlerGroupType> createEventProcessors(const std::vector<std::shared_ptr<Sequence>> &barrierSequences,
            const std::vector<std::shared_ptr<IEventHandler<T>>>                                              &eventHandlers) {
        checkNotStarted();

        std::vector<std::shared_ptr<Sequence>> processorSequences;
        processorSequences.reserve(eventHandlers.size());

        auto barrier = newBarrier(_ringBuffer, barrierSequences);

        for (auto &&eventHandler : eventHandlers) {
            auto batchEventProcessor = std::make_shared<BatchEventProcessor<T>>(_ringBuffer, barrier, eventHandler);
            if (_exceptionHandler != nullptr)
                batchEventProcessor->setExceptionHandler(_exceptionHandler);

            _consumerRepository->add(batchEventProcessor, eventHandler, barrier);
            processorSequences.push_back(batchEventProcessor->sequence());
        }

        if (!processorSequences.empty()) {
            _consumerRepository->unMarkEventProcessorsAsEndOfChain(barrierSequences);
        }

        return std::make_shared<EventHandlerGroupType>(this->shared_from_this(), _consumerRepository, processorSequences);
    }

    std::shared_ptr<EventHandlerGroupType> createWorkerPool(const std::vector<std::shared_ptr<Sequence>> &barrierSequences,
            const std::vector<std::shared_ptr<IWorkHandler<T>>>                                          &workHandlers) {
        auto sequenceBarrier = _ringBuffer->newBarrier(barrierSequences);
        auto workerPool      = std::make_shared<WorkerPool<T>>(_ringBuffer, sequenceBarrier, _exceptionHandler, workHandlers);
        _consumerRepository->add(workerPool, sequenceBarrier);

        return std::make_shared<EventHandlerGroupType>(this->shared_from_this(), _consumerRepository, workerPool->getWorkerSequences());
    }

    std::shared_ptr<EventHandlerGroupType> createEventProcessors(const std::vector<std::shared_ptr<Sequence>> &barrierSequences,
            const std::vector<std::shared_ptr<IEventProcessorFactory<T>>>                                     &processorFactories) {
        std::vector<std::shared_ptr<IEventProcessor>> processors;
        for (auto &&processorFactory : processorFactories) {
            processors.push_back(processorFactory->createEventProcessor(_ringBuffer, barrierSequences));
        }

        return handleEventsWith(processors);
    }

private:
    /**
     * Private constructor helper
     */
    DisruptorCore(const std::shared_ptr<RingBuffer<T, SIZE, WAIT_STRATEGY, CLAIM_STRATEGY>> &ringBuffer, const std::shared_ptr<IExecutor> &executor)
        : _ringBuffer(ringBuffer)
        , _executor(executor) {
    }

    // Confirms if all messages have been consumed by all event processors
    bool hasBacklog() {
        auto cursor = _ringBuffer->cursor();
        for (auto &&sequence : _consumerRepository->getLastSequenceInChain(false)) {
            if (cursor > sequence->value())
                return true;
        }
        return false;
    }

    void checkNotStarted() const {
        if (_started == 1) {
            throw std::logic_error("All event handlers must be added before calling starts.");
        }
    }

    void checkOnlyStartedOnce() {
        if (std::atomic_exchange(&_started, 1) != 0) {
            throw std::logic_error("Disruptor.start() must only be called once.");
        }
    }
};

template<typename T, std::size_t SIZE, ProducerType producerType, typename Scheduler, typename WaitStrategy>
class Disruptor {
private:
    std::shared_ptr<Scheduler> _scheduler;
    //    std::shared_ptr<DisruptorCore<T, SIZE, WaitStrategy, producerType == ProducerType::Single ? SingleThreadedStrategy<SIZE, WaitStrategy> : MultiThreadedStrategy<SIZE, WaitStrategy>>> _disruptorCore
    std::shared_ptr<DisruptorCore<T, SIZE, WaitStrategy, MultiThreadedStrategy>> _disruptorCore;

public:
    using EventType = T;

    Disruptor(std::size_t threadsCount) {
        _scheduler = std::make_shared<Scheduler>();
        _scheduler->start(threadsCount);

        _disruptorCore = std::make_shared<DisruptorCore<EventType, SIZE, WaitStrategy, MultiThreadedStrategy>>(_scheduler);
    }

    auto operator->() const {
        return _disruptorCore.operator->();
    }

    ~Disruptor() {
        std::cerr << "Shutting down disruptor...\n";
        _disruptorCore->shutdown();
        std::cerr << "Stopping scheduler...\n";
        _scheduler->stop();
    }
};

} // namespace opencmw::disruptor
