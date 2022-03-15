#pragma once

#include <memory>

#include "IConsumerInfo.hpp"
#include "IEventHandler.hpp"
#include "IEventProcessor.hpp"

namespace opencmw::disruptor {

template<typename T>
class EventProcessorInfo : public IConsumerInfo {
private:
    std::shared_ptr<IEventProcessor>  _eventProcessor;
    std::shared_ptr<IEventHandler<T>> _eventHandler;
    std::shared_ptr<ISequenceBarrier> _barrier;
    bool                              _isEndOfChain = true;

public:
    EventProcessorInfo(const std::shared_ptr<IEventProcessor> &eventProcessor,
            const std::shared_ptr<IEventHandler<T>>           &eventHandler,
            const std::shared_ptr<ISequenceBarrier>           &barrier)
        : _eventProcessor(eventProcessor)
        , _eventHandler(eventHandler)
        , _barrier(barrier) {}

    [[nodiscard]] const std::shared_ptr<IEventProcessor> &eventProcessor() const {
        return _eventProcessor;
    }

    std::vector<std::shared_ptr<Sequence>> sequences() const override {
        return { _eventProcessor->sequence() };
    }

    std::shared_ptr<IEventHandler<T>> handler() const {
        return _eventHandler;
    }

    const std::shared_ptr<ISequenceBarrier> &barrier() const override {
        return _barrier;
    }

    bool isEndOfChain() const override {
        return _isEndOfChain;
    }

    void start(const std::shared_ptr<IExecutor> &executor) override {
        executor->execute([this] { _eventProcessor->run(); });
    }

    void halt() override {
        _eventProcessor->halt();
    }

    void markAsUsedInBarrier() override {
        _isEndOfChain = false;
    }

    bool isRunning() const override {
        return _eventProcessor->isRunning();
    }
};

} // namespace opencmw::disruptor
