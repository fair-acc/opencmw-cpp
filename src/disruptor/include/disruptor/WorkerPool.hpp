#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>

#include "IExecutor.hpp"
#include "RingBuffer.hpp"
#include "Sequence.hpp"
#include "WaitStrategy.hpp"
#include "WorkProcessor.hpp"

namespace opencmw::disruptor {
/**
 * WorkerPool contains a pool of WorkProcessor<T> that will consume sequences so jobs can be farmed out across a pool of workers. Each of the WorkProcessor<T>
 * manage and calls a IWorkHandler<T> to process the events.
 *
 * \tparam T event to be processed by a pool of workers
 */
template<typename T, std::size_t SIZE = 1024>
class WorkerPool {
    std::atomic<std::int32_t>      _running{ 0 };
    std::shared_ptr<Sequence>      _workSequence = std::make_shared<Sequence>();
    std::shared_ptr<EventStore<T>> _ringBuffer;
    // WorkProcessors are created to wrap each of the provided WorkHandlers
    std::vector<std::shared_ptr<WorkProcessor<T>>> _workProcessors;

public:
    /**
     * Create a worker pool to enable an array of IWorkHandler<T> to consume published sequences. This option requires a pre-configured DataProvider<T> which must have
     * SequencerBase::setGatingSequences() called before the work pool is started.
     *
     * \param ringBuffer ringBuffer of events to be consumed.
     * \param sequenceBarrier sequenceBarrier on which the workers will depend.
     * \param exceptionHandler exceptionHandler to callback when an error occurs which is not handled by the<see cref="IWorkHandler{T}"/>s.
     * \param workHandlers workHandlers to distribute the work load across.
     */
    WorkerPool(const std::shared_ptr<EventStore<T>>             &ringBuffer,
            const std::shared_ptr<ISequenceBarrier>             &sequenceBarrier,
            const std::shared_ptr<IExceptionHandler<T>>         &exceptionHandler,
            const std::vector<std::shared_ptr<IWorkHandler<T>>> &workHandlers)
        : _ringBuffer(ringBuffer) {
        _workProcessors.resize(workHandlers.size());

        for (auto i = 0u; i < workHandlers.size(); ++i) {
            _workProcessors[i] = WorkProcessor<T>::create(ringBuffer, sequenceBarrier, workHandlers[i], exceptionHandler, _workSequence);
        }
    }

    /**
     * Construct a work pool with an internal DataProvider<T> for convenience. This option does not require SequencerBase::setGatingSequences() to be called before the work pool is started.
     *
     * \param eventFactory eventFactory for filling the<see cref="RingBuffer{T}"/>
     * \param exceptionHandler exceptionHandler to callback when an error occurs which is not handled by the<see cref="IWorkHandler{T}"/>s.
     * \param workHandlers workHandlers to distribute the work load across.
     */
    WorkerPool(const std::function<T()>                         &eventFactory,
            const std::shared_ptr<IExceptionHandler<T>>         &exceptionHandler,
            const std::vector<std::shared_ptr<IWorkHandler<T>>> &workHandlers)

    {
        _ringBuffer  = EventStore<T>::createMultiProducer(eventFactory, 1024, std::make_shared<BlockingWaitStrategy>());
        auto barrier = _ringBuffer->newBarrier();
        _workProcessors.resize(workHandlers.size());

        for (auto i = 0u; i < workHandlers.size(); ++i) {
            _workProcessors[i] = WorkProcessor<T>::create(_ringBuffer, barrier, workHandlers[i], exceptionHandler, _workSequence);
        }

        _ringBuffer->addGatingSequences(getWorkerSequences());
    }

    /**
     * Get an array of Sequences representing the progress of the workers.
     */
    std::vector<std::shared_ptr<Sequence>> getWorkerSequences() {
        std::vector<std::shared_ptr<Sequence>> sequences(_workProcessors.size() + 1);
        for (auto i = 0u; i < _workProcessors.size(); ++i) {
            sequences[i] = _workProcessors[i]->sequence();
        }
        sequences[sequences.size() - 1] = _workSequence;

        return sequences;
    }

    /**
     * Start the worker pool processing events in sequence.
     *
     * \returns the DataProvider<T> used for the work queue.
     */
    std::shared_ptr<EventStore<T>> start(const std::shared_ptr<IExecutor> &executor) {
        if (std::atomic_exchange(&_running, 1) != 0) {
            throw std::logic_error("WorkerPool has already been started and cannot be restarted until halted");
        }

        auto cursor = _ringBuffer->cursor();
        _workSequence->setValue(cursor);

        for (auto &&workProcessor : _workProcessors) {
            workProcessor->sequence()->setValue(cursor);
            executor->execute([workProcessor] { workProcessor->run(); });
        }

        return _ringBuffer;
    }

    /**
     * Wait for the DataProvider<T> to drain of published events then halt the workers.
     */
    void drainAndHalt() {
        auto workerSequences = getWorkerSequences();
        while (_ringBuffer->cursor() > detail::getMinimumSequence(workerSequences)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(0));
        }

        for (auto &&workProcessor : _workProcessors) {
            workProcessor->halt();
        }

        _running = 0;
    }

    /**
     * Halt all workers immediately at then end of their current cycle.
     */
    void halt() {
        for (auto &&workProcessor : _workProcessors) {
            workProcessor->halt();
        }

        _running = 0;
    }

    bool isRunning() const {
        return _running == 1;
    }
};

} // namespace opencmw::disruptor
