#pragma once

#include <iostream>
#include <sstream>

#include "Exception.hpp"
#include "IExceptionHandler.hpp"
#include "TypeInfo.hpp"

namespace opencmw::disruptor {

/**
 * Convenience implementation of an exception handler that using standard Console.Writeline to log the exception re-throw it wrapped in a ApplicationException
 */
template<typename T>
class FatalExceptionHandler : public IExceptionHandler<T> {
public:
    /**
     * Strategy for handling uncaught exceptions when processing an event.
     *
     * \param ex exception that propagated from the IEventHandler<T>
     * \param sequence sequence of the event which cause the exception.
     * \param evt event being processed when the exception occurred.
     */
    void handleEventException(const std::exception &ex, std::int64_t sequence, T & /*evt*/) override {
        auto message = fmt::format("Exception processing sequence {} for event {}: {}", sequence, Utils::getMetaTypeInfo<T>().fullyQualifiedName(), ex.what());
        std::cerr << message;
        throw WrappedException(ex, message);
    }

    /**
     * Callback to notify of an exception during ILifecycleAware.onStart
     *
     * \param ex ex throw during the starting process.
     */
    void handleOnStartException(const std::exception &ex) override {
        auto message = fmt::format("Exception during OnStart(): {}", ex.what());
        std::cerr << message;
        throw WrappedException(ex, message);
    }

    /**
     * Callback to notify of an exception during ILifecycleAware.onShutdown
     *
     * \param ex ex throw during the shutdown process.
     */
    void handleOnShutdownException(const std::exception &ex) override {
        auto message = fmt::format("Exception during OnShutdown(): {}", ex.what());
        std::cerr << message;
        throw WrappedException(ex, message);
    }

    /**
     * Callback to notify of an exception during ITimeoutHandler.onTimeout
     *
     * \param ex ex throw during the starting process.
     * \param sequence sequence of the event which cause the exception.
     */
    void handleOnTimeoutException(const std::exception &ex, std::int64_t sequence) override {
        auto message = fmt::format("Exception during OnTimeout() processing sequence {} for event {}: {}", sequence, Utils::getMetaTypeInfo<T>().fullyQualifiedName(), ex.what());
        std::cerr << message;
        throw WrappedException(ex, message);
    }
};

} // namespace opencmw::disruptor
