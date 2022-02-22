#ifndef OPENCMW_CPP_CMW_CLIENT_HPP
#define OPENCMW_CPP_CMW_CLIENT_HPP

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <ClientPublisher.hpp>
#include <disruptor/Disruptor.hpp>
#include <functional>
#include <IoSerialiser.hpp>
#include <majordomo/Message.hpp>
#include <majordomo/Settings.hpp> // from majordomo project, consider to move to core or util to make client independent of majodomo (but core dep on zmq :/)
#include <majordomo/Utils.hpp>
#include <majordomo/ZmqPtr.hpp> // from majordomo project, consider to move to core or util to make client independent of majodomo (but core dep on zmq :/)
#include <memory>
#include <opencmw.hpp>
#include <URI.hpp>
#include <utility>

namespace opencmw::client {

using namespace std::literals;
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
    // std::chrono::milliseconds _lastHeartbeat = 0s;
    ConnectionState           _connectionState               = ConnectionState::DISCONNECTED;
    std::chrono::milliseconds _nextReconnectAttemptTimeStamp = 0s;

    Connection(const majordomo::Context &context, const std::string_view authority, const int zmq_dealer_type)
        : _authority{ authority }, _socket{ context, zmq_dealer_type } {
        auto commonSocketInit = [](const majordomo::Socket &sock) {
            const majordomo::Settings settings_{};
            const int                 heartbeatInterval = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(settings_.heartbeatInterval).count());
            const int                 ttl               = heartbeatInterval * settings_.heartbeatLiveness;
            const int                 hb_timeout        = heartbeatInterval * settings_.heartbeatLiveness;
            return zmq_invoke(zmq_setsockopt, sock, ZMQ_SNDHWM, &settings_.highWaterMark, sizeof(settings_.highWaterMark))
                && zmq_invoke(zmq_setsockopt, sock, ZMQ_RCVHWM, &settings_.highWaterMark, sizeof(settings_.highWaterMark))
                && zmq_invoke(zmq_setsockopt, sock, ZMQ_HEARTBEAT_TTL, &ttl, sizeof(ttl))
                && zmq_invoke(zmq_setsockopt, sock, ZMQ_HEARTBEAT_TIMEOUT, &hb_timeout, sizeof(hb_timeout))
                && zmq_invoke(zmq_setsockopt, sock, ZMQ_HEARTBEAT_IVL, &heartbeatInterval, sizeof(heartbeatInterval))
                && zmq_invoke(zmq_setsockopt, sock, ZMQ_LINGER, &heartbeatInterval, sizeof(heartbeatInterval));
        };
        commonSocketInit(_socket).assertSuccess();
        _nextReconnectAttemptTimeStamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()); // timeout);
    }
};

} // namespace detail

class Client : public ClientBase {
private:
    static std::atomic<int>         _instanceCount;
    const std::chrono::milliseconds _clientTimeout;
    const majordomo::Context       &_context;
    // const majordomo::Socket       _monitorSocket;
    // std::vector<URI<uri_check::RELAXED>> _resolvedNames; // these get resolved by asking the /mmi endpoint of the supplied _authority or the default broker
    const std::string               _clientId;
    const std::string               _sourceName;
    std::chrono::milliseconds       _heartbeatInterval;
    std::vector<detail::Connection> _connections;
    std::vector<zmq_pollitem_t>     _pollItems;
    long                            _nextRequestId = 0;

public:
    explicit Client(const majordomo::Context &context,
            const std::chrono::milliseconds   timeout  = 1s,
            std::string                       clientId = "")
        : _clientTimeout(timeout), _context(context),
        // _monitorSocket{ context, ZMQ_PAIR },
        _clientId(std::move(clientId))
        , _sourceName(fmt::format("OpenCmwClient(clientId: {})", _clientId))
        , _heartbeatInterval(timeout) {
        // setup socket monitor // todo: use client unique socket name, to avoid clash with other instances // disabled as it breaks the code
        // majordomo::zmq_invoke(zmq_connect, _monitorSocket, "inproc://monitor-client").assertSuccess();
        // _pollItem[2].socket = _monitorSocket.zmq_ptr;
        // _pollItem[2].events = ZMQ_POLLIN;

        // take the minimum of the (albeit worker) heartbeat, client (if defined) or locally prescribed timeout
        std::chrono::milliseconds HEARTBEAT_INTERVAL(1000);
        _heartbeatInterval = std::chrono::milliseconds(std::min(HEARTBEAT_INTERVAL.count(), _clientTimeout == std::chrono::milliseconds(0) ? timeout.count() : std::min(timeout.count(), _clientTimeout.count())));
    }

    void connect(const URI<RELAXED> &uri) {
        // _connections.emplace_back(_context, std::string_view(uri.authority().value()));
        auto con = detail::Connection(_context, uri.authority().value(), ZMQ_DEALER);
        _connections.push_back(std::move(con));
    }

    URI<RELAXED> connect(detail::Connection &con) {
        using detail::ConnectionState;
        // todo: replace this by proper logic, for now use tcp if port is specified, otherwise use inproc
        auto        ep     = con._authority.find(':') != std::string::npos ? URI<STRICT>("tcp://"s + con._authority) : URI<STRICT>("inproc://"s + con._authority);
        std::string zmq_ep = majordomo::toZeroMQEndpoint(ep).data();
        if (opencmw::majordomo::zmq_invoke(zmq_connect, con._socket, zmq_ep).isValid()) {
            // zmq_invoke(zmq_socket_monitor, _socket, "inproc://monitor-client", ZMQ_EVENT_CLOSED | ZMQ_EVENT_CONNECTED | ZMQ_EVENT_DISCONNECTED).assertSuccess();
            _pollItems.push_back({ .socket = con._socket.zmq_ptr, .fd = 0, .events = ZMQ_POLLIN, .revents = 0 });
        }
        con._connectionState = ConnectionState::CONNECTED;
        return URI<RELAXED>("mdp://");
    }

    URI<RELAXED> endpoint() override {
        return URI<RELAXED>("mdp://");
    };

    detail::Connection &findConnection(const URI<RELAXED> &uri) {
        // TODO: use a map for more efficient lookup? how to handle multiple uris which resolve to the same host?
        auto con = std::find_if(_connections.begin(), _connections.end(), [&uri](detail::Connection &c) { return c._authority == uri.authority().value(); });
        if (con == _connections.end()) {
            auto newCon = detail::Connection(_context, uri.authority().value(), ZMQ_DEALER);
            connect(newCon);
            _connections.push_back(std::move(newCon));
            return _connections.back();
        }
        return *con;
    }

    void get(const URI<RELAXED> &uri) override {
        detail::Connection &con     = findConnection(uri);
        auto                message = createRequestTemplate(majordomo::Command::Get, uri.path().value());
        message.send(con._socket).assertSuccess();
        return;
    }

    void set(const URI<RELAXED> &uri, const std::span<const std::byte> &request) override {
        auto &con     = findConnection(uri);
        auto  message = createRequestTemplate(majordomo::Command::Set, uri.path().value());
        message.setBody(std::string(reinterpret_cast<const char *>(request.data()), request.size()), majordomo::MessageFrame::dynamic_bytes_tag{});
        message.send(con._socket).assertSuccess();
        return;
    }

    void subscribe(const URI<RELAXED> &uri) override {
        auto &con = findConnection(uri);
        fmt::print("Sub socket not available, falling back to mdp subscription.");
        auto message = createRequestTemplate(majordomo::Command::Subscribe, uri.path().value());
        message.send(con._socket).assertSuccess();
        return;
    }

    void unsubscribe(const URI<RELAXED> &uri) override {
        auto &con     = findConnection(uri);
        auto  message = createRequestTemplate(majordomo::Command::Unsubscribe, uri.path().value());
        message.send(con._socket).assertSuccess();
    }

    bool disconnect(detail::Connection &con) {
        _pollItems.erase(std::remove_if(_pollItems.begin(), _pollItems.end(), [&con](zmq_pollitem_t &pollitem) -> bool { return pollitem.socket == con._socket.zmq_ptr; }), _pollItems.end());
        zmq_invoke(zmq_disconnect, con._socket, majordomo::toZeroMQEndpoint(URI<STRICT>(con._authority)).data()).ignoreResult();
        con._connectionState = detail::ConnectionState::DISCONNECTED;
        return true;
    }

    bool handleMessage(majordomo::MdpMessage &&message, RawMessage &output) {
        if (!message.isValid()) {
            return true;
        }
        // subscription updates
        if (message.command() == majordomo::Command::Notify || message.command() == majordomo::Command::Final) {
            output.data.resize(message.body().size());
            URI<uri_check::RELAXED> uri{ std::string{ message.topic() } };
            // auto queryParamMap = uri.queryParamMap();
            std::memcpy(output.data.data(), message.body().begin(), message.body().size());
            output.context            = uri.queryParam().value_or("");
            output.endpoint           = std::make_unique<URI<uri_check::RELAXED>>(std::string{ message.topic() });
            output.timestamp_received = 0ms;
            return true;
        }
        return true;
    }

    bool read(RawMessage &msg) override {
        // const auto result = majordomo::zmq_invoke(zmq_poll, _pollItems.data(), static_cast<int>(_pollItems.size()), _clientTimeout.count()); // todo: polling is already done by the Publisher?
        // if (result.value() == 0) {
        //     return false;
        // }
        // todo:: more clever way to schedule connections and break out of the loop if there are heartbeats pending
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
    std::chrono::milliseconds housekeeping(const std::chrono::milliseconds &now) {
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
            // TODO handle client HEARTBEAT etc.
        }
        return _heartbeatInterval;
    }

private:
    majordomo::MdpMessage createRequestTemplate(majordomo::Command command, std::string_view serviceName) {
        auto req = majordomo::MdpMessage::createClientMessage(command);
        req.setServiceName(serviceName, majordomo::MessageFrame::dynamic_bytes_tag{});
        req.setClientRequestId(std::to_string(_nextRequestId++), majordomo::MessageFrame::dynamic_bytes_tag{});
        return req;
    }
};

class SubscriptionClient : public ClientBase {
    const std::chrono::milliseconds _clientTimeout;
    const majordomo::Context       &_context;
    // const majordomo::Socket       _monitorSocket;
    // std::vector<URI<uri_check::RELAXED>> _resolvedNames; // these get resolved by asking the /mmi endpoint of the supplied _authority or the default broker
    const std::string               _clientId;
    const std::string               _sourceName;
    std::chrono::milliseconds       _heartbeatInterval;
    std::vector<detail::Connection> _connections;
    std::vector<zmq_pollitem_t>     _pollItems;
    long                            _nextRequestId = 0;

public:
    explicit SubscriptionClient(const majordomo::Context &context,
            const std::chrono::milliseconds               timeout  = 1s,
            std::string                                   clientId = "")
        : _clientTimeout(timeout), _context(context),
        // _monitorSocket{ context, ZMQ_PAIR },
        _clientId(std::move(clientId))
        , _sourceName(fmt::format("OpenCmwClient(clientId: {})", _clientId))
        , _heartbeatInterval(timeout) {
        // setup socket monitor // todo: use client unique socket name, to avoid clash with other instances // disabled as it breaks the code
        // majordomo::zmq_invoke(zmq_connect, _monitorSocket, "inproc://monitor-client").assertSuccess();

        // take the minimum of the (albeit worker) heartbeat, client (if defined) or locally prescribed timeout
        std::chrono::milliseconds HEARTBEAT_INTERVAL(1000);
        _heartbeatInterval = std::chrono::milliseconds(std::min(HEARTBEAT_INTERVAL.count(), _clientTimeout == std::chrono::milliseconds(0) ? timeout.count() : std::min(timeout.count(), _clientTimeout.count())));
    }

    void connect(const URI<RELAXED> &uri) {
        // _connections.emplace_back(_context, std::string_view(uri.authority().value()));
        auto con = detail::Connection(_context, uri.authority().value(), ZMQ_SUB);
        _connections.push_back(std::move(con));
    }

    URI<RELAXED> connect(detail::Connection &con) {
        using detail::ConnectionState;
        // todo: replace this by proper logic, for now use tcp if port is specified, otherwise use inproc... mds+tcp:// -> tcp://, mds+inproc:// -> inproc:// -> auto detect
        auto        ep     = con._authority.find(':') != std::string::npos ? URI<STRICT>("tcp://"s + con._authority) : URI<STRICT>("inproc://"s + con._authority);
        std::string zmq_ep = majordomo::toZeroMQEndpoint(ep).data();
        if (opencmw::majordomo::zmq_invoke(zmq_connect, con._socket, zmq_ep).isValid()) {
            // zmq_invoke(zmq_socket_monitor, _socket, "inproc://monitor-client", ZMQ_EVENT_CLOSED | ZMQ_EVENT_CONNECTED | ZMQ_EVENT_DISCONNECTED).assertSuccess();
            _pollItems.push_back({ .socket = con._socket.zmq_ptr, .fd = 0, .events = ZMQ_POLLIN, .revents = 0 });
            con._connectionState = ConnectionState::CONNECTED;
        }
        return URI<RELAXED>("mds://");
    }

    URI<RELAXED> endpoint() override {
        return URI<RELAXED>("mds://");
    };

    detail::Connection &findConnection(const URI<RELAXED> &uri) {
        // TODO: use a map for more efficient lookup? how to handle multiple uris which resolve to the same host?
        auto con = std::find_if(_connections.begin(), _connections.end(), [&uri](detail::Connection &c) { return c._authority == uri.authority().value(); });
        if (con == _connections.end()) {
            auto newCon = detail::Connection(_context, uri.authority().value(), ZMQ_SUB);
            connect(newCon);
            _connections.push_back(std::move(newCon));
            return _connections.back();
        }
        return *con;
    }

    void get(const URI<RELAXED> & /*uri*/) override {
        throw std::logic_error("get not implemented");
    }

    void set(const URI<RELAXED> & /*uri*/, const std::span<const std::byte> & /*request*/) override {
        throw std::logic_error("get not implemented");
    }

    void subscribe(const URI<RELAXED> &uri) override {
        auto            &con         = findConnection(uri);
        std::string_view serviceName = uri.path().value();
        serviceName.remove_prefix(std::min(serviceName.find_first_not_of("/"), serviceName.size()));
        assert(!serviceName.empty());
        opencmw::majordomo::zmq_invoke(zmq_setsockopt, con._socket, ZMQ_SUBSCRIBE, serviceName.data(), serviceName.size()).assertSuccess();
    }

    void unsubscribe(const URI<RELAXED> &uri) override {
        auto            &con         = findConnection(uri);
        std::string_view serviceName = uri.path().value();
        serviceName.remove_prefix(std::min(serviceName.find_first_not_of("/"), serviceName.size()));
        assert(!serviceName.empty());
        opencmw::majordomo::zmq_invoke(zmq_setsockopt, con._socket, ZMQ_UNSUBSCRIBE, serviceName.data(), serviceName.size()).assertSuccess();
    }

    bool disconnect(detail::Connection &con) {
        _pollItems.erase(std::remove_if(_pollItems.begin(), _pollItems.end(), [&con](zmq_pollitem_t &pollitem) -> bool { return pollitem.socket == con._socket.zmq_ptr; }), _pollItems.end());
        zmq_invoke(zmq_disconnect, con._socket, majordomo::toZeroMQEndpoint(URI<STRICT>(con._authority)).data()).ignoreResult();
        con._connectionState = detail::ConnectionState::DISCONNECTED;
        return true;
    }

    bool handleMessage(majordomo::BasicMdpMessage<MessageFormat::WithSourceId> &&message, RawMessage &output) {
        if (!message.isValid()) {
            return true;
        }
        // subscription updates
        if (message.command() == majordomo::Command::Notify || message.command() == majordomo::Command::Final) {
            output.data.resize(message.body().size());
            std::memcpy(output.data.data(), message.body().begin(), message.body().size());
            output.context            = "";
            output.endpoint           = std::make_unique<URI<uri_check::RELAXED>>(std::string{ message.topic() });
            output.timestamp_received = 0ms;
            return true;
        }
        return true;
    }

    bool read(RawMessage &msg) override {
        // const auto result = majordomo::zmq_invoke(zmq_poll, _pollItems.data(), static_cast<int>(_pollItems.size()), _clientTimeout.count()); // todo: polling is already done by the Publisher?
        // if (result.value() == 0) {
        //     return false;
        // }
        // todo:: more clever way to schedule connections and break out of the loop if there are heartbeats pending
        for (detail::Connection &con : _connections) {
            if (con._connectionState != detail::ConnectionState::CONNECTED) {
                continue;
            }
            while (true) {
                if (auto message = majordomo::BasicMdpMessage<MessageFormat::WithSourceId>::receive(con._socket)) {
                    return handleMessage(std::move(*message), msg);
                } else {
                    break;
                }
            }
        }
        return false;
    }

    // method to be called in regular time intervals to send and verify heartbeats
    std::chrono::milliseconds housekeeping(const std::chrono::milliseconds &now) {
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
            // TODO handle client HEARTBEAT etc.
        }
        return _heartbeatInterval;
    }
};

} // namespace opencmw::client
#endif // OPENCMW_CPP_CMW_CLIENT_HPP
