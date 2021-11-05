#ifndef YAMAL_CLIENT_H
#define YAMAL_CLIENT_H

#include <cassert>
#include <charconv>
#include <string>
#include <string_view>
#include <unordered_map>

#include <majordomo/Message.hpp>
#include <yaz/yaz.hpp>

namespace Majordomo::OpenCMW {

class Client {
    yaz::Socket<MdpMessage, Client *>                        _socket;
    std::string                                              _broker_url;
    int                                                      _next_request_id = 0;
    std::unordered_map<int, std::function<void(MdpMessage)>> _callbacks;

public:
    struct Request {
        int id;
    };

    Client(yaz::Context &context)
        : _socket{ yaz::make_socket<MdpMessage>(context, ZMQ_DEALER, this) } {
    }

    virtual ~Client() = default;

    bool connect(std::string_view broker_url) {
        return _socket.connect(broker_url);
    }

    bool disconnect() {
        return _socket.disconnect();
    }

    virtual void handle_response(MdpMessage &&) {}

    template<typename BodyType>
    Request get(std::string_view service_name, BodyType request) {
        auto [handle, message] = create_request_template(MdpMessage::ClientCommand::Get, service_name);
        message.setBody(YAZ_FWD(request), MessagePart::dynamic_bytes_tag{});
        send(std::move(message));
        return handle;
    }

    template<typename BodyType, typename Callback>
    Request get(std::string_view service_name, BodyType request, Callback fnc) {
        auto r = get(service_name, YAZ_FWD(request));
        _callbacks.emplace(r.id, YAZ_FWD(fnc));
        return r;
    }

    template<typename BodyType>
    Request set(std::string_view service_name, BodyType request) {
        auto [handle, message] = create_request_template(MdpMessage::ClientCommand::Set, service_name);
        message.setBody(YAZ_FWD(request), MessagePart::dynamic_bytes_tag{});
        send(std::move(message));
        return handle;
    }

    template<typename BodyType, typename Callback>
    Request set(std::string_view service_name, BodyType request, Callback fnc) {
        auto r = set(service_name, request);
        _callbacks.emplace(r.id, YAZ_FWD(fnc));
        return r;
    }

    void handle_message(MdpMessage &&message) {
        // we receive 8 frames here, add first empty frame for MdpMessage
        auto &frames = message.parts_ref();
        frames.emplace(frames.begin(), yaz::MessagePart{});

        if (!message.isValid()) {
            debug() << "Received invalid message" << message << std::endl;
            return;
        }

        // TODO handle client HEARTBEAT etc.

        const auto id_str = message.clientRequestId();
        int        id;
        auto       as_int  = std::from_chars(id_str.begin(), id_str.end(), id);

        bool       handled = false;
        if (as_int.ec != std::errc::invalid_argument) {
            auto it = _callbacks.find(id);
            if (it != _callbacks.end()) {
                handled = true;
                it->second(std::move(message));
                _callbacks.erase(it);
            }
        }

        if (!handled)
            handle_response(std::move(message));
    }

    void try_read() {
        _socket.try_read();
    }

private:
    std::pair<Request, MdpMessage> create_request_template(MdpMessage::ClientCommand command, std::string_view service_name) {
        auto req = std::make_pair(make_request_handle(), MdpMessage::createClientMessage(command));
        req.second.setServiceName(service_name, MessagePart::dynamic_bytes_tag{});
        req.second.setClientRequestId(std::to_string(req.first.id), MessagePart::dynamic_bytes_tag{});
        return req;
    }

    Request make_request_handle() {
        return Request{ _next_request_id++ };
    }

    void send(MdpMessage &&message) {
        auto &frames = message.parts_ref();
        auto  span   = std::span(frames);
        _socket.send_parts(span.subspan(1, span.size() - 1));
    }
};

} // namespace Majordomo::OpenCMW

#endif // YAMAL_CLIENT_H
