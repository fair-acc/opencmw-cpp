#ifndef OPENCMW_MAJORDOMO_MOCKSERVER_H
#define OPENCMW_MAJORDOMO_MOCKSERVER_H

#include <charconv>
#include <string>
#include <string_view>
#include <unordered_map>

#include <majordomo/Broker.hpp>
#include <majordomo/Settings.hpp>
#include <majordomo/Worker.hpp>

#include <URI.hpp>

namespace opencmw::majordomo {

/*
 * Very simple mock opencmw server to use for testing.
 * Does not do any processing on its own, all actions have to be queried explicitly by calling the respective handler.
 * This allows single threaded and reproducible testing of client logic.
 */
class MockServer {
    static int                    INSTANCE_COUNT;
    const int                     id;
    const zmq::Context           &_context;
    std::string                   _address;
    std::string                   _subAddress;
    std::optional<zmq::Socket>    _socket;
    std::optional<zmq::Socket>    _pubSocket;
    std::array<zmq_pollitem_t, 1> _pollerItems{};
    Settings                      _settings;

public:
    explicit MockServer(const zmq::Context &context)
        : id(INSTANCE_COUNT++), _context(context), _address{ std::format("inproc://MockServer{}", id) }, _subAddress{ std::format("inproc://MockServerSub{}", id) } {
        bind();
    }

    virtual ~MockServer() = default;

    const zmq::Context       &context() { return _context; };

    [[nodiscard]] std::string address() { return _address; };
    [[nodiscard]] std::string addressSub() { return _subAddress; };

    template<typename Callback>
    bool processRequest(Callback handler) {
        const auto result           = zmq::invoke(zmq_poll, _pollerItems.data(), static_cast<int>(_pollerItems.size()), _settings.heartbeatInterval.count());
        bool       anythingReceived = false;
        do {
            auto maybeMessage = zmq::receive<mdp::MessageFormat::WithoutSourceId>(_socket.value());
            if (!maybeMessage) { // empty message
                anythingReceived = false;
                break;
            }
            anythingReceived = true;
            auto &message    = maybeMessage.value();
            if (message.protocolName != mdp::clientProtocol) {
                throw std::logic_error("mock server received unexpected worker message");
            }
            auto reply = replyFromRequest(message);
            handler(message, reply);
            zmq::send(std::move(reply), _socket.value()).assertSuccess();
        } while (anythingReceived);
        // N.B. block until data arrived or for at most one heart-beat interval
        return result.isValid();
    }

    void bind() {
        _socket.emplace(_context, ZMQ_DEALER);
        if (const auto result = zmq::invoke(zmq_bind, *_socket, _address.data()); result.isValid()) {
            _pollerItems[0].socket = _socket->zmq_ptr;
            _pollerItems[0].events = ZMQ_POLLIN;
        } else {
            std::print("error: {}\n", zmq_strerror(result.error()));
            _socket.reset();
        }
        _pubSocket.emplace(_context, ZMQ_XPUB);
        if (auto result = zmq::invoke(zmq_bind, *_pubSocket, _subAddress.data()); !result.isValid()) {
            std::print("error: {}\n", zmq_strerror(result.error()));
            _pubSocket.reset();
        }
    }

    bool disconnect() {
        _socket.reset();
        _pubSocket.reset();
        return true;
    }

    void notify(std::string_view endpoint, std::string_view value) {
        static const auto                                   brokerName   = "";
        static const auto                                   serviceName  = "a.service";
        const auto                                          subscription = mdp::Topic::fromMdpTopic(URI<>(std::string(endpoint)));
        mdp::BasicMessage<mdp::MessageFormat::WithSourceId> notify;
        notify.protocolName    = mdp::clientProtocol;
        notify.command         = mdp::Command::Final;
        notify.serviceName     = serviceName;
        notify.topic           = subscription.toMdpTopic();
        notify.sourceId        = subscription.toZmqTopic();
        notify.clientRequestID = IoBuffer(brokerName);
        notify.data            = IoBuffer(value.data(), value.size());
        zmq::send(std::move(notify), _pubSocket.value()).assertSuccess();
    }

    static mdp::Message replyFromRequest(const mdp::Message &request) noexcept {
        mdp::Message reply;
        reply.protocolName    = request.protocolName;
        reply.command         = mdp::Command::Final;
        reply.serviceName     = request.serviceName;
        reply.clientRequestID = request.clientRequestID;
        reply.topic           = request.topic;
        reply.rbac            = request.rbac;
        return reply;
    }
};

inline int MockServer::INSTANCE_COUNT = 0;

} // namespace opencmw::majordomo

#endif
