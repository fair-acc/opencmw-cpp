#ifndef OPENCMW_CPP_CMW_CLIENT_HPP
#define OPENCMW_CPP_CMW_CLIENT_HPP

#include <algorithm>
#include <atomic>
#include <chrono>
#include <ClientContext.hpp>
#include <majordomo/Message.hpp>
#include <majordomo/ZmqPtr.hpp>
#include <MdpMessage.hpp>
#include <opencmw.hpp>
#include <URI.hpp>

namespace opencmw::client {

using opencmw::majordomo::MessageFormat;

namespace detail {

enum class ConnectionState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    DISCONNECTING
};

struct Connection {
    std::string       _authority;
    majordomo::Socket _socket;
    ConnectionState   _connectionState               = ConnectionState::DISCONNECTED;
    timePoint         _nextReconnectAttemptTimeStamp = std::chrono::system_clock::now();

    Connection(const majordomo::Context &context, const std::string_view authority, const int zmq_dealer_type)
        : _authority{ authority }, _socket{ context, zmq_dealer_type } {
        majordomo::initializeZmqSocket(_socket).assertSuccess();
    }
};

} // namespace detail

class MDClientBase {
public:
    virtual ~MDClientBase()                                                                                   = default;
    virtual bool      receive(mdp::Message &message)                                                          = 0;
    virtual timePoint housekeeping(const timePoint &now)                                                      = 0;
    virtual void      get(const URI<STRICT> &, majordomo::MessageFrame &)                                     = 0;
    virtual void      set(const URI<STRICT> &, majordomo::MessageFrame &, const std::span<const std::byte> &) = 0;
    virtual void      subscribe(const URI<STRICT> &, majordomo::MessageFrame &)                               = 0;
    virtual void      unsubscribe(const URI<STRICT> &, majordomo::MessageFrame &)                             = 0;
};

class Client : public MDClientBase {
    using timeUnit = std::chrono::milliseconds;
    const timeUnit                  _clientTimeout;
    const majordomo::Context       &_context;
    const std::string               _clientId;
    const std::string               _sourceName;
    std::vector<detail::Connection> _connections;
    std::vector<zmq_pollitem_t>    &_pollItems;

public:
    explicit Client(const majordomo::Context &context,
            std::vector<zmq_pollitem_t>      &pollItems,
            const timeUnit                    timeout  = 1s,
            std::string                       clientId = "")
        : _clientTimeout(timeout), _context(context), _clientId(std::move(clientId)), _sourceName(fmt::format("OpenCmwClient(clientId: {})", _clientId)), _pollItems(pollItems) {}

    void connect(const URI<STRICT> &uri) {
        auto con = detail::Connection(_context, uri.authority().value(), ZMQ_DEALER);
        _connections.push_back(std::move(con));
    }

    void connect(detail::Connection &con) {
        using detail::ConnectionState;
        // todo: switch on scheme, for now use tcp if port is specified, otherwise use inproc
        auto        ep     = con._authority.find(':') != std::string::npos ? URI<STRICT>("tcp://"s + con._authority) : URI<STRICT>("inproc://"s + con._authority);
        std::string zmq_ep = majordomo::toZeroMQEndpoint(ep);
        if (opencmw::majordomo::zmq_invoke(zmq_connect, con._socket, zmq_ep).isValid()) {
            _pollItems.push_back({ .socket = con._socket.zmq_ptr, .fd = 0, .events = ZMQ_POLLIN, .revents = 0 });
        }
        con._connectionState = ConnectionState::CONNECTED;
    }

    detail::Connection &findConnection(const URI<STRICT> &uri) {
        const auto con = std::ranges::find_if(_connections, [&uri](detail::Connection &c) { return c._authority == uri.authority().value(); });
        if (con == _connections.end()) {
            auto newCon = detail::Connection(_context, uri.authority().value(), ZMQ_DEALER);
            connect(newCon);
            _connections.push_back(std::move(newCon));
            return _connections.back();
        }
        return *con;
    }

    void get(const URI<STRICT> &uri, majordomo::MessageFrame &req_id) override {
        const auto &con     = findConnection(uri);
        auto        message = createRequestTemplate(majordomo::Command::Get, uri.relativeRefNoFragment().value(), req_id);
        message.send(con._socket).assertSuccess();
    }

    void set(const URI<STRICT> &uri, majordomo::MessageFrame &req_id, const std::span<const std::byte> &request) override {
        const auto &con     = findConnection(uri);
        auto        message = createRequestTemplate(majordomo::Command::Set, uri.relativeRefNoFragment().value(), req_id);
        message.setBody(std::string(reinterpret_cast<const char *>(request.data()), request.size()), majordomo::MessageFrame::dynamic_bytes_tag{});
        message.send(con._socket).assertSuccess();
    }

    void subscribe(const URI<STRICT> &uri, majordomo::MessageFrame &req_id) override {
        const auto &con     = findConnection(uri);
        auto        message = createRequestTemplate(majordomo::Command::Subscribe, uri.relativeRefNoFragment().value(), req_id);
        message.send(con._socket).assertSuccess();
    }

    void unsubscribe(const URI<STRICT> &uri, majordomo::MessageFrame &req_id) override {
        const auto &con     = findConnection(uri);
        auto        message = createRequestTemplate(majordomo::Command::Unsubscribe, uri.relativeRefNoFragment().value(), req_id);
        message.send(con._socket).assertSuccess();
    }

    bool disconnect(detail::Connection &con) {
#if not defined(__EMSCRIPTEN__) and (not defined(__clang__) or (__clang_major__ >= 16))
        const auto remove = std::ranges::remove_if(_pollItems, [&con](zmq_pollitem_t &pollItem) { return pollItem.socket == con._socket.zmq_ptr; });
        _pollItems.erase(remove.begin(), remove.end());
#else
        const auto remove = std::remove_if(_pollItems.begin(), _pollItems.end(), [&con](zmq_pollitem_t &pollItem) { return pollItem.socket == con._socket.zmq_ptr; });
        _pollItems.erase(remove, _pollItems.end());
#endif
        zmq_invoke(zmq_disconnect, con._socket, majordomo::toZeroMQEndpoint(URI<STRICT>(con._authority)).data()).ignoreResult();
        con._connectionState = detail::ConnectionState::DISCONNECTED;
        return true;
    }

    static bool handleMessage(const majordomo::MdpMessage &message, mdp::Message &output) {
        if (!message.isValid()) {
            return true;
        }
        // subscription updates
        if (message.command() == majordomo::Command::Notify || message.command() == majordomo::Command::Final) {
            output.arrivalTime = std::chrono::system_clock::now();
            output.data.resize(message.body().size());
            URI<uri_check::STRICT> uri{ std::string{ message.topic() } };
            std::memcpy(output.data.data(), message.body().begin(), message.body().size());
            output.endpoint   = URI<uri_check::STRICT>(std::string{ message.topic() });
            auto params       = output.endpoint.queryParamMap();
            auto requestId_sv = message.clientRequestId();
            if (auto result = std::from_chars(requestId_sv.data(), requestId_sv.data() + requestId_sv.size(), output.id); result.ec == std::errc::invalid_argument || result.ec == std::errc::result_out_of_range) {
                output.id = 0;
            }
            return true;
        }
        return true;
    }

    bool receive(mdp::Message &msg) override {
        for (auto &con : _connections) {
            if (con._connectionState != detail::ConnectionState::CONNECTED) {
                continue;
            }
            if (auto message = majordomo::MdpMessage::receive(con._socket)) {
                return handleMessage(std::move(*message), msg);
            }
        }
        return false;
    }

    // method to be called in regular time intervals to send and verify heartbeats
    timePoint housekeeping(const timePoint &now) override {
        using detail::ConnectionState;
        // handle connection state
        for (auto &con : _connections) {
            switch (con._connectionState) {
            case ConnectionState::DISCONNECTED:
                if (con._nextReconnectAttemptTimeStamp <= now) {
                    connect(con);
                }
                break;
            case ConnectionState::CONNECTING:
                if (con._nextReconnectAttemptTimeStamp + _clientTimeout < now) {
                    // abort connection attempt and start a new one
                }
                break;
            case ConnectionState::CONNECTED:
            case ConnectionState::DISCONNECTING:
                break; // do nothing
            }
        }
        return now + _clientTimeout / 2;
    }

private:
    static majordomo::MdpMessage createRequestTemplate(majordomo::Command command, std::string_view serviceName, const majordomo::MessageFrame &req_id) {
        auto req = majordomo::MdpMessage::createClientMessage(command);
        req.setServiceName(serviceName, majordomo::MessageFrame::dynamic_bytes_tag{});
        req.setClientRequestId(req_id.data(), majordomo::MessageFrame::dynamic_bytes_tag{});
        return req;
    }
};

class SubscriptionClient : public MDClientBase {
    using timeUnit = std::chrono::milliseconds;
    const timeUnit                  _clientTimeout;
    const majordomo::Context       &_context;
    const std::string               _clientId;
    const std::string               _sourceName;
    std::vector<detail::Connection> _connections;
    std::vector<zmq_pollitem_t>    &_pollItems;

public:
    explicit SubscriptionClient(const majordomo::Context &context, std::vector<zmq_pollitem_t> &pollItems, const timeUnit timeout = 1s, std::string clientId = "")
        : _clientTimeout(timeout), _context(context), _clientId(std::move(clientId)), _sourceName(fmt::format("OpenCmwClient(clientId: {})", _clientId)), _pollItems(pollItems) {}

    void connect(const URI<STRICT> &uri) {
        auto con = detail::Connection(_context, uri.authority().value(), ZMQ_SUB);
        _connections.push_back(std::move(con));
    }

    void connect(detail::Connection &con) {
        using detail::ConnectionState;
        // todo: replace this by proper logic, for now use tcp if port is specified, otherwise use inproc... mds+tcp:// -> tcp://, mds+inproc:// -> inproc:// -> auto detect
        auto        ep     = con._authority.find(':') != std::string::npos ? URI<STRICT>("tcp://"s + con._authority) : URI<STRICT>("inproc://"s + con._authority);
        std::string zmq_ep = majordomo::toZeroMQEndpoint(ep);
        if (opencmw::majordomo::zmq_invoke(zmq_connect, con._socket, zmq_ep).isValid()) {
            _pollItems.push_back({ .socket = con._socket.zmq_ptr, .fd = 0, .events = ZMQ_POLLIN, .revents = 0 });
            con._connectionState = ConnectionState::CONNECTED;
        }
    }

    detail::Connection &findConnection(const URI<STRICT> &uri) {
        // TODO: use a map for more efficient lookup? how to handle multiple uris which resolve to the same host?
        auto con = std::ranges::find_if(_connections, [&uri](const detail::Connection &c) { return c._authority == uri.authority().value(); });
        if (con == _connections.end()) {
            auto newCon = detail::Connection(_context, uri.authority().value(), ZMQ_SUB);
            connect(newCon);
            _connections.push_back(std::move(newCon));
            return _connections.back();
        }
        return *con;
    }

    void get(const URI<STRICT> & /*uri*/, majordomo::MessageFrame &) override {
        throw std::logic_error("get not implemented");
    }

    void set(const URI<STRICT> & /*uri*/, majordomo::MessageFrame &, const std::span<const std::byte> & /*request*/) override {
        throw std::logic_error("get not implemented");
    }

    void subscribe(const URI<STRICT> &uri, majordomo::MessageFrame & /*reqId*/) override {
        auto       &con         = findConnection(uri);
        std::string serviceName = uri.relativeRefNoFragment().value();
        assert(!serviceName.empty());
        opencmw::majordomo::zmq_invoke(zmq_setsockopt, con._socket, ZMQ_SUBSCRIBE, serviceName.data(), serviceName.size()).assertSuccess();
    }

    void unsubscribe(const URI<STRICT> &uri, majordomo::MessageFrame & /*reqId*/) override {
        auto       &con         = findConnection(uri);
        std::string serviceName = uri.relativeRefNoFragment().value();
        assert(!serviceName.empty());
        opencmw::majordomo::zmq_invoke(zmq_setsockopt, con._socket, ZMQ_UNSUBSCRIBE, serviceName.data(), serviceName.size()).assertSuccess();
    }

    bool disconnect(detail::Connection &con) {
#if not defined(__EMSCRIPTEN__) and (not defined(__clang__) or (__clang_major__ >= 16))
        const auto remove = std::ranges::remove_if(_pollItems, [&con](const zmq_pollitem_t &pollItem) { return pollItem.socket == con._socket.zmq_ptr; });
        _pollItems.erase(remove.begin(), remove.end());
#else
        const auto remove = std::remove_if(_pollItems.begin(), _pollItems.end(), [&con](const zmq_pollitem_t &pollItem) { return pollItem.socket == con._socket.zmq_ptr; });
        _pollItems.erase(remove, _pollItems.end());
#endif
        zmq_invoke(zmq_disconnect, con._socket, majordomo::toZeroMQEndpoint(URI<STRICT>(con._authority)).data()).ignoreResult();
        con._connectionState = detail::ConnectionState::DISCONNECTED;
        return true;
    }

    static bool handleMessage(const majordomo::BasicMdpMessage<MessageFormat::WithSourceId> &message, mdp::Message &output) {
        if (!message.isValid()) {
            return true;
        }
        // subscription updates
        if (message.command() == majordomo::Command::Notify || message.command() == majordomo::Command::Final) {
            output.arrivalTime = std::chrono::system_clock::now();
            output.data.resize(message.body().size());
            std::memcpy(output.data.data(), message.body().begin(), message.body().size());
            // output.serviceName = URI<uri_check::STRICT>(std::string{ message.serviceName() });
            output.serviceName = URI<uri_check::STRICT>(std::string{ message.sourceId() }); // // temporary hack until serviceName -> 'requestedTopic' and 'topic' -> 'replyTopic'
            output.endpoint    = URI<uri_check::STRICT>(std::string{ message.topic() });
            output.clientRequestID.reset();
            output.clientRequestID.put(message.clientRequestId());
            output.id = 0; // review if this is still needed
            return true;
        }
        return true;
    }

    bool receive(mdp::Message &msg) override {
        for (const detail::Connection &con : _connections) {
            if (con._connectionState != detail::ConnectionState::CONNECTED) {
                continue;
            }
            while (true) {
                if (auto message = majordomo::BasicMdpMessage<MessageFormat::WithSourceId>::receive(con._socket)) {
                    return handleMessage(*message, msg);
                } else {
                    break;
                }
            }
        }
        return false;
    }

    // method to be called in regular time intervals to send and verify heartbeats
    timePoint housekeeping(const timePoint &now) override {
        using detail::ConnectionState;
        // handle monitor events
        // handle connection state
        for (auto &con : _connections) {
            switch (con._connectionState) {
            case ConnectionState::DISCONNECTED:
                if (con._nextReconnectAttemptTimeStamp <= now) {
                    connect(con);
                }
                break;
            case ConnectionState::CONNECTING:
                if (con._nextReconnectAttemptTimeStamp + _clientTimeout < now) {
                    // abort connection attempt and start a new one
                }
                break;
            case ConnectionState::CONNECTED:
            case ConnectionState::DISCONNECTING:
                break; // do nothing
            }
        }
        return now + _clientTimeout / 2;
    }
};

/*
 * Implementation of the Majordomo client protocol. Spawns a single thread which controls all client connections and sockets.
 * A dispatcher thread reads the requests from the command ring buffer and dispatches them to the zeromq poll loop using an inproc socket pair.
 */
class MDClientCtx : public ClientBase {
    using timeUnit = std::chrono::milliseconds;
    std::unordered_map<URI<STRICT>, std::unique_ptr<MDClientBase>> _clients;
    const majordomo::Context                                      &_zctx;
    majordomo::Socket                                              _control_socket_send;
    majordomo::Socket                                              _control_socket_recv;
    std::jthread                                                   _poller;
    std::vector<zmq_pollitem_t>                                    _pollitems{};
    std::unordered_map<std::size_t, Request>                       _requests;
    std::unordered_map<std::string, Subscription>                  _subscriptions;
    timeUnit                                                       _timeout;
    std::string                                                    _clientId;
    std::size_t                                                    _request_id = 0;

public:
    explicit MDClientCtx(const majordomo::Context &zeromq_context, const timeUnit timeout = 1s, std::string clientId = "") // todo: also pass thread pool
        : _zctx{ zeromq_context }, _control_socket_send(zeromq_context, ZMQ_PAIR), _control_socket_recv(zeromq_context, ZMQ_PAIR), _timeout(timeout), _clientId(std::move(clientId)) {
        _poller = std::jthread([this](const std::stop_token &stoken) { this->poll(stoken); });
        majordomo::zmq_invoke(zmq_bind, _control_socket_send, "inproc://mdclientControlSocket").assertSuccess();
        _pollitems.push_back({ .socket = _control_socket_recv.zmq_ptr, .fd = 0, .events = ZMQ_POLLIN, .revents = 0 });
    }

    std::vector<std::string> protocols() override {
        return { "mdp", "mdp+tcp", "mdp+inproc", "mds", "mds+tcp", "mds+inproc" }; // majordomo protocol and subscription protocol, if transport is unspecified, tcp is used if authority contains a port
    }

    std::unique_ptr<MDClientBase> &getClient(const URI<STRICT> &uri) {
        auto baseUri = URI<STRICT>::factory(uri).setQuery({}).path("").fragment("").build();
        if (_clients.contains(baseUri)) {
            return _clients.at(baseUri);
        }
        auto [it, ins] = _clients.emplace(baseUri, createClient(baseUri));
        if (!ins) {
            throw std::logic_error("could not insert client into client list\n");
        }
        return it->second;
    }

    std::unique_ptr<MDClientBase> createClient(const URI<STRICT> &uri) {
        if (uri.str().starts_with("mdp")) {
            return std::make_unique<Client>(_zctx, _pollitems, _timeout, _clientId);
        } else if (uri.str().starts_with("mds")) {
            return std::make_unique<SubscriptionClient>(_zctx, _pollitems, _timeout, _clientId);
        } else {
            throw std::logic_error("unsupported protocol");
        }
    }

    void stop() override {
        _poller.request_stop();
        _poller.join();
    }

    void request(Command &cmd) override {
        std::size_t req_id = 0;
        if (cmd.callback) {
            if (cmd.command == mdp::Command::Get || cmd.command == mdp::Command::Set) {
                req_id = _request_id++;
                _requests.insert({ req_id, Request{ .uri = cmd.endpoint, .callback = std::move(cmd.callback), .timestamp_received = cmd.arrivalTime } });
            } else if (cmd.command == mdp::Command::Subscribe) {
                req_id = _request_id++;
                _subscriptions.insert({ *cmd.endpoint.relativeRefNoFragment(), Subscription{ .uri = cmd.endpoint, .callback = std::move(cmd.callback), .timestamp_received = cmd.arrivalTime } });
            } else if (cmd.command == mdp::Command::Unsubscribe) {
                _requests.erase(0); // todo: lookup correct subscription
            }
        }
        sendCmd(cmd.endpoint, cmd.command, req_id, cmd.data);
    }

private:
    void sendCmd(const URI<STRICT> &uri, mdp::Command commandType, std::size_t req_id, const IoBuffer &data = std::move(IoBuffer())) const {
        const bool              isSet = commandType == mdp::Command::Set;
        majordomo::MessageFrame cmdType{ std::string{ static_cast<char>(commandType) }, majordomo::MessageFrame::dynamic_bytes_tag() };
        cmdType.send(_control_socket_send, ZMQ_SNDMORE).assertSuccess();
        majordomo::MessageFrame reqId{ std::to_string(req_id), majordomo::MessageFrame::dynamic_bytes_tag() };
        reqId.send(_control_socket_send, ZMQ_SNDMORE).assertSuccess();
        majordomo::MessageFrame endpoint{ uri.str(), majordomo::MessageFrame::dynamic_bytes_tag() };
        endpoint.send(_control_socket_send, isSet ? ZMQ_SNDMORE : 0).assertSuccess();
        if (isSet) {
            majordomo::MessageFrame dataframe{ std::string_view{ reinterpret_cast<const char *>(data.data()), data.size() }, majordomo::MessageFrame::dynamic_bytes_tag() };
            dataframe.send(_control_socket_send, 0).assertSuccess();
        }
    }

    void handleRequests() {
        majordomo::MessageFrame cmd;
        majordomo::MessageFrame reqId;
        majordomo::MessageFrame endpoint;
        while (cmd.receive(_control_socket_recv, ZMQ_DONTWAIT).isValid()) {
            if (!reqId.receive(_control_socket_recv, ZMQ_DONTWAIT).isValid()) {
                throw std::logic_error("invalid request received: failure receiving message");
            }
            if (!endpoint.receive(_control_socket_recv, ZMQ_DONTWAIT).isValid()) {
                throw std::logic_error("invalid request received: invalid message contents");
            }
            URI<STRICT> uri{ std::string(endpoint.data()) };
            auto       &client = getClient(uri);
            if (cmd.data().size() != 1) {
                throw std::logic_error("invalid request received: wrong number of frames");
            } else if (cmd.data()[0] == static_cast<char>(mdp::Command::Get)) {
                client->get(uri, reqId);
            } else if (cmd.data()[0] == static_cast<char>(mdp::Command::Set)) {
                majordomo::MessageFrame data;
                if (!data.receive(_control_socket_recv, ZMQ_DONTWAIT).isValid()) {
                    throw std::logic_error("missing set str");
                }
                client->set(uri, reqId, as_bytes(std::span(data.data().data(), data.data().size())));
            } else if (cmd.data()[0] == static_cast<char>(mdp::Command::Subscribe)) {
                client->subscribe(uri, reqId);
            } else if (cmd.data()[0] == static_cast<char>(mdp::Command::Unsubscribe)) {
                client->unsubscribe(uri, reqId);
            } else {
                throw std::logic_error("invalid request received"); // messages always consist of 2 frames
            }
        }
    }

    void poll(const std::stop_token &stoken) {
        auto nextHousekeeping = std::chrono::system_clock::now();
        majordomo::zmq_invoke(zmq_connect, _control_socket_recv, "inproc://mdclientControlSocket").assertSuccess();
        while (!stoken.stop_requested() && majordomo::zmq_invoke(zmq_poll, _pollitems.data(), static_cast<int>(_pollitems.size()), 200)) {
            if (auto now = std::chrono::system_clock::now(); nextHousekeeping < now) {
                nextHousekeeping = housekeeping(now);
                // expire old subscriptions/requests/connections
            }
            handleRequests();
            for (const auto &[uri, client] : _clients) {
                mdp::Message receivedEvent;
                while (client->receive(receivedEvent)) {
                    if (auto serviceName = receivedEvent.serviceName.str(); _subscriptions.contains(serviceName)) {
                        _subscriptions.at(serviceName).callback(receivedEvent); // callback
                    }
                    if (_requests.contains(receivedEvent.id)) {
                        _requests.at(receivedEvent.id).callback(receivedEvent); // callback
                        _requests.erase(receivedEvent.id);
                    }
                    // perform housekeeping duties if necessary
                    if (auto now = std::chrono::system_clock::now(); nextHousekeeping < now) {
                        nextHousekeeping = housekeeping(now);
                    }
                }
            }
        }
    }

    timePoint housekeeping(timePoint now) const {
        timePoint next = now + _timeout;
        for (const auto &[uri, client] : _clients) {
            next = std::min(next, client->housekeeping(now));
        }
        return next;
    }
    // manage commands: setup new clients if necessary and establish new subscriptions etc
    // todo: remove unused (= no open subscriptions && last request was some time ago) clients after some unused time
};

} // namespace opencmw::client
#endif // OPENCMW_CPP_CMW_CLIENT_HPP
