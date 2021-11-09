#ifndef OPENCMW_MAJORDOMO_ZMQPTR_H
#define OPENCMW_MAJORDOMO_ZMQPTR_H

// A few thin RAII-only wrappers for ZMQ structures

#include <cassert>
#include <source_location>
#include <string>
#include <type_traits>

#include <zmq.h>

#include "Debug.hpp"

#ifndef ENABLE_RESULT_CHECKS
#define ENABLE_RESULT_CHECKS 1
#endif

namespace opencmw::majordomo {

template<typename T>
class [[nodiscard]] Result { // TODO-Q rstein: could this be made constexpr? Need a bit of explanation...
private:
    /*const*/ T   _value;
    /*const*/ int _error = 0;

#if (ENABLE_RESULT_CHECKS)
    // This serves just to check whether we
    // verified that the result is correct or not
    mutable bool _ignoreError = false;
#endif
public:
    // Returns the value from the result
    T value() const {
        assert(isValid());
        return _value;
    }

    int error() const {
        assert(!isValid());
        return _error;
    }

    void ignoreResult([[maybe_unused]] const std::source_location location = std::source_location::current()) {
#if (ENABLE_RESULT_CHECKS)
        if (!isValid()) {
            debugWithLocation(location) << "Ignored error result";
        }
        _ignoreError = true;
#endif
    };
    void assertSuccess([[maybe_unused]] const std::source_location location = std::source_location::current()) {
#if (ENABLE_RESULT_CHECKS)
        if (!isValid()) {
            debugWithLocation(location) << "Assertion failed";
        }
        _ignoreError = true;
#endif
        assert(isValid());
    };

    bool isValid() const {
#if (ENABLE_RESULT_CHECKS)
        _ignoreError = true;
#endif
        return _value >= 0;
    }

    explicit operator bool() const { return isValid(); }

    explicit constexpr Result(const T value)
        : _value{ value } {
        if (!isValid()) {
            _error = errno;
        }
    }

    ~Result() {
#if (ENABLE_RESULT_CHECKS)
        assert(_ignoreError || isValid());
#endif
    }

    Result(const Result &other)
        : _value(other._value)
        , _error(other._error)
#if (ENABLE_RESULT_CHECKS)
        , _ignoreError(other._ignoreError)
#endif
    {
    }

    Result &operator=(Result other) {
        std::swap(_value, other._value);
        std::swap(_error, other._error);
#if (ENABLE_RESULT_CHECKS)
        other._ignoreError = true;
#endif
        return *this;
    }

    Result operator&&(const Result &other) const {
        return _value >= 0 ? other : *this;
    }
};

namespace detail {

template<typename T>
concept ZmqPtrWrapper = requires(T s) {
    s.zmq_ptr;
};

template<typename Arg, typename ArgValueType = std::remove_cvref_t<Arg>>
constexpr decltype(auto) passArgument(Arg &&arg) {
    if constexpr (ZmqPtrWrapper<ArgValueType>) {
        return arg.zmq_ptr;
    } else if constexpr (std::is_same_v<ArgValueType, std::string>) {
        return arg.data();
    } else if constexpr (std::is_same_v<ArgValueType, std::string_view>) {
        return arg.data();
    } else {
        return std::forward<Arg>(arg);
    }
}
} // namespace detail

template<typename Function, typename... Args>
[[nodiscard]] auto zmq_invoke(const Function &&f, Args &&...args) {
    static_assert((not std::is_same_v<std::remove_cvref_t<Args>, void *> && ...));
    auto result = f(detail::passArgument(std::forward<Args>(args))...);
    return Result{ result };
}

struct ZmqPtr {
    void *zmq_ptr;
    explicit ZmqPtr(void *_ptr)
        : zmq_ptr{ _ptr } { assert(zmq_ptr != nullptr); }
    ZmqPtr()         = delete;
    ZmqPtr(ZmqPtr &) = delete;
    ZmqPtr &operator=(const ZmqPtr &) = delete;
};

struct Context : ZmqPtr {
    Context()
        : ZmqPtr{ zmq_ctx_new() } {}
    ~Context() { zmq_ctx_term(zmq_ptr); }
};

struct Socket : ZmqPtr {
    Socket(const Context &context, const int type)
        : ZmqPtr(zmq_socket(context.zmq_ptr, type)) {
    }
    Socket() = delete;
    ~Socket() { zmq_close(zmq_ptr); }
};

} // namespace opencmw::majordomo

#endif
