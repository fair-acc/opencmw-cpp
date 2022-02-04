#pragma once

#include <memory>

#include "IConsumerInfo.hpp"
#include "IEventHandler.hpp"
#include "IEventProcessor.hpp"

namespace opencmw::disruptor {

template<typename T>
class EventProcessorInfo : public IConsumerInfo {
private:
    std::shared_ptr<IEventProcessor>  m_eventProcessor;
    std::shared_ptr<IEventHandler<T>> m_eventHandler;
    std::shared_ptr<ISequenceBarrier> m_barrier;
    bool                              m_isEndOfChain = true;

public:
    EventProcessorInfo(const std::shared_ptr<IEventProcessor> &eventProcessor,
            const std::shared_ptr<IEventHandler<T>>           &eventHandler,
            const std::shared_ptr<ISequenceBarrier>           &barrier)
        : m_eventProcessor(eventProcessor)
        , m_eventHandler(eventHandler)
        , m_barrier(barrier)
        , m_isEndOfChain(true) {
    }

    const std::shared_ptr<IEventProcessor> &eventProcessor() const {
        return m_eventProcessor;
    }

    std::vector<std::shared_ptr<ISequence>> sequences() const override {
        return { m_eventProcessor->sequence() };
    }

    std::shared_ptr<IEventHandler<T>> handler() const {
        return m_eventHandler;
    }

    const std::shared_ptr<ISequenceBarrier> &barrier() const override {
        return m_barrier;
    }

    bool isEndOfChain() const override {
        return m_isEndOfChain;
    }

    void start(const std::shared_ptr<IExecutor> &executor) override {
        executor->execute([this] { m_eventProcessor->run(); });
    }

    void halt() override {
        m_eventProcessor->halt();
    }

    void markAsUsedInBarrier() override {
        m_isEndOfChain = false;
    }

    bool isRunning() const override {
        return m_eventProcessor->isRunning();
    }
};

} // namespace opencmw::disruptor
