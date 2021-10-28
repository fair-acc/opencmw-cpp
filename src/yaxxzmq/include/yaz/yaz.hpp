#ifndef YAZ_H
#define YAZ_H

#include <array>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <exception>
#include <functional>
#include <optional>
#include <string_view>
#include <tuple>

#include <zmq.h>

#include "Debug.hpp"
#include "Meta.hpp"
#include "Result.hpp"

#include "kill/Message.hpp"

namespace yaz {

constexpr std::size_t sized_string_max_capacity = 255;
//
// Class that holds a small string that is O(1) convertible
// to both 0mq and C strings
//
template<std::size_t Capacity = sized_string_max_capacity>
class SizedString {
private:
    static_assert(Capacity > 0 and Capacity <= sized_string_max_capacity, "Capacity needs to be larger than 0 and less than 255");

    // Capacity denotes the largest string that this SizedString
    // can contain. To be compatible with 0mq, _data[0] needs
    // to hold the string length, while to be compatible with C,
    // it needs to be '\0'-terminated
    std::array<unsigned char, Capacity + 2> _data;

public:
    constexpr static const auto capacity = Capacity;

    constexpr SizedString() {
        _data[0] = 0;    // 0mq size is 0
        _data[1] = '\0'; // terminating a C string
    }

    constexpr explicit SizedString(const char *data, std::size_t size) {
        const auto clipped_length = std::min(Capacity, size);
        _data[0]                  = clipped_length;
        std::copy(data, data + clipped_length, &_data[1]);
        _data[clipped_length + 1] = '\0';
    }

    constexpr explicit SizedString(std::string_view other)
        : SizedString(other.data(), other.length()) {
    }

    [[nodiscard]] constexpr explicit operator std::string_view() const {
        return std::string_view(c_str(), _data[0]);
    }

    [[nodiscard]] const char *c_str() const {
        return reinterpret_cast<const char *>(&_data[1]);
    }

    [[nodiscard]] const char *zmq_str() const {
        return reinterpret_cast<const char *>(_data.data());
    }
};

template<typename T>
concept sized_string_instance = meta::is_value_instantiation_of_v<::yaz::SizedString, T>;

// A simple RAII wrapper class for 0mq context
class Context {
private:
    template<typename Message, typename Handler>
    friend class Socket;

    // Ah, 0mq and void pointers
    using zmq_context_handle = void *;

    [[nodiscard]] nonnegative_or_errno<int> set_option(int option, int value) {
        return nonnegative_or_errno<int>{ zmq_ctx_set(_zcontext, option, value) };
    }

    [[nodiscard]] nonnegative_or_errno<int> option(int option) const {
        return nonnegative_or_errno<int>{ zmq_ctx_get(_zcontext, option) };
    }

    zmq_context_handle _zcontext;

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
};

// A simple wrapper class for 0mq polling
template<std::size_t ItemCount>
class Poller {
private:
    std::array<zmq_pollitem_t, ItemCount> _items{};

public:
    explicit constexpr Poller() = default;

    constexpr auto &operator[](std::size_t index) {
        return _items[index];
    }

    constexpr const auto &operator[](std::size_t index) const {
        return _items[index];
    }

    [[nodiscard]] nonnegative_or_errno<int> poll() {
        return nonnegative_or_errno<int>{ zmq_poll(_items.data(), ItemCount, 0) };
    }
};

// We want to support several types of handlers
// - values or pointers to handlers
// - handlers that have the call operator (for lambdas),
//   and handlers that have a handle_message member function
// - handlers that accept a socket plus a message, or just a
//   message
template<typename Sender, typename Handler, typename Message>
void pass_to_message_handler(Sender &sender, Handler &&handler, Message &&message) {
    auto                *handler_ptr = meta::to_pointer(YAZ_FWD(handler));

    constexpr const bool has_full_handle_message_v
            = requires {
        handler_ptr->handle_message(sender, YAZ_FWD(message));
    };
    constexpr const bool has_simple_handle_message_v
            = requires {
        handler_ptr->handle_message(YAZ_FWD(message));
    };

    constexpr const bool has_full_call_operator_v = requires {
        std::invoke(*handler_ptr, sender, YAZ_FWD(message));
    };
    constexpr const bool has_simple_call_operator_v
            = requires {
        std::invoke(*handler_ptr, YAZ_FWD(message));
    };

    if constexpr (has_full_handle_message_v) {
        handler_ptr->handle_message(sender, YAZ_FWD(message));
    }
    else if constexpr (has_simple_handle_message_v) {
        handler_ptr->handle_message(YAZ_FWD(message));
    }
    else if constexpr (has_full_call_operator_v) {
        std::invoke(*handler_ptr, sender, YAZ_FWD(message));
    }
    else if constexpr (has_simple_call_operator_v) {
        std::invoke(*handler_ptr, YAZ_FWD(message));
    }
    else {
        meta::error_print_types<decltype(*handler_ptr)>{};
        static_assert(meta::always_false<decltype(handler_ptr)>, "Handler does not have handle_message or operator()");
    }
}

// `Socket` is a wrapper on top of 0mq sockets which provides a saner C++ API.
//
// The `Handler` template parameter can be either a callable object (lambda for example),
// or an object that contains a `process_message` member function. The signatures
// of said functions can be (socket, message) -> void, or (message) -> void.
//
// A handler can also be a pointer to an object with above qualities. This is useful
// in the case when you want to create a `Socket` member variable inside of an object
// that will be used as `Socket`'s handler.
template<typename Message, typename Handler = meta::regular_void>
class Socket {
private:
    // Ah, 0mq and void stars
    using zmq_socket_ptr = void *;

    [[no_unique_address]] Handler _handler;
    zmq_socket_ptr                _zsocket;
    intptr_t                      _fd;

public:
    explicit Socket(
            Context  &context,
            int       type,
            Handler &&handler)
        : _handler(std::move(handler)), _zsocket(zmq_socket(context._zcontext, type)), _fd(file_descriptor()) {
    }
    ~Socket() {
        disconnect();
    }

    Socket(const Socket &other) = delete;
    Socket &operator=(const Socket &other) = delete;

    Socket(Socket &&other) noexcept
        : _handler(std::move(other._handler)), _zsocket(nullptr), _fd(-1) {
        std::swap(_zsocket, other._zsocket);
        std::swap(_fd, other._fd);
    }

    Socket &operator=(Socket &&other) noexcept {
        auto temp = std::move(other);
        swap(other);
        return *this;
    }

    void swap(Socket &other) {
        using std::swap;
        swap(_handler, other._handler);
        swap(_zsocket, other._zsocket);
        swap(_fd, other._fd);
    }

    bool connect(std::string_view address,
            std::string_view      subscription = "") {
        if (zmq_connect(_zsocket, address.data()) != 0) {
            return false;
        }
        return subscription.empty() || static_cast<bool>(set_option<ZMQ_SUBSCRIBE>(subscription.data()));
    }

    bool disconnect() {
        if (_zsocket != nullptr && zmq_close(_zsocket) == 0) {
            _zsocket = nullptr;
            return true;
        }

        return false;
    }

    bool bind(std::string_view address) {
        return zmq_bind(_zsocket, address.data()) == 0;
    }

    // TODO these are hacks to prepend frames and send subscribe/unsubscribe messages
    // that don't fit into the Message pattern
    void send_more(std::string_view data) {
        MessagePart part(std::move(data), MessagePart::dynamic_bytes_tag{});
        const auto  result = part.send(_zsocket, ZMQ_DONTWAIT | ZMQ_SNDMORE);
        assert(result);
    }

    void send(std::string_view data) {
        MessagePart part(std::move(data), MessagePart::dynamic_bytes_tag{});
        const auto  result = part.send(_zsocket, ZMQ_DONTWAIT);
        assert(result);
    }

    void send(Message &&message) {
        auto parts_count = message.parts_count();
        for (std::size_t part_index = 0; part_index < parts_count; part_index++) {
            const auto flags  = part_index + 1 == parts_count ? ZMQ_DONTWAIT
                                                              : ZMQ_DONTWAIT | ZMQ_SNDMORE;
            const auto result = message[part_index].send(_zsocket, flags);
            assert(result);
        }
    }

    std::optional<Message> receive() {
        if (_zsocket == nullptr) {
            std::terminate();
        }

        std::vector<MessagePart> parts;

        while (true) {
            MessagePart part;
            const auto  byte_count_result = part.receive(_zsocket, ZMQ_DONTWAIT);

            if (byte_count_result) {
                parts.emplace_back(std::move(part));
            } else {
                break;
            }

            if (receive_more()) {
                // multipart message

            } else {
                break;
            }
        }

        if (parts.empty()) {
            return {};
        }

        return { Message{ std::move(parts) } };
    }

    template<typename ReadHandler>
    void read(ReadHandler &&handler) {
        while (true) {
            auto message = receive();
            if (message.has_value()) {
                pass_to_message_handler(*this, handler, std::move(*message));
                return;
            }
        }
    }

    void read() {
        read(_handler);
    }

    auto set_xpub_verbose(bool value) {
        return set_option<ZMQ_XPUB_VERBOSE>(value);
    }

    auto set_hwm(int value) {
        // setHWM in java version sets both of these
        return set_option<ZMQ_SNDHWM>(value) && set_option<ZMQ_RCVHWM>(value);
    }

    auto set_heartbeat_ttl(int value) {
        return set_option<ZMQ_HEARTBEAT_TTL>(value);
    }

    auto set_heartbeat_timeout(int value) {
        return set_option<ZMQ_HEARTBEAT_TIMEOUT>(value);
    }

    auto set_heartbeat_ivl(int value) {
        return set_option<ZMQ_HEARTBEAT_IVL>(value);
    }

    auto set_linger(int value) {
        return set_option<ZMQ_LINGER>(value);
    }

protected:
    template<int flag, typename Type, typename Result = Type>
    inline Result option() const {
        // Pointers... pointers everywhere
        Type   result;
        size_t size    = sizeof(result);

        auto   success = nonnegative_or_errno<int>(zmq_getsockopt(_zsocket, flag, &result, &size));
        assert(success);

        return result;
    }

    template<int flag>
    inline nonnegative_or_errno<int> set_option(const Bytes &value) {
        return nonnegative_or_errno<int>{ zmq_setsockopt(_zsocket, flag,
                value.data(), value.size()) };
    }

    template<int flag>
    inline nonnegative_or_errno<int> set_option(int value) {
        return nonnegative_or_errno<int>{ zmq_setsockopt(_zsocket, flag,
                &value, sizeof(value)) };
    }

    [[nodiscard]] inline intptr_t file_descriptor() const {
        return option<ZMQ_FD, intptr_t>();
    }

    [[nodiscard]] inline bool receive_more() const {
        return option<ZMQ_RCVMORE, intptr_t, bool>();
    }

    [[nodiscard]] zmq_socket_ptr zsocket() const {
        return _zsocket;
    }

    template<typename, template<typename> typename...>
    friend class SocketGroup;
};

template<typename Message, typename Handler>
auto make_socket(Context &context, int type, Handler &&handler) {
    return Socket<Message, Handler>(context, type, YAZ_FWD(handler));
}

template<typename Handler, template<typename> typename Socket, typename HandlerValue = std::remove_cvref_t<Handler>>
concept SocketGroupHandler = requires(HandlerValue handler, Socket<HandlerValue> &socket) {
    { meta::to_pointer(handler)->continue_after_messages_read(true) } -> std::same_as<bool>;
    { meta::to_pointer(handler)->receive_message(socket, true) } -> std::same_as<bool>;
};

// Represents a group of sockets that are read from using a 0mq poller.
// `Sockets` are class templates parametrised on the `Handler` type.
// They will be instantiated by the `SocketGroup` constructor.
//
// The `Handler` is an object (or a pointer to an object) that contains
// the following member functions:
// - receive_message: (socket, bool) -> bool
// - continue_after_messages_read: (bool) -> bool
template<typename Handler, template<typename> typename... Sockets>
class SocketGroup {
private:
    using this_t = SocketGroup<Handler, Sockets...>;
    // static_assert(std::is_same_v<Handler, meta::regular_void> || (SocketGroupHandler<this_t *, Sockets> && ...));

    std::tuple<Sockets<this_t *>...> _sockets;
    Handler                          _handler;

public:
    explicit SocketGroup(Context &context, Handler &&handler)
        : _sockets(Sockets<this_t *>(context, this)...), _handler{ std::move(handler) } {}

    template<std::size_t Id>
    [[nodiscard]] const auto &get() const {
        return std::get<Id>(_sockets);
    }

    template<std::size_t Id>
    [[nodiscard]] auto &get() {
        return std::get<Id>(_sockets);
    }

    template<template<typename> typename Socket>
    [[nodiscard]] const auto &get() const {
        return std::get<Socket<this_t *>>(_sockets);
    }

    template<template<typename> typename Socket>
    [[nodiscard]] auto &get() {
        return std::get<Socket<this_t *>>(_sockets);
    }

    void read() requires SocketGroupHandler<Handler, Socket> {
        Poller<sizeof...(Sockets)> poller;
        auto                      *handler_ptr = meta::to_pointer(_handler);

        meta::for_each_indexed(_sockets,
                [&poller](std::size_t index, auto &socket) {
                    poller[index].socket = socket.zsocket();
                    poller[index].events = ZMQ_POLLIN;
                });

        while (true) {
            bool anything_received = false;
            meta::for_each(_sockets, [handler_ptr, &anything_received](auto &socket) {
                anything_received |= handler_ptr->receive_message(socket, false);
            });

            if (!handler_ptr->continue_after_messages_read(anything_received)) {
                break;
            }

            if (!poller.poll()) {
                break;
            }
        }
    }

    void handle_message(auto &socket, auto &&message) const {
        pass_to_message_handler(socket, _handler, message);
    }
};

template<template<typename> typename... Sockets>
auto make_socket_group(Context &context, auto &&handler) {
    return SocketGroup<std::remove_cv_t<decltype(handler)>, Sockets...>(context, YAZ_FWD(handler));
}

template<template<typename> typename... Sockets>
auto make_socket_group(Context &context) {
    return SocketGroup<meta::regular_void, Sockets...>(context, meta::regular_void{});
}

} // namespace yaz

#endif // include guard
