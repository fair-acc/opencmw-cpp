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
    std::unordered_map<std::shared_ptr<IEventHandler<T>>, std::shared_ptr<EventProcessorInfo<T>>> _eventProcessorInfoByEventHandler;
    std::unordered_map<std::shared_ptr<ISequence>, std::shared_ptr<IConsumerInfo>>                _eventProcessorInfoBySequence;
    ConsumerInfos                                                                                 _consumerInfos;

public:
    void add(const std::shared_ptr<IEventProcessor> &eventProcessor,
            const std::shared_ptr<IEventHandler<T>> &eventHandler,
            const std::shared_ptr<ISequenceBarrier> &sequenceBarrier) {
        auto consumerInfo                                         = std::make_shared<EventProcessorInfo<T>>(eventProcessor, eventHandler, sequenceBarrier);
        _eventProcessorInfoByEventHandler[eventHandler]           = consumerInfo;
        _eventProcessorInfoBySequence[eventProcessor->sequence()] = consumerInfo;
        _consumerInfos.push_back(consumerInfo);
    }

    void add(const std::shared_ptr<IEventProcessor> &processor) {
        auto consumerInfo                                    = std::make_shared<EventProcessorInfo<T>>(processor, nullptr, nullptr);
        _eventProcessorInfoBySequence[processor->sequence()] = consumerInfo;
        _consumerInfos.push_back(consumerInfo);
    }

    template<std::size_t SIZE = 1024>
    void add(const std::shared_ptr<WorkerPool<T, SIZE>> &workerPool, const std::shared_ptr<ISequenceBarrier> &sequenceBarrier) {
        auto workerPoolInfo = std::make_shared<WorkerPoolInfo<T, SIZE>>(workerPool, sequenceBarrier);
        _consumerInfos.push_back(workerPoolInfo);
        for (auto &&sequence : workerPool->getWorkerSequences()) {
            _eventProcessorInfoBySequence[sequence] = workerPoolInfo;
        }
    }

    std::vector<std::shared_ptr<ISequence>> getLastSequenceInChain(bool includeStopped) {
        std::vector<std::shared_ptr<ISequence>> lastSequence;
        for (auto &&consumerInfo : _consumerInfos) {
            if ((includeStopped || consumerInfo->isRunning()) && consumerInfo->isEndOfChain()) {
                auto sequences = consumerInfo->sequences();
                std::copy(sequences.begin(), sequences.end(), std::back_inserter(lastSequence));
            }
        }

        return lastSequence;
    }

    std::shared_ptr<IEventProcessor> getEventProcessorFor(const std::shared_ptr<IEventHandler<T>> &eventHandler) {
        auto it = _eventProcessorInfoByEventHandler.find(eventHandler);
        if (it == _eventProcessorInfoByEventHandler.end() || it->second == nullptr) {
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
            auto it = _eventProcessorInfoBySequence.find(barrierEventProcessor);
            if (it != _eventProcessorInfoBySequence.end()) {
                it->second->markAsUsedInBarrier();
            }
        }
    }

    std::shared_ptr<ISequenceBarrier> getBarrierFor(const std::shared_ptr<IEventHandler<T>> &eventHandler) {
        auto it = _eventProcessorInfoByEventHandler.find(eventHandler);
        if (it == _eventProcessorInfoByEventHandler.end())
            return nullptr;

        return it->second->barrier();
    }

    ConsumerInfos::iterator begin() {
        return _consumerInfos.begin();
    }

    ConsumerInfos::iterator end() {
        return _consumerInfos.end();
    }

    ConsumerInfos::const_iterator begin() const {
        return _consumerInfos.begin();
    }

    ConsumerInfos::const_iterator end() const {
        return _consumerInfos.end();
    }
};

} // namespace opencmw::disruptor
