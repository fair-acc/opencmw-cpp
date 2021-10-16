#ifndef YAZ_SOCKET_GROUP_H
#define YAZ_SOCKET_GROUP_H

#include <concepts>
#include <tuple>

#include <zmq.h>

#include "Context.hpp"
#include "Poller.hpp"
#include "Socket.hpp"
#include "SocketCommon_p.hpp"

#include "Debug.hpp"

namespace yaz {

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
    static_assert(std::is_same_v<Handler, meta::regular_void> || SocketGroupHandler<Handler, Socket>);
    using this_t = SocketGroup<Handler, Sockets...>;

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

    template<typename T>
    [[nodiscard]] const auto &get() const {
        return std::get<T>(_sockets);
    }

    template<typename T>
    [[nodiscard]] auto &get() {
        return std::get<T>(_sockets);
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

private:
    std::tuple<Sockets<this_t *>...> _sockets;
    Handler                          _handler;
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

