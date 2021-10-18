#ifndef YAZ_SOCKET_H
#define YAZ_SOCKET_H

#include <cassert>
#include <functional>
#include <string_view>

#include "Context.hpp"
#include "Message.hpp"
#include "MessagePart_p.hpp"
#include "SocketCommon_p.hpp"

namespace yaz {

class SocketPrivate;

enum class SocketType {
    Request,
    Reply,
    Dealer,
    Router,
    Publish,
    Subscribe,
    XPublish,
    XSubscribe,
    Push,
    Pull,
    Stream,
    Pair
};

// `Socket` is a wrapper on top of 0mq sockets which provides a saner C++ API.
//
// The `Handler` template parameter can be either a callable object (lambda for example),
// or an object that contains a `process_message` member function. The signatures
// of said functions can be (socket, message) -> void, or (message) -> void.
//
// A handler can also be a pointer to an object with above qualities. This is useful
// in the case when you want to create a `Socket` member variable inside of an object
// that will be used as `Socket`'s handler.
template<typename Handler = meta::regular_void>
class Socket {
    // Ah, 0mq and void stars
    using zmq_socket_ptr = void *;

public:
    explicit Socket(
            Context &  context,
            SocketType type,
            Handler && handler)
        : _handler(std::move(handler)), _zsocket(zmq_socket(context._zcontext, to_zmq_type(type))), _fd(file_descriptor()) {
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

    void send(const Message &message) {
        using detail::MessagePart;
        for (std::size_t message_id = 0; message_id < message.parts_count(); ++message_id) {
            MessagePart msg(message[message_id]);

            const auto  flags  = message_id + 1 == message.parts_count() ? ZMQ_DONTWAIT
                                                                         : ZMQ_DONTWAIT | ZMQ_SNDMORE;
            auto        result = msg.send(_zsocket, flags);

            if (!result) {
                assert(false && "sending failed");
            }
        }
    }

    void connect(std::string_view address,
            std::string_view      subscription = "") {
        zmq_connect(_zsocket, address.data());
        set_option<ZMQ_SUBSCRIBE>(subscription.data());
    }

    void disconnect() {
        if (_zsocket != nullptr) {
            zmq_close(_zsocket);
            _zsocket = nullptr;
        }
    }

    void bind(std::string_view address) {
        zmq_bind(_zsocket, address.data());
    }

    template<typename ReadHandler>
    void read(ReadHandler &&handler) {
        if (_zsocket == nullptr) {
            std::terminate();
            return;
        }

        Message message;

        using detail::MessagePart;
        while (true) {
            MessagePart part;
            const auto  byte_count_result = part.receive(_zsocket, ZMQ_NOBLOCK);

            if (!byte_count_result) {
                break;
            }

            message.add_part(part.data());

            if (receive_more()) {
                // multipart message

            } else {
                pass_to_message_handler(*this, handler, message);
                message.clear();
            }
        }
    }

    void read() {
        read(_handler);
    }

protected:
    constexpr int to_zmq_type(SocketType type) {
        // clang-format off
        return type == SocketType::Publish    ? ZMQ_PUB
             : type == SocketType::Subscribe  ? ZMQ_SUB
             : type == SocketType::XPublish   ? ZMQ_XPUB
             : type == SocketType::XSubscribe ? ZMQ_XSUB
             : type == SocketType::Request    ? ZMQ_REQ
             : type == SocketType::Reply      ? ZMQ_REP
             : type == SocketType::Dealer     ? ZMQ_DEALER
             : type == SocketType::Router     ? ZMQ_ROUTER
             : type == SocketType::Push       ? ZMQ_PUSH
             : type == SocketType::Pull       ? ZMQ_PULL
             : type == SocketType::Stream     ? ZMQ_STREAM
             : type == SocketType::Pair       ? ZMQ_PAIR
             : /* otherwise */                  -1;
        // clang-format on
    }

    template<int flag, typename Type, typename Result = Type>
    inline Result option() const {
        // Pointers... pointers everywhere
        Type   result;
        size_t size = sizeof(result);
        zmq_getsockopt(_zsocket, flag, &result, &size);
        return result;
    }

    template<int flag>
    inline void set_option(const Bytes &value) {
        zmq_setsockopt(_zsocket, flag,
                value.data(), value.size());
    }

    template<int flag>
    inline void set_option(int value) {
        zmq_setsockopt(_zsocket, flag,
                &value, sizeof(value));
    }

    [[nodiscard]] inline intptr_t file_descriptor() const {
        return option<ZMQ_FD, intptr_t>();
    }

    [[nodiscard]] inline bool receive_more() const {
        return option<ZMQ_RCVMORE, intptr_t, bool>();
    }

    [[nodiscard]] zmq_socket_ptr zsocket() const { return _zsocket; }

private:
    [[no_unique_address]] Handler _handler;
    zmq_socket_ptr                _zsocket;
    intptr_t                      _fd;

    template<typename, template<typename> typename...>
    friend class SocketGroup;
};

} // namespace yaz

#endif // include guard
