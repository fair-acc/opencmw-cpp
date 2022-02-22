#ifndef OPENCMW_MAJORDOMO_MOCKSERVER_H
#define OPENCMW_MAJORDOMO_MOCKSERVER_H

#include <charconv>
#include <string>
#include <string_view>
#include <unordered_map>

#include <majordomo/Broker.hpp>
#include <majordomo/Message.hpp>
#include <majordomo/Settings.hpp>
#include <majordomo/Worker.hpp>

#include <URI.hpp>

namespace opencmw::majordomo {

/*
 * Very simple mock opencmw server to use for testing. Offers a single int property named "a.service" which can be get/set/subscribed.
 */
class MockServer {
    static int                    INSTANCE_COUNT;
    const int                     id;
    const Context                &_context;
    std::string                   _address;
    std::string                   _subAddress;
    std::optional<Socket>         _socket;
    std::optional<Socket>         _pubSocket;
    std::array<zmq_pollitem_t, 1> _pollerItems{};
    Settings                      _settings;

public:
    explicit MockServer(const Context &context)
        : id(INSTANCE_COUNT++), _context(context), _address{ fmt::format("inproc://MockServer{}", id) }, _subAddress{ fmt::format("inproc://MockServerSub{}", id) } {
        bind();
    }

    virtual ~MockServer() = default;

    const Context            &context() { return _context; };

    [[nodiscard]] std::string address() { return _address; };
    [[nodiscard]] std::string addressSub() { return _subAddress; };

    template<typename Callback>
    bool processRequest(Callback handler) {
        const auto result           = zmq_invoke(zmq_poll, _pollerItems.data(), static_cast<int>(_pollerItems.size()), _settings.heartbeatInterval.count());
        bool       anythingReceived = false;
        int        loopCount        = 0;
        do {
            auto maybeMessage = MdpMessage::receive(_socket.value());
            if (!maybeMessage) { // empty message
                anythingReceived = false;
                break;
            }
            anythingReceived = true;
            auto &message    = maybeMessage.value();
            if (!message.isValid() || !message.isClientMessage()) {
                throw std::logic_error("mock server received invalid message");
            }
            auto reply = replyFromRequest(message);
            handler(message, reply);
            reply.send(_socket.value()).assertSuccess();

            loopCount++;
        } while (anythingReceived);
        // N.B. block until data arrived or for at most one heart-beat interval
        return result.isValid();
    }

    void bind() {
        _socket.emplace(_context, ZMQ_DEALER);
        if (const auto result = zmq_invoke(zmq_bind, *_socket, _address.data()); result.isValid()) {
            _pollerItems[0].socket = _socket->zmq_ptr;
            _pollerItems[0].events = ZMQ_POLLIN;
        } else {
            fmt::print("error: {}\n", zmq_strerror(result.error()));
            _socket.reset();
        }
        _pubSocket.emplace(_context, ZMQ_XPUB);
        if (auto result = zmq_invoke(zmq_bind, *_pubSocket, _subAddress.data()); !result.isValid()) {
            fmt::print("error: {}\n", zmq_strerror(result.error()));
            _pubSocket.reset();
        }
    }

    bool disconnect() {
        _socket.reset();
        _pubSocket.reset();
        return true;
    }

    void notify(std::string_view topic, std::string_view value) {
        auto       brokerName  = "";
        auto       serviceName = "a.service";
        const auto dynamic_tag = MessageFrame::dynamic_bytes_tag{};
        auto       notify      = BasicMdpMessage<MessageFormat::WithSourceId>::createClientMessage(Command::Final);
        notify.setServiceName(serviceName, dynamic_tag);
        notify.setTopic(topic, dynamic_tag);
        notify.setSourceId(topic, dynamic_tag);
        notify.setClientRequestId(brokerName, dynamic_tag);
        notify.setBody(value, dynamic_tag);
        notify.send(_pubSocket.value()).assertSuccess();
    }

    void notify(std::string_view topic, std::string_view uri, std::string_view value) {
        auto       brokerName  = "";
        auto       serviceName = "a.service";
        const auto dynamic_tag = MessageFrame::dynamic_bytes_tag{};
        auto       notify      = BasicMdpMessage<MessageFormat::WithSourceId>::createClientMessage(Command::Final);
        notify.setServiceName(serviceName, dynamic_tag);
        notify.setTopic(uri, dynamic_tag);
        notify.setSourceId(topic, dynamic_tag);
        notify.setClientRequestId(brokerName, dynamic_tag);
        notify.setBody(value, dynamic_tag);
        notify.send(_pubSocket.value()).assertSuccess();
    }

    static MdpMessage replyFromRequest(const MdpMessage &request) noexcept {
        MdpMessage reply;
        reply.setProtocol(request.protocol());
        reply.setCommand(Command::Final);
        reply.setServiceName(request.serviceName(), MessageFrame::dynamic_bytes_tag{});
        reply.setClientSourceId(request.clientSourceId(), MessageFrame::dynamic_bytes_tag{});
        reply.setClientRequestId(request.clientRequestId(), MessageFrame::dynamic_bytes_tag{});
        reply.setTopic(request.topic(), MessageFrame::dynamic_bytes_tag{});
        reply.setRbacToken(request.rbacToken(), MessageFrame::dynamic_bytes_tag{});
        return reply;
    }
};

inline int MockServer::INSTANCE_COUNT = 0;

} // namespace opencmw::majordomo

#endif
