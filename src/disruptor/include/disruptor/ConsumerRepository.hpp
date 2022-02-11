#pragma once

#include <memory>
#include <unordered_map>

#include "EventProcessorInfo.hpp"
#include "IConsumerInfo.hpp"
#include "IEventHandler.hpp"
#include "IEventProcessor.hpp"
#include "ISequence.hpp"
#include "ISequenceBarrier.hpp"
#include "WorkerPool.hpp"
#include "WorkerPoolInfo.hpp"

namespace opencmw::disruptor {

template<typename T>
class ConsumerRepository {
private:
    using ConsumerInfos = std::vector<std::shared_ptr<IConsumerInfo>>;
    std::unordered_map<std::shared_ptr<IEventHandler<T>>, std::shared_ptr<EventProcessorInfo<T>>> m_eventProcessorInfoByEventHandler;
    std::unordered_map<std::shared_ptr<ISequence>, std::shared_ptr<IConsumerInfo>>                m_eventProcessorInfoBySequence;
    ConsumerInfos                                                                                 m_consumerInfos;

public:
    void add(const std::shared_ptr<IEventProcessor> &eventProcessor,
            const std::shared_ptr<IEventHandler<T>> &eventHandler,
            const std::shared_ptr<ISequenceBarrier> &sequenceBarrier) {
        auto consumerInfo                                          = std::make_shared<EventProcessorInfo<T>>(eventProcessor, eventHandler, sequenceBarrier);
        m_eventProcessorInfoByEventHandler[eventHandler]           = consumerInfo;
        m_eventProcessorInfoBySequence[eventProcessor->sequence()] = consumerInfo;
        m_consumerInfos.push_back(consumerInfo);
    }

    void add(const std::shared_ptr<IEventProcessor> &processor) {
        auto consumerInfo                                     = std::make_shared<EventProcessorInfo<T>>(processor, nullptr, nullptr);
        m_eventProcessorInfoBySequence[processor->sequence()] = consumerInfo;
        m_consumerInfos.push_back(consumerInfo);
    }

    void add(const std::shared_ptr<WorkerPool<T>> &workerPool, const std::shared_ptr<ISequenceBarrier> &sequenceBarrier) {
        auto workerPoolInfo = std::make_shared<WorkerPoolInfo<T>>(workerPool, sequenceBarrier);
        m_consumerInfos.push_back(workerPoolInfo);
        for (auto &&sequence : workerPool->getWorkerSequences()) {
            m_eventProcessorInfoBySequence[sequence] = workerPoolInfo;
        }
    }

    std::vector<std::shared_ptr<ISequence>> getLastSequenceInChain(bool includeStopped) {
        std::vector<std::shared_ptr<ISequence>> lastSequence;
        for (auto &&consumerInfo : m_consumerInfos) {
            if ((includeStopped || consumerInfo->isRunning()) && consumerInfo->isEndOfChain()) {
                auto sequences = consumerInfo->sequences();
                std::copy(sequences.begin(), sequences.end(), std::back_inserter(lastSequence));
            }
        }

        return lastSequence;
    }

    std::shared_ptr<IEventProcessor> getEventProcessorFor(const std::shared_ptr<IEventHandler<T>> &eventHandler) {
        auto it = m_eventProcessorInfoByEventHandler.find(eventHandler);
        if (it == m_eventProcessorInfoByEventHandler.end() || it->second == nullptr) {
            throw std::invalid_argument(fmt::format("The event handler {} is not processing events.", eventHandler));
        }

        auto &&eventProcessorInfo = it->second;
        return eventProcessorInfo->eventProcessor();
    }

    std::shared_ptr<ISequence> getSequenceFor(const std::shared_ptr<IEventHandler<T>> &eventHandler) {
        return getEventProcessorFor(eventHandler)->sequence();
    }

    void unMarkEventProcessorsAsEndOfChain(const std::vector<std::shared_ptr<ISequence>> &barrierEventProcessors) {
        for (auto &&barrierEventProcessor : barrierEventProcessors) {
            auto it = m_eventProcessorInfoBySequence.find(barrierEventProcessor);
            if (it != m_eventProcessorInfoBySequence.end()) {
                it->second->markAsUsedInBarrier();
            }
        }
    }

    std::shared_ptr<ISequenceBarrier> getBarrierFor(const std::shared_ptr<IEventHandler<T>> &eventHandler) {
        auto it = m_eventProcessorInfoByEventHandler.find(eventHandler);
        if (it == m_eventProcessorInfoByEventHandler.end())
            return nullptr;

        return it->second->barrier();
    }

    ConsumerInfos::iterator begin() {
        return m_consumerInfos.begin();
    }

    ConsumerInfos::iterator end() {
        return m_consumerInfos.end();
    }

    ConsumerInfos::const_iterator begin() const {
        return m_consumerInfos.begin();
    }

    ConsumerInfos::const_iterator end() const {
        return m_consumerInfos.end();
    }
};

} // namespace opencmw::disruptor
