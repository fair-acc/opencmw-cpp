#pragma once

#include <memory>

#include "FatalExceptionHandler.hpp"
#include "IExceptionHandler.hpp"

namespace opencmw::disruptor {

template<typename T>
class ExceptionHandlerWrapper : public IExceptionHandler<T> {
private:
    std::shared_ptr<IExceptionHandler<T>> m_handler = std::make_shared<FatalExceptionHandler<T>>();

public:
    void switchTo(const std::shared_ptr<IExceptionHandler<T>> &exceptionHandler) {
        m_handler = exceptionHandler;
    }

    void handleEventException(const std::exception &ex, std::int64_t sequence, T &evt) override {
        m_handler->handleEventException(ex, sequence, evt);
    }

    void handleOnStartException(const std::exception &ex) override {
        m_handler->handleOnStartException(ex);
    }

    void handleOnShutdownException(const std::exception &ex) override {
        m_handler->handleOnShutdownException(ex);
    }

    void handleOnTimeoutException(const std::exception &ex, std::int64_t sequence) override {
        m_handler->handleOnTimeoutException(ex, sequence);
    }
};

} // namespace opencmw::disruptor
