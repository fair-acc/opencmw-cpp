#ifndef OPENCMW_MAJORDOMO_WORKER_H
#define OPENCMW_MAJORDOMO_WORKER_H

#include "Broker.hpp"
#include "Message.hpp"

#include <majordomo/Broker.hpp>
#include <majordomo/Constants.hpp>
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
    const MdpMessage                             request;
    MdpMessage                                   reply;
    opencmw::MIME::MimeType                      mimeType = opencmw::MIME::BINARY;
    std::unordered_map<std::string, std::string> htmlData;
};

template<typename T>
concept HandlesRequest = requires(T handler, RequestContext &context) { std::invoke(handler, context); };

template<HandlesRequest RequestHandler>
class BasicMdpWorker {
private:
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

public:
    explicit BasicMdpWorker(std::string_view serviceName, std::string_view brokerAddress, RequestHandler &&handler, const Context &context = {}, Settings settings = {})
        : _handler{ std::forward<RequestHandler>(handler) }, _settings{ std::move(settings) }, _brokerAddress{ std::move(brokerAddress) }, _serviceName{ std::move(serviceName) }, _context(context) {
    }

    explicit BasicMdpWorker(std::string_view serviceName, std::string_view brokerAddress, RequestHandler &&handler, Settings settings)
        : BasicMdpWorker(serviceName, brokerAddress, std::forward<RequestHandler>(handler), {}, settings) {
    }

    explicit BasicMdpWorker(std::string_view serviceName, const Broker &broker, RequestHandler &&handler)
        : BasicMdpWorker(serviceName, INTERNAL_ADDRESS_BROKER, std::forward<RequestHandler>(handler), broker.context, broker.settings) {
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
    MdpMessage createMessage(Command command) {
        auto message = MdpMessage::createWorkerMessage(command);
        message.setServiceName(_serviceName, MessageFrame::dynamic_bytes_tag{});
        message.setRbacToken(_rbacRole, MessageFrame::dynamic_bytes_tag{});
        return message;
    }

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
            {
                auto reply = processRequest(std::move(message));
                reply.send(*_socket).assertSuccess();
                return;
            }
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

    MdpMessage processRequest(MdpMessage &&request) {
        RequestContext context{ .request = std::move(request), .reply = replyFromRequest(context.request), .htmlData = {} };

        try {
            std::invoke(_handler, context);
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

template <HandlesRequest RequestHandler>
BasicMdpWorker(std::string_view, std::string_view, RequestHandler&&, const Context &, Settings) -> BasicMdpWorker<RequestHandler>;

template <HandlesRequest RequestHandler>
BasicMdpWorker(std::string_view, std::string_view, RequestHandler&&, Settings) -> BasicMdpWorker<RequestHandler>;

template <HandlesRequest RequestHandler>
BasicMdpWorker(std::string_view, const Broker &, RequestHandler&&) -> BasicMdpWorker<RequestHandler>;

} // namespace opencmw::majordomo

#endif
