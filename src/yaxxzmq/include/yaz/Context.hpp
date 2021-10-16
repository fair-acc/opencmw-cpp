#ifndef YAZ_CONTEXT_H
#define YAZ_CONTEXT_H

#include <exception>

#include <zmq.h>

#include "Result.hpp"

namespace yaz {

// A simple RAII wrapper class for 0mq context
class Context {
public:
    Context()
        : _zcontext{ zmq_ctx_new() } {
        if (_zcontext == nullptr) {
            std::terminate();
        }
    }

    ~Context() {
        if (_zcontext != nullptr) {
            zmq_ctx_term(_zcontext);
        }
    }

    Context(const Context &other) = delete;
    Context &operator=(const Context &other) = delete;

    Context(Context &&other) noexcept
        : _zcontext(nullptr) {
        std::swap(_zcontext, other._zcontext);
    }

    Context &operator=(Context &&other) noexcept {
        auto temp = std::move(other);
        swap(other);
        return *this;
    }

    void swap(Context &other) {
        std::swap(_zcontext, other._zcontext);
    }

private:
    template<typename Handler>
    friend class Socket;

    // Ah, 0mq and void pointers
    using zmq_context_handle = void *;

    [[nodiscard]] positive_or_errno<int> set_option(int option, int value) {
        return positive_or_errno<int>{ zmq_ctx_set(_zcontext, option, value) };
    }

    [[nodiscard]] positive_or_errno<int> option(int option) const {
        return positive_or_errno<int>{ zmq_ctx_get(_zcontext, option) };
    }

    zmq_context_handle _zcontext;
};

} // namespace yaz

#endif // include guard

