#pragma once

#include <memory>

#include "FatalExceptionHandler.hpp"
#include "IExceptionHandler.hpp"

namespace opencmw::disruptor {

template<typename T>
class ExceptionHandlerWrapper : public IExceptionHandler<T> {
private:
    std::shared_ptr<IExceptionHandler<T>> _handler = std::make_shared<FatalExceptionHandler<T>>();

public:
    void switchTo(const std::shared_ptr<IExceptionHandler<T>> &exceptionHandler) {
        _handler = exceptionHandler;
    }

    void handleEventException(const std::exception &ex, std::int64_t sequence, T &evt) override {
        _handler->handleEventException(ex, sequence, evt);
    }

    void handleOnStartException(const std::exception &ex) override {
        _handler->handleOnStartException(ex);
    }

    void handleOnShutdownException(const std::exception &ex) override {
        _handler->handleOnShutdownException(ex);
    }

    void handleOnTimeoutException(const std::exception &ex, std::int64_t sequence) override {
        _handler->handleOnTimeoutException(ex, sequence);
    }
};

} // namespace opencmw::disruptor
