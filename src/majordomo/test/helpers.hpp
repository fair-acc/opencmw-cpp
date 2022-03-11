#ifndef MAJORDOMO_TESTS_HELPERS_H
#define MAJORDOMO_TESTS_HELPERS_H

#include <majordomo/Client.hpp>
#include <majordomo/Message.hpp>
#include <majordomo/Settings.hpp>
#include <majordomo/Utils.hpp>
#include <majordomo/Worker.hpp>
#include <majordomo/ZmqPtr.hpp>

#include <URI.hpp>

#include <chrono>
#include <thread>

template<typename T>
concept Shutdownable = requires(T s) {
    s.run();
    s.shutdown();
};

template<Shutdownable T>
struct RunInThread {
    T           &_toRun;
    std::jthread _thread;

    explicit RunInThread(T &toRun)
        : _toRun(toRun)
        , _thread([this] { _toRun.run(); }) {
    }

    ~RunInThread() {
        _toRun.shutdown();
        _thread.join();
    }
};

inline opencmw::majordomo::Settings testSettings() {
    opencmw::majordomo::Settings settings;
    settings.heartbeatInterval = std::chrono::milliseconds(100);
    settings.dnsTimeout        = std::chrono::milliseconds(250);
    return settings;
}

template<typename MessageType>
class TestNode {
public:
    opencmw::majordomo::Socket _socket;

    explicit TestNode(const opencmw::majordomo::Context &context, int socket_type = ZMQ_DEALER)
        : _socket(context, socket_type) {
    }

    bool bind(const opencmw::URI<opencmw::STRICT> &address) {
        return zmq_invoke(zmq_bind, _socket, opencmw::majordomo::toZeroMQEndpoint(address).data()).isValid();
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

    std::optional<MessageType> tryReadOne(std::chrono::milliseconds timeout = std::chrono::milliseconds(3000)) {
        std::array<zmq_pollitem_t, 1> pollerItems;
        pollerItems[0].socket = _socket.zmq_ptr;
        pollerItems[0].events = ZMQ_POLLIN;

        const auto result     = opencmw::majordomo::zmq_invoke(zmq_poll, pollerItems.data(), static_cast<int>(pollerItems.size()), timeout.count());
        if (!result.isValid())
            return {};

        return MessageType::receive(_socket);
    }

    void send(MessageType &message) {
        message.send(_socket).assertSuccess();
    }
};

inline bool waitUntilServiceAvailable(const opencmw::majordomo::Context &context, std::string_view serviceName, const opencmw::URI<opencmw::STRICT> &brokerAddress = opencmw::majordomo::INTERNAL_ADDRESS_BROKER) {
    TestNode<opencmw::majordomo::MdpMessage> client(context);
    if (!client.connect(brokerAddress)) {
        return false;
    }

    constexpr auto timeout   = std::chrono::seconds(3);
    const auto     startTime = std::chrono::system_clock::now();

    while (std::chrono::system_clock::now() - startTime < timeout) {
        auto request = opencmw::majordomo::MdpMessage::createClientMessage(opencmw::majordomo::Command::Get);
        request.setServiceName("mmi.service", opencmw::majordomo::MessageFrame::static_bytes_tag{});
        request.setBody(serviceName, opencmw::majordomo::MessageFrame::dynamic_bytes_tag{});
        client.send(request);

        auto reply = client.tryReadOne();
        if (!reply) { // no reply at all? something went seriously wrong
            return false;
        }

        if (reply->body() == "200") {
            return true;
        }
    }

    return false;
}

constexpr auto static_tag  = opencmw::majordomo::MessageFrame::static_bytes_tag{};
constexpr auto dynamic_tag = opencmw::majordomo::MessageFrame::dynamic_bytes_tag{};

class TestIntHandler {
    int _x = 10;

public:
    explicit TestIntHandler(int initialValue)
        : _x(initialValue) {
    }

    void operator()(opencmw::majordomo::RequestContext &context) {
        if (context.request.command() == opencmw::majordomo::Command::Get) {
            context.reply.setBody(std::to_string(_x), opencmw::majordomo::MessageFrame::dynamic_bytes_tag{});
            return;
        }

        assert(context.request.command() == opencmw::majordomo::Command::Set);

        const auto request = context.request.body();
        int        value   = 0;
        const auto result  = std::from_chars(request.begin(), request.end(), value);

        if (result.ec == std::errc::invalid_argument) {
            context.reply.setError("Not a valid int", opencmw::majordomo::MessageFrame::static_bytes_tag{});
        } else {
            _x = value;
            context.reply.setBody("Value set. All good!", opencmw::majordomo::MessageFrame::static_bytes_tag{});
        }
    }
};

class NonCopyableMovableHandler {
public:
    NonCopyableMovableHandler()                                      = default;
    NonCopyableMovableHandler(NonCopyableMovableHandler &&) noexcept = default;
    NonCopyableMovableHandler &operator=(NonCopyableMovableHandler &&) noexcept = default;

    void                       operator()(opencmw::majordomo::RequestContext &) {}
};

#endif
