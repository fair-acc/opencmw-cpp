#ifndef OPENCMW_MAJORDOMO_WORKER_H
#define OPENCMW_MAJORDOMO_WORKER_H

#include "Message.hpp"

#include <majordomo/Debug.hpp>
#include <majordomo/Settings.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <concepts>
#include <string>

namespace opencmw::majordomo {

class BasicMdpWorker {
public:
    using Clock     = std::chrono::steady_clock;
    using Timestamp = std::chrono::time_point<Clock>;

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
    virtual ~BasicMdpWorker() = default;

    explicit BasicMdpWorker(Settings settings, const Context &context, std::string_view brokerAddress, std::string_view serviceName)
        : _settings{ std::move(settings) }, _brokerAddress{ std::move(brokerAddress) }, _serviceName{ std::move(serviceName) }, _context(context) {
    }

    virtual std::optional<MdpMessage> handleGet(MdpMessage &&request) = 0;
    virtual std::optional<MdpMessage> handleSet(MdpMessage &&request) = 0;

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
                if (auto reply = handleGet(std::move(message)); reply) {
                    reply->send(*_socket).assertSuccess();
                }
                return;
            case Command::Set:
                if (auto reply = handleSet(std::move(message)); reply) {
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
