#pragma once

#include <cstdint>
#include <functional>
#include <memory>

namespace opencmw::disruptor {

/**
 * Callback interface to be implemented for processing events as they become available in the RingBuffer<T>
 *
 * \tparam T Type of events for sharing during exchange or parallel coordination of an event
 * \remark See BatchEventProcessor<T>.SetExceptionHandler if you want to handle exceptions propagated out of the handler.
 */
template<typename T>
class IEventHandler {
public:
    virtual ~IEventHandler() = default;

    /**
     * Called when a publisher has committed an event to the RingBuffer<T>
     *
     * \param data Data committed to the RingBuffer<T>
     * \param sequence Sequence number committed to the RingBuffer<T>
     * \param endOfBatch flag to indicate if this is the last event in a batch from the RingBuffer<T>
     */
    virtual void onEvent(T &data, std::int64_t sequence, bool endOfBatch) = 0;
};

// TODO: Make IEventHandler a simple std::function
template<typename T, typename Function>
class FunctionObjectEventHandler : public IEventHandler<T> {
public:
    FunctionObjectEventHandler(Function function)
        : _function(std::move(function)) {
    }

    virtual void onEvent(T &data, std::int64_t sequence, bool endOfBatch) override {
        _function(data, sequence, endOfBatch);
        // std::invoke(_function, data, sequence, endOfBatch);
    }

    Function       &function() { return _function; }
    const Function &function() const { return _function; }

private:
    Function _function;
};

template<typename T, typename Function>
auto makeEventHandler(Function &&function) {
    return std::make_shared<FunctionObjectEventHandler<T, Function>>(std::forward<Function>(function));
}

} // namespace opencmw::disruptor
