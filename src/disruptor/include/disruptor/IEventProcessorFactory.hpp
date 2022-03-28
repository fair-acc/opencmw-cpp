#pragma once

#include <memory>

#include "EventStore.hpp"
#include "RingBuffer.hpp"

namespace opencmw::disruptor {

/**
 * A factory interface to make it possible to include custom event processors in a chain
 */
template<typename T>
class IEventProcessorFactory {
public:
    virtual ~IEventProcessorFactory() = default;

    /**
     * Create a new event processor that gates on barrierSequences
     *
     * \param ringBuffer ring buffer
     * \param barrierSequences barrierSequences the sequences to gate on
     * \returns a new EventProcessor that gates on before processing events
     */
    virtual std::shared_ptr<IEventProcessor> createEventProcessor(const std::shared_ptr<EventStore<T>> &ringBuffer, const std::vector<std::shared_ptr<Sequence>> &barrierSequences) = 0;
};

} // namespace opencmw::disruptor
