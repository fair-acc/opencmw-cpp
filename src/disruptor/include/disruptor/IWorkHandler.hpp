#pragma once

namespace opencmw::disruptor {

/**
 * Callback interface to be implemented for processing units of work as they become available in the EventStore<T>
 *
 * \tparam T event implementation storing the data for sharing during exchange or parallel coordination of an event.
 */
template<typename T>
class IWorkHandler {
public:
    virtual ~IWorkHandler() = default;

    /**
     * Callback to indicate a unit of work needs to be processed.
     *
     * \param evt event published to the EventStore<T>
     */
    virtual void onEvent(T &evt) = 0;
};

} // namespace opencmw::disruptor
