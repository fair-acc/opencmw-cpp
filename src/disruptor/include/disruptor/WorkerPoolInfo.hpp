#pragma once

#include <memory>

#include "IConsumerInfo.hpp"
#include "WorkerPool.hpp"

namespace opencmw::disruptor {

template<typename T, std::size_t SIZE>
class WorkerPoolInfo : public IConsumerInfo {
private:
    std::shared_ptr<WorkerPool<T, SIZE>> _workerPool;
    std::shared_ptr<ISequenceBarrier>    _barrier;
    bool                                 _isEndOfChain;

public:
    WorkerPoolInfo(const std::shared_ptr<WorkerPool<T, SIZE>> &workerPool, const std::shared_ptr<ISequenceBarrier> &barrier)
        : _workerPool(workerPool)
        , _barrier(barrier)
        , _isEndOfChain(true) {
    }

    std::vector<std::shared_ptr<ISequence>> sequences() const override {
        return _workerPool->getWorkerSequences();
    }

    const std::shared_ptr<ISequenceBarrier> &barrier() const override {
        return _barrier;
    }

    bool isEndOfChain() const override {
        return _isEndOfChain;
    }

    void start(const std::shared_ptr<IExecutor> &executor) override {
        _workerPool->start(executor);
    }

    void halt() override {
        _workerPool->halt();
    }

    void markAsUsedInBarrier() override {
        _isEndOfChain = false;
    }

    bool isRunning() const override {
        return _workerPool->isRunning();
    }
};

} // namespace opencmw::disruptor
