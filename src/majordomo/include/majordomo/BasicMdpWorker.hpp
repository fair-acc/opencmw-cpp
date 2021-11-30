#ifndef OPENCMW_MAJORDOMO_WORKER_H
#define OPENCMW_MAJORDOMO_WORKER_H

#include "Message.hpp"

#include <majordomo/Debug.hpp>
#include <majordomo/Settings.hpp>

#include <MIME.hpp>

#include <fmt/format.h>

#include <array>
#include <atomic>
#include <chrono>
#include <concepts>
#include <string>

namespace opencmw::majordomo {

struct RequestContext {
    MdpMessage                                   request;
    std::optional<MdpMessage>                    reply;
    opencmw::MIME::MimeType                      mimeType = opencmw::MIME::BINARY;
    std::unordered_map<std::string, std::string> htmlData;
};

template<typename T>
constexpr bool has_handleRequest_v = requires(T handler, RequestContext &context) { handler.handleRequest(context); };

template<typename T>
constexpr bool has_call_operator_v = requires(T handler, RequestContext &context) { std::invoke(handler, context); };

template<typename T>
concept HandlesRequest = has_handleRequest_v<T> || has_call_operator_v<T>;

template<HandlesRequest RequestHandler>
class BasicMdpWorker {
public:
    using Clock     = std::chrono::steady_clock;
    using Timestamp = std::chrono::time_point<Clock>;

    RequestHandler                _handler;
    const Settings                _settings;
    const std::string             _brokerAddress;
    const std::string             _serviceName;
    std::string                   _serviceDescription;
    std::string                   _rbacRole;
    std::atomic<bool>             _shutdownRequested = false;
    int                           _liveness          = 0;
    Timestamp                     _heartbeatAt;

    const Context &               _context;
    std::optional<Socket>         _socket;
    std::array<zmq_pollitem_t, 1> _pollerItems;

protected:
    MdpMessage createMessage(Command command) {
        auto message = MdpMessage::createWorkerMessage(command);
        message.setServiceName(_serviceName, MessageFrame::dynamic_bytes_tag{});
        message.setRbacToken(_rbacRole, MessageFrame::dynamic_bytes_tag{});
        return message;
    }

public:
    explicit BasicMdpWorker(Settings settings, const Context &context, std::string_view brokerAddress, std::string_view serviceName, RequestHandler &&handler)
        : _handler{ std::move(handler) }, _settings{ std::move(settings) }, _brokerAddress{ std::move(brokerAddress) }, _serviceName{ std::move(serviceName) }, _context(context) {
    }

    // Sets the service description
    void setServiceDescription(std::string description) {
        _serviceDescription = std::move(description);
    }

    void setRbacRole(std::string rbac) {
        _rbacRole = std::move(rbac);
    }

    void shutdown() {
        _shutdownRequested = true;
    }

    void disconnect() {
        auto msg = createMessage(Command::Disconnect);
        msg.send(*_socket).assertSuccess();
        _socket.reset();
    }

    void handleMessage(MdpMessage &&message) {
        _liveness = _settings.heartbeatLiveness;

        if (!message.isValid()) {
            debug() << "invalid MdpMessage received\n";
            return;
        }

        if (message.isWorkerMessage()) {
            switch (message.command()) {
            case Command::Get:
            case Command::Set:
                if (auto reply = processRequest(std::move(message))) {
                    reply->send(*_socket).assertSuccess();
                }
                return;
            case Command::Heartbeat:
                return;
            case Command::Disconnect:
                if (message.body() == "broker shutdown") {
                    _shutdownRequested = true;
                } else {
                    connectToBroker();
                }
                return;
            default:
                assert(!"not implemented");
                return;
            }
        } else {
            assert(!"not implemented");
        }
    }

    void run() {
        if (!connectToBroker())
            return;

        assert(_socket);

        const auto heartbeatIntervalMs = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(_settings.heartbeatInterval).count());

        do {
            while (auto message = MdpMessage::receive(*_socket)) {
                handleMessage(std::move(*message));
            }

            if (Clock::now() > _heartbeatAt && --_liveness == 0) {
                std::this_thread::sleep_for(_settings.workerReconnectInterval);
                if (!connectToBroker())
                    return;
            }
            assert(_socket);

            if (Clock::now() > _heartbeatAt) {
                auto heartbeat = createMessage(Command::Heartbeat);
                heartbeat.send(*_socket).assertSuccess();
                _heartbeatAt = Clock::now() + _settings.heartbeatInterval;
            }
        } while (!_shutdownRequested
                 && zmq_invoke(zmq_poll, _pollerItems.data(), static_cast<int>(_pollerItems.size()), heartbeatIntervalMs).isValid());
    }

private:
    MdpMessage replyFromRequest(const MdpMessage &request) {
        MdpMessage reply;
        reply.setProtocol(request.protocol());
        reply.setCommand(Command::Final);
        reply.setServiceName(request.serviceName(), MessageFrame::dynamic_bytes_tag{});
        reply.setClientSourceId(request.clientSourceId(), MessageFrame::dynamic_bytes_tag{});
        reply.setClientRequestId(request.clientRequestId(), MessageFrame::dynamic_bytes_tag{});
        reply.setTopic(request.topic(), MessageFrame::dynamic_bytes_tag{});
        reply.setRbacToken(_rbacRole, MessageFrame::dynamic_bytes_tag{});
        return reply;
    }

    std::optional<MdpMessage> processRequest(MdpMessage &&request) {
        RequestContext context;
        context.request = std::move(request);
        context.reply   = replyFromRequest(context.request);

        try {
            if constexpr (has_handleRequest_v<RequestHandler>) {
                _handler.handleRequest(context);
            } else if constexpr (has_call_operator_v<RequestHandler>) {
                std::invoke(_handler, context);
            }
            return std::move(context.reply);
        } catch (const std::exception &e) {
            auto errorReply = replyFromRequest(context.request);
            errorReply.setError(fmt::format("Caught exception for service '{}'\nrequest message: {}\nexception: {}", _serviceName, context.request.body(), e.what()), MessageFrame::dynamic_bytes_tag{});
            return errorReply;
        } catch (...) {
            auto errorReply = replyFromRequest(context.request);
            errorReply.setError(fmt::format("Caught unexpected exception for service '{}'\nrequest message: {}", _serviceName, context.request.body()), MessageFrame::dynamic_bytes_tag{});
            return errorReply;
        }
    }

    bool connectToBroker() {
        _pollerItems[0].socket = nullptr;
        _socket.emplace(_context, ZMQ_DEALER);

        const auto connectResult = zmq_invoke(zmq_connect, *_socket, _brokerAddress.data());
        if (!connectResult) {
            return false;
        }

        auto ready = createMessage(Command::Ready);
        ready.setBody(_serviceDescription, MessageFrame::dynamic_bytes_tag{});
        ready.send(*_socket).assertSuccess();

        _pollerItems[0].socket = _socket->zmq_ptr;
        _pollerItems[0].events = ZMQ_POLLIN;

        _liveness              = _settings.heartbeatLiveness;
        _heartbeatAt           = Clock::now() + _settings.heartbeatInterval;

        return true;
    }
};
} // namespace opencmw::majordomo

#endif
