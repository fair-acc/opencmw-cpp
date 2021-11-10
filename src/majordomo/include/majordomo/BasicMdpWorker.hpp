#ifndef OPENCMW_MAJORDOMO_WORKER_H
#define OPENCMW_MAJORDOMO_WORKER_H

#include "Message.hpp"

#include "Broker.hpp"
#include "Debug.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <concepts>
#include <string>

namespace opencmw::majordomo {

class BasicMdpWorker {
    static constexpr auto HEARTBEAT_INTERVAL = std::chrono::milliseconds(1000); // TODO share with broker
    static constexpr auto HEARTBEAT_LIVENESS = 3;                               // TODO share with broker

    using Clock                              = std::chrono::steady_clock;
    using Timestamp                          = std::chrono::time_point<Clock>;

    const std::string             _brokerAddress;
    const std::string             _serviceName;
    std::string                   _serviceDescription;
    std::string                   _rbacRole;
    std::atomic<bool>             _shutdownRequested = false;
    int                           _liveness          = 0;
    Timestamp                     _heartbeatAt;

    const Context                &_context;
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

    explicit BasicMdpWorker(const Context &context, std::string_view brokerAddress, std::string_view serviceName)
        : _brokerAddress{ std::move(brokerAddress) }, _serviceName{ std::move(serviceName) }, _context(context) {
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
        _liveness = HEARTBEAT_LIVENESS;

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
                _socket.reset(); // quit or reconnect?
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

        bool pollOk = false;
        do {
            while (auto message = MdpMessage::receive(*_socket)) {
                handleMessage(std::move(*message));
            }

            if (Clock::now() > _heartbeatAt && --_liveness == 0) {
                // TODO sleep? java: LockSupport.parkNanos(TimeUnit.MILLISECONDS.toNanos(reconnect));
                if (!connectToBroker())
                    return;
            }
            assert(_socket);

            if (Clock::now() > _heartbeatAt) {
                auto heartbeat = createMessage(Command::Heartbeat);
                heartbeat.send(*_socket).assertSuccess();
                _heartbeatAt = Clock::now() + HEARTBEAT_INTERVAL;
            }

            const auto result = zmq_invoke(zmq_poll, _pollerItems.data(), static_cast<int>(_pollerItems.size()), toMilliseconds(HEARTBEAT_INTERVAL));
            pollOk            = result.isValid();
        } while (pollOk && !_shutdownRequested);
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

        _liveness              = HEARTBEAT_LIVENESS;
        _heartbeatAt           = Clock::now() + HEARTBEAT_INTERVAL;

        return true;
    }

    static int toMilliseconds(auto duration) { // TODO share with broker
        return static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(duration).count());
    }
};
} // namespace opencmw::majordomo

#endif
