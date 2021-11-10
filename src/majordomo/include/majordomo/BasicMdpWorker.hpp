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
    const std::string     _serviceName;
    std::string           _serviceDescription;
    std::string           _rbacRole;
    std::atomic<bool>     _shutdownRequested = false;

    const Context &       _context;
    std::optional<Socket> _socket;

    static constexpr auto HEARTBEAT_INTERVAL = std::chrono::milliseconds(1000); // TODO share with broker

    static int toMilliseconds(auto duration) { // TODO share with broker
        return static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(duration).count());
    }

protected:
    MdpMessage createMessage(Command command) {
        auto message = MdpMessage::createWorkerMessage(command);
        message.setServiceName(_serviceName, MessageFrame::dynamic_bytes_tag{});
        message.setRbacToken(_rbacRole, MessageFrame::dynamic_bytes_tag{});
        return message;
    }

public:
    virtual ~BasicMdpWorker() = default;

    explicit BasicMdpWorker(const Context &context, std::string serviceName)
        : _serviceName{ std::move(serviceName) }, _context(context) {
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

    bool connect(std::string_view address) {
        _socket.emplace(_context, ZMQ_DEALER);

        const auto connectResult = zmq_invoke(zmq_connect, *_socket, address.data());
        if (!connectResult) {
            return false;
        }

        auto ready = createMessage(Command::Ready);
        ready.setBody(_serviceDescription, MessageFrame::dynamic_bytes_tag{});
        ready.send(*_socket).assertSuccess();

        return true;
    }

    void disconnect() {
        auto msg = createMessage(Command::Disconnect);
        msg.send(*_socket).assertSuccess();
        _socket.reset();
    }

    void handleMessage(MdpMessage &&message) {
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
                debug() << "HEARTBEAT not implemented yet\n";
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
        assert(_socket);

        std::array<zmq_pollitem_t, 1> pollerItems;
        pollerItems[0].socket = _socket->zmq_ptr;
        pollerItems[0].events = ZMQ_POLLIN;

        bool pollOk           = false;
        do {
            while (auto message = MdpMessage::receive(*_socket)) {
                handleMessage(std::move(*message));
            }

            // TODO handle heartbeats and reconnect to broker

            const auto result = zmq_invoke(zmq_poll, pollerItems.data(), static_cast<int>(pollerItems.size()), toMilliseconds(HEARTBEAT_INTERVAL));
            pollOk            = result.isValid();
        } while (pollOk && !_shutdownRequested);
    }
};
} // namespace opencmw::majordomo

#endif
