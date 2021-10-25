#ifndef YAMAL_CLIENT_H
#define YAMAL_CLIENT_H

#include <cassert>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "../yaxxzmq/context.hpp"
#include "../yaxxzmq/sized_string.hpp"
#include "../yaxxzmq/socket.hpp"

namespace yamal {

namespace detail {
constexpr const yaz::sized_string<6> ClientMessageHeader{ "MDPC02" };
enum class MessageType : uint8_t {
    Request = 0x01,
    Partial = 0x02,
    Final   = 0x03,
};
} // namespace detail

class Client {
    using Socket = yaz::Socket<Client *>;

public:
    template<typename BrokerPath>
    Client(std::shared_ptr<yaz::Context> context, BrokerPath &&broker_path)
        : _context{ std::move(context) }
        , _broker_url{ YAZ_FWD(broker_path) } {
        assert(context);
    }

    bool connect() {
        assert(!_socket.has_value());

        _socket = Socket(*_context, yaz::SocketType::Dealer, this);
        return _socket->connect(_broker_url);
    }

    void disconnect() {
        assert(_socket.has_value());
        _socket.reset();
    }

    template<typename Data>
    void send(const yaz::sized_string_instance auto &service_name, Data &&data) {
        auto message = create_request_header(service_name);
        message.add_part(std::forward<Data>(data));
        _socket->send(message);
    }

    void handle_message(const yaz::Message &message) {
        std::cout << "Got a reply " << message << " \n";
    }

private:
    auto create_request_header(
            const yaz::sized_string_instance auto &service_name) {
        // See https://rfc.zeromq.org/spec/18/#mdpclient
        // Request consists of:
        yaz::Message message;
        // Frame 0: “MDPC02” (six bytes, representing MDP/Client v0.2)
        message.add_part(detail::ClientMessageHeader);
        // Frame 1: 0x01 (one byte, representing REQUEST)
        message.add_part({ static_cast<yaz::Byte>(detail::MessageType::Request) });
        // Frame 2: Service name (0mq sized string)
        message.add_part(service_name.zmq_str());

        // Frames 3+: Request body (opaque binary)
        // will be added by the caller
        return message;
    }

    std::shared_ptr<yaz::Context> _context;
    std::optional<Socket>         _socket;
    std::string                   _broker_url;
};

} // namespace yamal

#endif // YAMAL_CLIENT_H
