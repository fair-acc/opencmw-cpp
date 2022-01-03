#ifndef MAJORDOMO_TESTS_HELPERS_H
#define MAJORDOMO_TESTS_HELPERS_H

#include <majordomo/Message.hpp>
#include <majordomo/Settings.hpp>
#include <majordomo/Utils.hpp>
#include <majordomo/ZmqPtr.hpp>

#include <URI.hpp>

#include <catch2/catch.hpp>

#include <chrono>

inline opencmw::majordomo::Settings testSettings() {
    opencmw::majordomo::Settings settings;
    settings.heartbeatInterval = std::chrono::milliseconds(250);
    return settings;
}

template<typename MessageType>
class TestNode {
public:
    opencmw::majordomo::Socket _socket;

    explicit TestNode(const opencmw::majordomo::Context &context, int socket_type = ZMQ_DEALER)
        : _socket(context, socket_type) {
    }

    bool connect(const opencmw::URI<opencmw::STRICT> &address, std::string_view subscription = "") {
        auto result = zmq_invoke(zmq_connect, _socket, opencmw::majordomo::toZeroMQEndpoint(address).data());
        if (!result) return false;

        if (!subscription.empty()) {
            return subscribe(subscription);
        }

        return true;
    }

    bool subscribe(std::string_view subscription) {
        assert(!subscription.empty());
        return zmq_invoke(zmq_setsockopt, _socket, ZMQ_SUBSCRIBE, subscription.data(), subscription.size()).isValid();
    }

    bool unsubscribe(std::string_view subscription) {
        assert(!subscription.empty());
        return zmq_invoke(zmq_setsockopt, _socket, ZMQ_UNSUBSCRIBE, subscription.data(), subscription.size()).isValid();
    }

    bool sendRawFrame(std::string data) {
        opencmw::majordomo::MessageFrame f(data, opencmw::majordomo::MessageFrame::dynamic_bytes_tag{});
        return f.send(_socket, 0).isValid(); // blocking for simplicity
    }

    MessageType readOne() {
        auto maybeMessage = tryReadOne(std::chrono::seconds(3));
        REQUIRE(maybeMessage.has_value());
        return std::move(*maybeMessage);
    }

    std::optional<MessageType> tryReadOne(std::chrono::milliseconds timeout) {
        std::array<zmq_pollitem_t, 1> pollerItems;
        pollerItems[0].socket = _socket.zmq_ptr;
        pollerItems[0].events = ZMQ_POLLIN;

        const auto result     = opencmw::majordomo::zmq_invoke(zmq_poll, pollerItems.data(), static_cast<int>(pollerItems.size()), timeout.count());
        if (!result.isValid())
            return {};

        return MessageType::receive(_socket);
    }

    void send(opencmw::majordomo::MdpMessage &message) {
        message.send(_socket).assertSuccess();
    }
};

#endif
