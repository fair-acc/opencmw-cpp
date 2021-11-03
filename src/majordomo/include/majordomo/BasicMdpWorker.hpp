#ifndef MAJORDOMO_OPENCMW_WORKER_H
#define MAJORDOMO_OPENCMW_WORKER_H

#include "Message.hpp"

#include <yaz/yaz.hpp>
#include <yaz/Debug.hpp>

#include <atomic>
#include <concepts>
#include <string>

namespace Majordomo::OpenCMW {

    class BasicMdpWorker {
        std::string _service_name;
        std::string _service_description;
        std::string _rbac_role;
        std::atomic<bool> _shutdown_requested = false;
        yaz::Socket<yaz::Message, BasicMdpWorker*> _socket;

        void send(MdpMessage &&message) {
            auto frames = message.take_parts();
            assert(frames.size() == 9);
            auto span = std::span(frames);
            _socket.send_parts(span.subspan(1, span.size() - 1));
        }

    protected:

        MdpMessage create_message(MdpMessage::WorkerCommand command) {
            auto message = MdpMessage::createWorkerMessage(command);
            message.setServiceName(_service_name, yaz::MessagePart::dynamic_bytes_tag{});
            message.setRbac(_rbac_role, yaz::MessagePart::dynamic_bytes_tag{});
            return message;
        }

    public:
        virtual ~BasicMdpWorker() = default;

        explicit BasicMdpWorker(yaz::Context &context, std::string service_name)
            : _service_name{ std::move(service_name) }
            , _socket{ yaz::make_socket<yaz::Message>(context, ZMQ_DEALER, this) } {
        }

        virtual std::optional<MdpMessage> handle_get(MdpMessage &&request) = 0;
        virtual std::optional<MdpMessage> handle_set(MdpMessage &&request) = 0;

        void set_service_description(std::string description) {
            _service_description = std::move(description);
        }

        void set_rbac_role(std::string rbac) {
            _rbac_role = std::move(rbac);
        }

        void shutdown() {
            _shutdown_requested = true;
        }

        void handle_message(yaz::Message &&ymsg) {
            auto frames = ymsg.take_parts();
            // we receive 8 frames here, add first empty frame for MdpMessage
            frames.emplace(frames.begin(), yaz::MessagePart{});

            MdpMessage message(std::move(frames));
            if (!message.isValid()) {
                debug() << "invalid MdpMessage received\n";
                return;
            }

            if (message.isWorkerMessage()) {
                switch (message.workerCommand()) {
                case MdpMessage::WorkerCommand::Get:
                    if (auto reply = handle_get(std::move(message))) {
                        send(std::move(*reply));
                    }
                    return;
                case MdpMessage::WorkerCommand::Set:
                    if (auto reply = handle_set(std::move(message))) {
                        send(std::move(*reply));
                    }
                    return;
                case MdpMessage::WorkerCommand::Heartbeat:
                    debug() << "HEARTBEAT not implemented yet\n";
                    return;
                case MdpMessage::WorkerCommand::Disconnect:
                    _socket.disconnect(); // quit or reconnect?
                    return;
                default:
                    assert(!"not implemented");
                    return;
                }
            } else {
                assert(!"not implemented");
            }
        }

        bool connect(std::string_view address) {
            if (!_socket.connect(address))
                return false;

            auto ready = create_message(MdpMessage::WorkerCommand::Ready);
            ready.setBody(_service_description, yaz::MessagePart::dynamic_bytes_tag{});
            send(std::move(ready));

            return true;
        }

        bool disconnect() {
            auto msg = create_message(MdpMessage::WorkerCommand::Disconnect);
            send(std::move(msg));

            return _socket.disconnect();
        }

        void run() {
            while (!_shutdown_requested) {
                _socket.read();
            }
        }
    };
}

#endif