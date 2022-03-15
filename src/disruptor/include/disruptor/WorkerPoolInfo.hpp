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
    bool                                 _isEndOfChain = true;

public:
    WorkerPoolInfo(const std::shared_ptr<WorkerPool<T, SIZE>> &workerPool, const std::shared_ptr<ISequenceBarrier> &barrier)
        : _workerPool(workerPool)
        , _barrier(barrier) {}

    [[nodiscard]] std::vector<std::shared_ptr<Sequence>>   sequences() const noexcept override { return _workerPool->getWorkerSequences(); }
    [[nodiscard]] const std::shared_ptr<ISequenceBarrier> &barrier() const noexcept override { return _barrier; }
    [[nodiscard]] bool                                     isEndOfChain() const noexcept override { return _isEndOfChain; }
    void                                                   start(const std::shared_ptr<IExecutor> &executor) noexcept override { _workerPool->start(executor); }
    void                                                   halt() override { _workerPool->halt(); }
    void                                                   markAsUsedInBarrier() noexcept override { _isEndOfChain = false; }
    [[nodiscard]] bool                                     isRunning() const noexcept override { return _workerPool->isRunning(); }
};

} // namespace opencmw::disruptor
