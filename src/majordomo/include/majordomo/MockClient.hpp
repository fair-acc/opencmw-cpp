#ifndef OPENCMW_MAJORDOMO_CLIENT_H
#define OPENCMW_MAJORDOMO_CLIENT_H

#include <charconv>
#include <string>
#include <string_view>
#include <unordered_map>

#include <majordomo/Broker.hpp>

#include <MdpMessage.hpp>
#include <URI.hpp>

namespace opencmw::majordomo {

class MockClient {
    const zmq::Context           &_context;
    std::optional<zmq::Socket>    _socket;
    std::string                   _brokerUrl;
    int                           _nextRequestId = 0;
    std::array<zmq_pollitem_t, 1> _pollerItems;

    // Maps request IDs to callbacks
    std::unordered_map<int, std::function<void(mdp::Message)>> _callbacks;

public:
    struct Request {
        int id;
    };

    explicit MockClient(const zmq::Context &context)
        : _context(context) {
    }

    virtual ~MockClient() = default;

    [[nodiscard]] auto connect(const opencmw::URI<> &brokerUrl) {
        _socket.emplace(_context, ZMQ_DEALER);

        const auto result = zmq::invoke(zmq_connect, *_socket, mdp::toZeroMQEndpoint(brokerUrl).data());
        if (result.isValid()) {
            _pollerItems[0].socket = _socket->zmq_ptr;
            _pollerItems[0].events = ZMQ_POLLIN;
        } else {
            _socket.reset();
        }

        return result;
    }

    bool disconnect() {
        _socket.reset();
        return true;
    }

    virtual void handleResponse(mdp::Message && /*message*/) {}

    Request      get(const std::string_view &serviceName, IoBuffer request) {
        auto [handle, message] = createRequestTemplate(mdp::Command::Get, serviceName);
        message.data           = std::move(request);
        zmq::send(std::move(message), *_socket).assertSuccess();
        return handle;
    }

    template<typename Callback>
    Request get(const std::string_view &serviceName, IoBuffer request, Callback fnc) {
        auto r = get(serviceName, std::move(request));
        _callbacks.emplace(r.id, std::move(fnc));
        return r;
    }

    Request set(std::string_view serviceName, IoBuffer request) {
        auto [handle, message] = createRequestTemplate(mdp::Command::Set, serviceName);
        message.data           = std::move(request);
        zmq::send(std::move(message), *_socket).assertSuccess();
        return handle;
    }

    template<typename Callback>
    Request set(std::string_view serviceName, IoBuffer request, Callback fnc) {
        auto r = set(serviceName, std::move(request));
        _callbacks.emplace(r.id, std::move(fnc));
        return r;
    }

    bool handleMessage(mdp::Message &&message) {
        // TODO handle client HEARTBEAT etc.

        const auto idStr = message.clientRequestID.asString();
        int        id;
        auto       asInt   = std::from_chars(idStr.begin(), idStr.end(), id);

        bool       handled = false;
        if (asInt.ec != std::errc::invalid_argument) {
            auto it = _callbacks.find(id);
            if (it != _callbacks.end()) {
                handled = true;
                it->second(std::move(message));
                _callbacks.erase(it);
            }
        }

        if (!handled) {
            handleResponse(std::move(message));
        }

        return handled;
    }

    bool tryRead(std::chrono::milliseconds timeout) {
        const auto result = zmq::invoke(zmq_poll, _pollerItems.data(), static_cast<int>(_pollerItems.size()), timeout.count());
        if (!result.isValid()) {
            return false;
        }

        if (auto message = zmq::receive<mdp::MessageFormat::WithoutSourceId>(*_socket)) {
            return handleMessage(std::move(*message));
        }

        return false;
    }

private:
    std::pair<Request, mdp::Message> createRequestTemplate(mdp::Command command, std::string_view serviceName) {
        auto req                   = std::make_pair(makeRequestHandle(), mdp::Message());
        req.second.protocolName    = mdp::clientProtocol;
        req.second.command         = command;
        req.second.serviceName     = std::string(serviceName);
        const auto requestID       = std::to_string(req.first.id);
        req.second.clientRequestID = IoBuffer(requestID.data(), requestID.size());
        return req;
    }

    Request makeRequestHandle() {
        return Request{ _nextRequestId++ };
    }
};

} // namespace opencmw::majordomo

#endif
