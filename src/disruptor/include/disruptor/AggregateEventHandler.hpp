#pragma once

#include <memory>
#include <vector>

#include "IEventHandler.hpp"
#include "ILifecycleAware.hpp"

namespace opencmw::disruptor {

/**
 * An aggregate collection of IEventHandler<T> that get called in sequence for each event.
 * \tparam T event implementation storing the data for sharing during exchange or parallel coordination of an event
 */
template<typename T>
class AggregateEventHandler : public IEventHandler<T>, public ILifecycleAware {
private:
    std::vector<std::shared_ptr<IEventHandler<T>>> _eventHandlers;

public:
    /**
     * Construct an aggregate collection of IEventHandler<T>/> to be called in sequence.
     *
     * \param eventHandlers to be called in sequence
     */
    explicit AggregateEventHandler(const std::vector<std::shared_ptr<IEventHandler<T>>> &eventHandlers = {})
        : _eventHandlers(eventHandlers) {
    }

    /**
     * Called when a publisher has committed an event to the RingBuffer<T>
     *
     * \param data Data committed to the RingBuffer<T>/>
     * \param sequence Sequence number committed to theRingBuffer<T>/>
     * \param endOfBatch flag to indicate if this is the last event in a batch from theRingBuffer<T>/>
     */
    void onEvent(T &data, std::int64_t sequence, bool endOfBatch) override {
        for (auto i = 0u; i < _eventHandlers.size(); ++i) {
            _eventHandlers[i]->onEvent(data, sequence, endOfBatch);
        }
    }

    /**
     * Called once on thread start before first event is available.
     */
    void onStart() override {
        for (auto &&eventHandler : _eventHandlers) {
            auto handler = std::dynamic_pointer_cast<ILifecycleAware>(eventHandler);
            if (handler != nullptr)
                handler->onStart();
        }
    }

    /**
     * Called once just before the thread is shutdown.
     */
    void onShutdown() override {
        for (auto &&eventHandler : _eventHandlers) {
            auto handler = std::dynamic_pointer_cast<ILifecycleAware>(eventHandler);
            if (handler != nullptr)
                handler->onShutdown();
        }
    }
};

} // namespace opencmw::disruptor
