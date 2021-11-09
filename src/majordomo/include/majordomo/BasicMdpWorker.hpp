#ifndef OPENCMW_MAJORDOMO_WORKER_H
#define OPENCMW_MAJORDOMO_WORKER_H

#include "Message.hpp"

#include "Broker.hpp"
#include "Debug.hpp"

#include <atomic>
#include <concepts>
#include <string>

namespace opencmw::majordomo {

class BasicMdpWorker {
    const std::string     _serviceName;
    std::string           _serviceDescription;
    std::string           _rbacRole;
    std::atomic<bool>     _shutdownRequested = false;

    const Context        &_context;
    std::optional<Socket> _socket;

protected:
    MdpMessage createMessage(WorkerCommand command) {
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

        auto ready = createMessage(WorkerCommand::Ready);
        ready.setBody(_serviceDescription, MessageFrame::dynamic_bytes_tag{});
        ready.send(*_socket).assertSuccess();

        return true;
    }

    void disconnect() {
        auto msg = createMessage(WorkerCommand::Disconnect);
        msg.send(*_socket).assertSuccess();
        _socket.reset();
    }

    void handleMessage(MdpMessage &&message) {
        if (!message.isValid()) {
            debug() << "invalid MdpMessage received\n";
            return;
        }

        if (message.isWorkerMessage()) {
            switch (message.workerCommand()) {
            case WorkerCommand::Get:
                if (auto reply = handleGet(std::move(message)); reply) {
                    reply->send(*_socket).assertSuccess();
                }
                return;
            case WorkerCommand::Set:
                if (auto reply = handleSet(std::move(message)); reply) {
                    reply->send(*_socket).assertSuccess();
                }
                return;
            case WorkerCommand::Heartbeat:
                debug() << "HEARTBEAT not implemented yet\n";
                return;
            case WorkerCommand::Disconnect:
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
        while (!_shutdownRequested) {
            assert(_socket);
            auto message = MdpMessage::receive(*_socket);
            if (!message) {
                continue;
            }

            handleMessage(std::move(*message));
        }
    }
};
} // namespace opencmw::majordomo

#endif
