#ifndef OPENCMW_CPP_CMWLIGHTCLIENT_HPP
#define OPENCMW_CPP_CMWLIGHTCLIENT_HPP

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <string_view>

#include <ClientContext.hpp>
#include <IoSerialiserCmwLight.hpp>
#include <MdpMessage.hpp>
#include <opencmw.hpp>
#include <Topic.hpp>
#include <URI.hpp>
#include <zmq/ZmqUtils.hpp>

namespace opencmw::client::cmwlight {

struct CmwLightHeaderOptions {
    int64_t                            b; // SOURCE_ID
    std::map<std::string, std::string> e;
    // can potentially contain more and arbitrary data
    // accessors to make code more readable
    int64_t                           &sourceId() { return b; }
    std::map<std::string, std::string> sessionBody;
};

struct CmwLightHeader {
    int8_t                                 x_2; // REQ_TYPE_TAG
    int64_t                                x_0; // ID_TAG
    std::string                            x_1; // DEVICE_NAME
    std::string                            f;   // PROPERTY_NAME
    int8_t                                 x_7; // UPDATE_TYPE
    std::string                            d;   // SESSION_ID
    std::unique_ptr<CmwLightHeaderOptions> x_3;
    // accessors to make code more readable
    int8_t                                 &requestType() { return x_2; }
    int64_t                                &id() { return x_0; }
    std::string                            &device() { return x_1; }
    std::string                            &property() { return f; }
    int8_t                                 &updateType() { return x_7; }
    std::string                            &sessionId() { return d; }
    std::unique_ptr<CmwLightHeaderOptions> &options() { return x_3; }
};
struct CmwLightConnectBody {
    std::string  x_9;
    std::string &clientInfo() { return x_9; }
};
struct CmwLightRequestContext {
    std::string                        x_8; // SELECTOR
    std::map<std::string, std::string> c;   // FILTERS // todo: support arbitrary filter data
    std::map<std::string, std::string> x;   // DATA // todo: support arbitrary filter data
    // accessors to make code more readable
    std::string                        &selector() { return x_8; };
    std::map<std::string, std::string> &filters() { return c; }
    std::map<std::string, std::string> &data() { return x; }
};
struct CmwLightDataContext {
    std::string                        x_4; // CYCLE_NAME
    int64_t                            x_6; // CYCLE_STAMP
    int64_t                            x_5; // ACQ_STAMP
    std::map<std::string, std::string> x;   // DATA // todo: support arbitrary filter data
    // accessors to make code more readable
    std::string                        &cycleName() { return x_4; }
    long                               &cycleStamp() { return x_6; }
    long                               &acqStamp() { return x_5; }
    std::map<std::string, std::string> &data() { return x; }
};

} // namespace opencmw::client::cmwlight
ENABLE_REFLECTION_FOR(opencmw::client::cmwlight::CmwLightHeaderOptions, b, e)
ENABLE_REFLECTION_FOR(opencmw::client::cmwlight::CmwLightHeader, x_2, x_0, x_1, f, x_7, d, x_3)
ENABLE_REFLECTION_FOR(opencmw::client::cmwlight::CmwLightConnectBody, x_9)
ENABLE_REFLECTION_FOR(opencmw::client::cmwlight::CmwLightRequestContext, x_8, c, x)
ENABLE_REFLECTION_FOR(opencmw::client::cmwlight::CmwLightDataContext, x_4, x_6, x_5, x)

namespace opencmw::client::cmwlight {
namespace detail {
/**
 * Sent as the first frame of an rda3 message determining the type of message
 */
enum class MessageType : char { SERVER_CONNECT_ACK = 0x01,
    SERVER_REP                                     = 0x02,
    SERVER_HB                                      = 0x03,
    CLIENT_CONNECT                                 = 0x20,
    CLIENT_REQ                                     = 0x21,
    CLIENT_HB                                      = 0x22 };

/**
 * Frame Types in the descriptor (Last frame of a message containing the type of each sub message)
 */
enum class FrameType : char { HEADER = 0,
    BODY                             = 1,
    BODY_DATA_CONTEXT                = 2,
    BODY_REQUEST_CONTEXT             = 3,
    BODY_EXCEPTION                   = 4 };

/*
 * Field names for the Request Header
static const std::map<std::string, std::string> FieldNames = {
    { "EVENT_TYPE_TAG", "eventType" },
    { "MESSAGE_TAG", "message" },
    { "ID_TAG", "0" },
    { "DEVICE_NAME_TAG", "1" },
    { "REQ_TYPE_TAG", "2" },
    { "OPTIONS_TAG", "3" },
    { "CYCLE_NAME_TAG", "4" },
    { "ACQ_STAMP_TAG", "5" },
    { "CYCLE_STAMP_TAG", "6" },
    { "UPDATE_TYPE_TAG", "7" },
    { "SELECTOR_TAG", "8" },
    { "CLIENT_INFO_TAG", "9" },
    { "NOTIFICATION_ID_TAG", "a" },
    { "SOURCE_ID_TAG", "b" },
    { "FILTERS_TAG", "c" },
    { "DATA_TAG", "x" },
    { "SESSION_ID_TAG", "d" },
    { "SESSION_BODY_TAG", "e" },
    { "PROPERTY_NAME_TAG", "f" }
};
*/

/**
 * request type used in request header REQ_TYPE_TAG
 */
enum class RequestType : char {
    GET                 = 0,
    SET                 = 1,
    CONNECT             = 2,
    REPLY               = 3,
    EXCEPTION           = 4,
    SUBSCRIBE           = 5,
    UNSUBSCRIBE         = 6,
    NOTIFICATION_DATA   = 7,
    NOTIFICATION_EXC    = 8,
    SUBSCRIBE_EXCEPTION = 9,
    EVENT               = 10,
    SESSION_CONFIRM     = 11
};

/**
 * UpdateType
 */
enum class UpdateType : char { NORMAL = 0,
    FIRST_UPDATE                      = 1,
    IMMEDIATE_UPDATE                  = 2 };

std::string getIdentity() {
    std::string hostname;
    hostname.resize(255);
    int result = gethostname(hostname.data(), hostname.capacity());
    if (!result) {
        hostname = "SYSPC008";
    } else {
        hostname.resize(strnlen(hostname.data(), hostname.size()));
        hostname.shrink_to_fit();
    }
    static int CONNECTION_ID_GENERATOR = 0;
    static int channelIdGenerator      = 0;                                                                 // todo: make this per connection
    return fmt::format("{}/{}/{}/{}", hostname, getpid(), ++CONNECTION_ID_GENERATOR, ++channelIdGenerator); // N.B. this scheme is parsed/enforced by CMW
}

std::string createClientInfo() {
    // todo insert correct data
    // return fmt::format("9#Address:#string#16#tcp:%2F%2FSYSPC008:0#ApplicationId:#string#69#app=fesa%2Dexplorer%2Dapp;ver=19%2E0%2E0;uid=akrimm;host=SYSPC008;pid=191616;#UserName:#string#6#akrimm#ProcessName:#string#8#cmwlight#Language:#string#3#cpp#StartTime:#long#1720084272252#Name:#string#15#cmwlightexample#Pid:#int#191616#Version:#string#6#10%2E3%2E0");
    return "9#Address:#string#16#tcp:%2F%2FSYSPC008:0#ApplicationId:#string#69#app=fesa%2Dexplorer%2Dapp;ver=19%2E0%2E0;uid=akrimm;host=SYSPC008;pid=191616;#UserName:#string#6#akrimm#ProcessName:#string#17#fesa%2Dexplorer%2Dapp#Language:#string#4#Java#StartTime:#long#1720084272252#Name:#string#17#fesa%2Dexplorer%2Dapp#Pid:#int#191616#Version:#string#6#10%2E3%2E0";
}

std::string createClientId() {
    return "RemoteHostInfoImpl[name=fesa-explorer-app; userName=akrimm; appId=[app=fesa-explorer-app;ver=19.0.0;uid=akrimm;host=SYSPC008;pid=191616;]; process=fesa-explorer-app; pid=191616; address=tcp://SYSPC008:0; startTime=2024-07-04 11:11:12; connectionTime=About ago; version=10.3.0; language=Java]1";
}

struct PendingRequest {
    enum class RequestState { INITIALIZED,
        WAITING,
        FINISHED };
    std::string       reqId{ "" };
    opencmw::IoBuffer data{};
    RequestType       requestType{ RequestType::GET };
    RequestState      state{ RequestState::INITIALIZED };
    std::string       uri{ "" };
};

struct OpenSubscription {
    enum class SubscriptionState { INITIALIZED,
        SUBSCRIBING,
        SUBSCRIBED,
        UNSUBSCRIBING,
        UNSUBSCRIBED };
    std::chrono::milliseconds             backOff = 20ms;
    long                                  updateId;
    long                                  reqId = 0L;
    std::string                           replyId;
    SubscriptionState                     state = SubscriptionState::SUBSCRIBING;
    std::chrono::system_clock::time_point nextTry;
    std::string                           uri;
};

struct Connection {
    enum class ConnectionState { DISCONNECTED,
        CONNECTING1,
        CONNECTING2,
        CONNECTED,
    };
    std::string                             _authority;
    zmq::Socket                             _socket;
    ConnectionState                         _connectionState               = ConnectionState::DISCONNECTED;
    timePoint                               _nextReconnectAttemptTimeStamp = std::chrono::system_clock::now();
    timePoint                               _lastHeartbeatReceived         = std::chrono::system_clock::now();
    timePoint                               _lastHeartBeatSent             = std::chrono::system_clock::now();
    std::chrono::milliseconds               _backoff                       = 20ms; // implements exponential back-off to get
    std::vector<zmq::MessageFrame>          _frames{};                             // currently received frames, will be accumulated until the message is complete
    std::map<std::string, OpenSubscription> _subscriptions;                        // all subscriptions requested for (un) subscribe
    int64_t                                 _subscriptionIdGenerator;
    int64_t                                 _requestIdGenerator;

    std::map<std::string, PendingRequest>   _pendingRequests;

    Connection(const zmq::Context &context, const std::string_view authority, const int zmq_dealer_type)
        : _authority{ authority }, _socket{ context, zmq_dealer_type } {
        zmq::initializeSocket(_socket).assertSuccess();
    }
};

static void send(const zmq::Socket &socket, int param, std::string_view errorMsg, auto &&data) {
    opencmw::zmq::MessageFrame connectFrame{ FWD(data) };
    if (!connectFrame.send(socket, param).isValid()) {
        throw std::runtime_error(errorMsg.data());
    }
}

static std::string descriptorToString(auto... descriptor) {
    std::string result{};
    result.reserve(sizeof...(descriptor));
    ((result.push_back(static_cast<char>(descriptor))), ...);
    return result;
}

static IoBuffer serialiseCmwLight(auto &requestType) {
    IoBuffer buffer{};
    opencmw::serialise<CmwLight, true>(buffer, requestType);
    buffer.reset();
    return buffer;
}

void sendConnectRequest(Connection &con) {
    using namespace std::string_view_literals;
    detail::send(con._socket, ZMQ_SNDMORE, "error sending get frame"sv, "\x21"); // 0x20 => detail::MessageType::CLIENT_REQ
    CmwLightHeader header;
    header.requestType() = static_cast<int8_t>(detail::RequestType::CONNECT);
    header.id()          = con._requestIdGenerator++;
    header.options()     = std::make_unique<CmwLightHeaderOptions>();
    send(con._socket, ZMQ_SNDMORE, "failed to send message header"sv, serialiseCmwLight(header)); // send message header
    CmwLightConnectBody connectBody;
    connectBody.clientInfo() = createClientInfo();
    send(con._socket, ZMQ_SNDMORE, "failed to send message header"sv, serialiseCmwLight(connectBody)); // send message header
    using enum detail::FrameType;
    send(con._socket, 0, "failed to send descriptor frame"sv, descriptorToString(HEADER, BODY));
}
} // namespace detail

class CMWLightClientBase {
public:
    virtual ~CMWLightClientBase()                                                               = default;
    virtual bool      receive(mdp::Message &message)                                            = 0;
    virtual timePoint housekeeping(const timePoint &now)                                        = 0;
    virtual void      get(const URI<STRICT> &, std::string_view)                                = 0;
    virtual void      set(const URI<STRICT> &, std::string_view, const std::span<const char> &) = 0;
    virtual void      subscribe(const URI<STRICT> &, std::string_view)                          = 0;
    virtual void      unsubscribe(const URI<STRICT> &, std::string_view)                        = 0;
};

class CMWLightClient : public CMWLightClientBase {
    using timeUnit = std::chrono::milliseconds;
    const timeUnit                  _clientTimeout;
    const zmq::Context             &_context;
    const std::string               _clientId;
    const std::string               _sourceName;
    std::vector<detail::Connection> _connections;
    std::vector<zmq_pollitem_t>    &_pollItems;
    constexpr static const auto     HEARTBEAT_INTERVAL = 2000ms;

public:
    explicit CMWLightClient(const zmq::Context &context,
            std::vector<zmq_pollitem_t>        &pollItems,
            const timeUnit                      timeout  = 1s,
            std::string                         clientId = "")
        : _clientTimeout(timeout), _context(context), _clientId(std::move(clientId)), _sourceName(fmt::format("CMWLightClient(clientId: {})", _clientId)), _pollItems(pollItems) {}

    void connect(const URI<STRICT> &uri) {
        auto con = detail::Connection(_context, uri.authority().value(), ZMQ_DEALER);
        _connections.push_back(std::move(con));
    }

    void connect(detail::Connection &con) {
        using enum detail::Connection::ConnectionState;
        using namespace std::string_view_literals;
        // todo: for now we expect rda3tcp://host:port, but this should allow be rda3://devicename which will be looked up on the cmw directory server
        auto        endpoint = fmt::format("tcp://{}", con._authority);
        std::string id       = detail::getIdentity();
        if (!zmq::invoke(zmq_setsockopt, con._socket, ZMQ_IDENTITY, id.data(), id.size()).isValid()) { // hostname/process/id/channel -- seems to be needed by CMW :-|
            fmt::print(stderr, "failed set socket identity");
        }
        if (opencmw::zmq::invoke(zmq_connect, con._socket, endpoint).isValid()) {
            _pollItems.push_back({ .socket = con._socket.zmq_ptr, .fd = 0, .events = ZMQ_POLLIN, .revents = 0 });
        }
        // send rda3 connect message
        detail::send(con._socket, ZMQ_SNDMORE, "error sending connect frame"sv, "\x20"); // 0x20 => detail::MessageType::CLIENT_CONNECT
        detail::send(con._socket, 0, "error sending connect frame"sv, "1.0.0");
        con._connectionState = CONNECTING1;
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

    void get(const URI<STRICT> &uri, std::string_view req_id) override {
        using namespace std::string_view_literals;
        using enum detail::FrameType;
        auto                  &con = findConnection(uri); // send message header
        std::string            key(req_id);
        detail::PendingRequest value{};
        value.reqId       = req_id;
        value.requestType = detail::RequestType::GET;
        value.state       = detail::PendingRequest::RequestState::INITIALIZED;
        value.uri         = uri.str();
        con._pendingRequests.insert({ fmt::format("{}", req_id), std::move(value) });
    }

    void set(const URI<STRICT> &uri, std::string_view req_id, const std::span<const char> &request) override {
        using namespace std::string_view_literals;
        using enum detail::FrameType;
        auto                  &con = findConnection(uri); // send message header
        detail::PendingRequest value{};
        value.reqId       = req_id;
        value.requestType = detail::RequestType::SET;
        value.data        = IoBuffer{ request.data(), request.size() };
        value.state       = detail::PendingRequest::RequestState::INITIALIZED;
        value.uri         = uri.str();
        con._pendingRequests.insert({ fmt::format("{}", req_id), std::move(value) });
    }

    void subscribe(const URI<STRICT> &uri, std::string_view req_id) override {
        using namespace std::string_view_literals;
        using enum detail::FrameType;
        auto                    &con = findConnection(uri);
        detail::OpenSubscription sub{};
        sub.state = detail::OpenSubscription::SubscriptionState::INITIALIZED;
        sub.uri   = uri.str();
        std::string req_id_string{ req_id };
        char       *req_id_end = req_id_string.data() + req_id_string.size();
        sub.reqId              = strtol(req_id_string.data(), &req_id_end, 10);
        con._subscriptions.insert({ fmt::format("{}", con._subscriptionIdGenerator++), std::move(sub) });
    }

    void unsubscribe(const URI<STRICT> &uri, std::string_view req_id) override {
        using namespace std::string_view_literals;
        auto &con                                       = findConnection(uri);
        con._subscriptions[std::string{ req_id }].state = detail::OpenSubscription::SubscriptionState::UNSUBSCRIBING;
        CmwLightHeader header;
        header.requestType() = static_cast<int8_t>(detail::RequestType::UNSUBSCRIBE);
        std::string reqIdString{ req_id };
        char       *end = reqIdString.data() + req_id.size();
        header.id()     = std::strtol(req_id.data(), &end, 10);
        // header.options() = {};
        // header.sessionId() = sessionId;
        // header.deviceName() = device;
        // header.propertyName() = property;
        // header.updateType() = updateType;
        detail::send(con._socket, ZMQ_SNDMORE, "failed to send message header"sv, detail::serialiseCmwLight(header)); // send message header
        CmwLightRequestContext ctx;
        // send requestContext
        using enum detail::FrameType;
        detail::send(con._socket, 0, "failed to send descriptor frame"sv, descriptorToString(HEADER));
    }

    bool disconnect(detail::Connection &con) {
#if not defined(__EMSCRIPTEN__) and (not defined(__clang__) or (__clang_major__ >= 16))
        const auto remove = std::ranges::remove_if(_pollItems, [&con](zmq_pollitem_t &pollItem) { return pollItem.socket == con._socket.zmq_ptr; });
        _pollItems.erase(remove.begin(), remove.end());
#else
        const auto remove = std::remove_if(_pollItems.begin(), _pollItems.end(), [&con](zmq_pollitem_t &pollItem) { return pollItem.socket == con._socket.zmq_ptr; });
        _pollItems.erase(remove, _pollItems.end());
#endif
        con._connectionState = detail::Connection::ConnectionState::DISCONNECTED;
        return true;
    }

    static bool handleServerReply(mdp::Message &output, detail::Connection &con, const auto currentTime) {
        using namespace std::string_view_literals;
        if (con._frames.size() < 2 || con._frames.back().data().size() != con._frames.size() - 2 || detail::FrameType(con._frames.back().data()[0]) != detail::FrameType::HEADER) {
            throw std::runtime_error(fmt::format("received malformed response: wrong number of frames({}) or mismatch with frame descriptor({})", con._frames.size(), con._frames.back().size()));
        }
        // deserialise header frames[1]
        IoBuffer         data(con._frames[1].data().data(), con._frames[1].data().size());
        DeserialiserInfo info = checkHeaderInfo<CmwLight>(data, DeserialiserInfo{}, ProtocolCheck::LENIENT);
        CmwLightHeader   header;
        auto             result = opencmw::deserialise<opencmw::CmwLight, ProtocolCheck::LENIENT>(data, header);

        if (con._connectionState == detail::Connection::ConnectionState::CONNECTING2) {
            if (header.requestType() == static_cast<uint8_t>(detail::RequestType::REPLY)) {
                con._connectionState       = detail::Connection::ConnectionState::CONNECTED;
                con._lastHeartbeatReceived = currentTime;
                return true;
            } else {
                throw std::runtime_error("expected connection reply but got different message");
            }
        }

        using enum detail::RequestType;
        switch (detail::RequestType{ header.requestType() }) {
        case REPLY: {
            // auto request = con._pendingRequests[fmt::format("{}", header.id())];
            // con._pendingRequests.erase(header.id());
            output.arrivalTime  = std::chrono::system_clock::now();                         /// timePoint   < UTC time when the message was sent/received by the client
            output.command      = opencmw::mdp::Command::Final;                             /// Command     < command type (GET, SET, SUBSCRIBE, UNSUBSCRIBE, PARTIAL, FINAL, NOTIFY, READY, DISCONNECT, HEARTBEAT)
            output.id           = 0;                                                        /// std::size_t
            output.protocolName = "RDA3";                                                   /// std::string < unique protocol name including version (e.g. 'MDPC03' or 'MDPW03')
            output.serviceName  = "/";                                                      /// std::string < service endpoint name (normally the URI path only), or client source ID (for broker <-> worker messages)
            output.clientRequestID;                                                         /// IoBuffer    < stateful: worker mirrors clientRequestID; stateless: worker generates unique increasing IDs (to detect packet loss)
            output.topic = URI{ "/" };                                                      /// URI         < URI containing at least <path> and optionally <query> parameters
            output.data  = IoBuffer{ con._frames[2].data().data(), con._frames[2].size() }; /// IoBuffer    < request/reply body -- opaque binary, e.g. YaS-, CmwLight-, JSON-, or HTML-based
            output.error = "";                                                              /// std::string < UTF-8 strings containing  error code and/or stack-trace (e.g. "404 Not Found")
            // output.rbac;                                         ///IoBuffer    < optional RBAC meta-info -- may contain token, role, signed message hash (implementation dependent)
            return true;
        }
        case EXCEPTION: {
            auto request = con._pendingRequests[fmt::format("{}", header.id())];
            // con._pendingRequests.erase(header.id());
            output.arrivalTime     = std::chrono::system_clock::now();                                /// timePoint   < UTC time when the message was sent/received by the client
            output.command         = opencmw::mdp::Command::Final;                                    /// Command     < command type (GET, SET, SUBSCRIBE, UNSUBSCRIBE, PARTIAL, FINAL, NOTIFY, READY, DISCONNECT, HEARTBEAT)
            output.id              = 0;                                                               /// std::size_t
            output.protocolName    = "RDA3";                                                          /// std::string < unique protocol name including version (e.g. 'MDPC03' or 'MDPW03')
            output.serviceName     = "/";                                                             /// std::string < service endpoint name (normally the URI path only), or client source ID (for broker <-> worker messages)
            output.clientRequestID = IoBuffer{};                                                      /// IoBuffer    < stateful: worker mirrors clientRequestID; stateless: worker generates unique increasing IDs (to detect packet loss)
            output.topic           = URI{ "/" };                                                      /// URI         < URI containing at least <path> and optionally <query> parameters
            output.data            = IoBuffer{ con._frames[2].data().data(), con._frames[2].size() }; /// IoBuffer    < request/reply body -- opaque binary, e.g. YaS-, CmwLight-, JSON-, or HTML-based
            output.error           = "";                                                              /// std::string < UTF-8 strings containing  error code and/or stack-trace (e.g. "404 Not Found")
            return true;
        }
        case SUBSCRIBE: {
            auto sub = con._subscriptions[fmt::format("{}", header.id())];
            // sub.replyId = header.options()->sourceId();
            sub.state   = detail::OpenSubscription::SubscriptionState::SUBSCRIBED;
            sub.backOff = 20ms; // reset back-off
            return false;
        }
        case UNSUBSCRIBE: {
            // successfully removed subscription
            auto subscriptionForUnsub  = con._subscriptions[fmt::format("{}", header.id())];
            subscriptionForUnsub.state = detail::OpenSubscription::SubscriptionState::UNSUBSCRIBED;
            // con._subscriptions.erase(subscriptionForUnsub.updateId);
            return false;
        }
        case NOTIFICATION_DATA: {
            std::string replyId;
            if (con._subscriptions.find(replyId) == con._subscriptions.end()) {
                return false;
            }
            auto subscriptionForNotification = con._subscriptions[replyId];
            // URI endpointForNotificationContext;
            // try {
            //     endpointForNotificationContext = new ParsedEndpoint(subscriptionForNotification.endpoint, reply.dataContext.cycleName).toURI();
            // } catch (URISyntaxException | CmwLightProtocol.RdaLightException e) {
            //     return false; // Error generating reply context URI
            // }
            output.arrivalTime     = std::chrono::system_clock::now();                                /// timePoint   < UTC time when the message was sent/received by the client
            output.command         = opencmw::mdp::Command::Notify;                                   /// Command     < command type (GET, SET, SUBSCRIBE, UNSUBSCRIBE, PARTIAL, FINAL, NOTIFY, READY, DISCONNECT, HEARTBEAT)
            output.id              = 0;                                                               /// std::size_t
            output.protocolName    = "RDA3";                                                          /// std::string < unique protocol name including version (e.g. 'MDPC03' or 'MDPW03')
            output.serviceName     = "/";                                                             /// std::string < service endpoint name (normally the URI path only), or client source ID (for broker <-> worker messages)
            output.clientRequestID = IoBuffer{};                                                      /// IoBuffer    < stateful: worker mirrors clientRequestID; stateless: worker generates unique increasing IDs (to detect packet loss)
            output.topic           = URI{ "/" };                                                      /// URI         < URI containing at least <path> and optionally <query> parameters
            output.data            = IoBuffer{ con._frames[2].data().data(), con._frames[2].size() }; /// IoBuffer    < request/reply body -- opaque binary, e.g. YaS-, CmwLight-, JSON-, or HTML-based
            output.error           = "";                                                              /// std::string < UTF-8 strings containing  error code and/or stack-trace (e.g. "404 Not Found")
            return true;
        }
        case NOTIFICATION_EXC: {
            std::string replyId;
            if (con._subscriptions.find(replyId) == con._subscriptions.end()) {
                return false;
            }
            auto subscriptionForNotifyExc = con._subscriptions[replyId];
            output.arrivalTime            = std::chrono::system_clock::now();                                /// timePoint   < UTC time when the message was sent/received by the client
            output.command                = opencmw::mdp::Command::Notify;                                   /// Command     < command type (GET, SET, SUBSCRIBE, UNSUBSCRIBE, PARTIAL, FINAL, NOTIFY, READY, DISCONNECT, HEARTBEAT)
            output.id                     = 0;                                                               /// std::size_t
            output.protocolName           = "RDA3";                                                          /// std::string < unique protocol name including version (e.g. 'MDPC03' or 'MDPW03')
            output.serviceName            = "/";                                                             /// std::string < service endpoint name (normally the URI path only), or client source ID (for broker <-> worker messages)
            output.clientRequestID        = IoBuffer{};                                                      /// IoBuffer    < stateful: worker mirrors clientRequestID; stateless: worker generates unique increasing IDs (to detect packet loss)
            output.topic                  = URI{ "/" };                                                      /// URI         < URI containing at least <path> and optionally <query> parameters
            output.data                   = IoBuffer{ con._frames[2].data().data(), con._frames[2].size() }; /// IoBuffer    < request/reply body -- opaque binary, e.g. YaS-, CmwLight-, JSON-, or HTML-based
            output.error                  = "";                                                              /// std::string < UTF-8 strings containing  error code and/or stack-trace (e.g. "404 Not Found")
            return true;
        }
        case SUBSCRIBE_EXCEPTION: {
            auto subForSubExc    = con._subscriptions[fmt::format("{}", header.id())];
            subForSubExc.state   = detail::OpenSubscription::SubscriptionState::UNSUBSCRIBED;
            subForSubExc.nextTry = currentTime + subForSubExc.backOff;
            subForSubExc.backOff *= 2;
            // exception during subscription, retrying
            output.arrivalTime     = std::chrono::system_clock::now();                                /// timePoint   < UTC time when the message was sent/received by the client
            output.command         = opencmw::mdp::Command::Notify;                                   /// Command     < command type (GET, SET, SUBSCRIBE, UNSUBSCRIBE, PARTIAL, FINAL, NOTIFY, READY, DISCONNECT, HEARTBEAT)
            output.id              = 0;                                                               /// std::size_t
            output.protocolName    = "RDA3";                                                          /// std::string < unique protocol name including version (e.g. 'MDPC03' or 'MDPW03')
            output.clientRequestID = IoBuffer{};                                                      /// IoBuffer    < stateful: worker mirrors clientRequestID; stateless: worker generates unique increasing IDs (to detect packet loss)
            output.topic           = URI{ "/" };                                                      /// URI         < URI containing at least <path> and optionally <query> parameters
            output.data            = IoBuffer{ con._frames[2].data().data(), con._frames[2].size() }; /// IoBuffer    < request/reply body -- opaque binary, e.g. YaS-, CmwLight-, JSON-, or HTML-based
            output.data            = IoBuffer{ con._frames[2].data().data(), con._frames[2].size() }; /// IoBuffer    < request/reply body -- opaque binary, e.g. YaS-, CmwLight-, JSON-, or HTML-based
            output.error           = "";                                                              /// std::string < UTF-8 strings containing  error code and/or stack-trace (e.g. "404 Not Found")
            return true;
        }
        // unsupported or non-actionable replies
        case GET:
        case SET:
        case CONNECT:
        case EVENT:
        case SESSION_CONFIRM:
        default:
            return false;
        }
    }

    static bool handleMessage(mdp::Message &output, detail::Connection &con) {
        assert(!con._frames.empty() && "this function can only be ever called with at least one frame");
        const auto currentTime = std::chrono::system_clock::now();
        using enum detail::MessageType;
        using enum detail::Connection::ConnectionState;
        switch (detail::MessageType(con._frames[0].data().at(0))) {
        case SERVER_CONNECT_ACK:
            if (con._connectionState == CONNECTING1) {
                if (con._frames.size() < 2 || con._frames[1].data().empty()) {
                    throw std::runtime_error("server connect does not contain required version info");
                }
                // verifyVersion(con._frames[1].data()); // todo: implement checking rda3 protocol version
                con._connectionState = CONNECTING2; // proceed to step 2 by sending the CLIENT_REQ, REQ_TYPE=CONNECT message
                sendConnectRequest(con);
                con._lastHeartbeatReceived = currentTime;
                con._backoff               = 20ms; // reset back-off time
            } else {
                throw std::runtime_error("received unsolicited SERVER_CONNECT_ACK");
            }
            break;
        case SERVER_HB:
            if (con._connectionState != CONNECTED && con._connectionState != CONNECTING2) {
                throw std::runtime_error("received a heart-beat message on an unconnected connection");
            }
            con._lastHeartbeatReceived = currentTime;
            break;
        case SERVER_REP:
            if (con._connectionState != CONNECTED && con._connectionState != CONNECTING2) {
                throw std::runtime_error("received an update on an unconnected connection");
            }
            con._lastHeartbeatReceived = currentTime;
            return handleServerReply(output, con, currentTime);
        case CLIENT_CONNECT:
        case CLIENT_REQ:
        case CLIENT_HB:
        default:
            throw std::runtime_error("Unexpected client message type received from server");
        }
        return false;
    }

    bool receive(mdp::Message &output) override {
        for (auto &con : _connections) {
            while (true) {
                zmq::MessageFrame frame;
                const auto        byteCountResultId = frame.receive(con._socket, ZMQ_DONTWAIT);
                if (!byteCountResultId.isValid() || byteCountResultId.value() < 1) {
                    fmt::print(".");
                    break;
                }
                fmt::print("+");
                con._frames.push_back(std::move(frame));
                int64_t more;
                size_t  moreSize = sizeof(more);
                if (!zmq::invoke(zmq_getsockopt, con._socket, ZMQ_RCVMORE, &more, &moreSize)) {
                    throw std::runtime_error("error checking rcvmore");
                } else if (more != 0) {
                    continue;
                } else {
                    fmt::print("\nhandleMessages({})", con._frames.size());
                    bool received = handleMessage(output, con);
                    con._frames.clear();
                    if (received) {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    // method to be called in regular time intervals to send and verify heartbeats
    timePoint housekeeping(const timePoint &now) override {
        using ConnectionState = detail::Connection::ConnectionState;
        using RequestState    = detail::PendingRequest::RequestState;
        using namespace std::literals;
        using enum detail::OpenSubscription::SubscriptionState;
        using enum detail::FrameType;
        // handle connection state
        for (auto &con : _connections) {
            switch (con._connectionState) {
            case ConnectionState::DISCONNECTED:
                if (con._nextReconnectAttemptTimeStamp <= now) {
                    connect(con);
                }
                break;
            case ConnectionState::CONNECTING1:
            case ConnectionState::CONNECTING2:
                if (con._nextReconnectAttemptTimeStamp + _clientTimeout < now) {
                    // abort connection attempt and start a new one
                }
                break;
            case ConnectionState::CONNECTED:
                for (auto &[id, req] : con._pendingRequests) {
                    using enum detail::RequestType;
                    if (req.state == RequestState::INITIALIZED) {
                        if (req.requestType == GET && !req.reqId.empty()) {
                            detail::send(con._socket, ZMQ_SNDMORE, "error sending get frame"sv, "\x21"); // 0x21 => detail::MessageType::CLIENT_REQ
                            CmwLightHeader msg;
                            msg.requestType() = static_cast<int8_t>(detail::RequestType::GET);
                            char *reqIdEnd    = req.reqId.data() + req.reqId.size();
                            msg.id()          = std::strtol(req.reqId.data(), &reqIdEnd, 10) + 1; // +1 to start with the identical requst id as the java impl
                            msg.sessionId()   = detail::createClientId();
                            URI<uri_check::STRICT> uri{ req.uri };
                            msg.device()   = uri.path()->substr(1, uri.path()->find('/', 1) - 1);
                            msg.property() = uri.path()->substr(uri.path()->find('/', 1) + 1);
                            msg.options()  = std::make_unique<CmwLightHeaderOptions>();
                            // msg.reqContext() = "";
                            msg.updateType() = static_cast<uint8_t>(detail::UpdateType::NORMAL);
                            detail::send(con._socket, ZMQ_SNDMORE, "failed to send message header"sv, detail::serialiseCmwLight(msg)); // send message header
                            bool hasRequestCtx = true;
                            if (hasRequestCtx) {
                                CmwLightRequestContext ctx;
                                ctx.selector() = ""; // todo: set correct ctx values
                                // ctx.data = {};
                                // ctx.filters = {"triggerName", "asdf"};
                                IoBuffer buffer{};
                                serialise<CmwLight>(buffer, ctx);
                                detail::send(con._socket, ZMQ_SNDMORE, "failed to send context frame"sv, std::move(buffer)); // send requestContext
                                detail::send(con._socket, 0, "failed to send descriptor frame"sv, descriptorToString(HEADER, BODY_REQUEST_CONTEXT));
                            } else {
                                detail::send(con._socket, 0, "failed to send descriptor frame"sv, descriptorToString(HEADER));
                            }
                            req.state = RequestState::WAITING;
                        } else if (req.requestType == SET) {
                            detail::send(con._socket, ZMQ_SNDMORE, "error sending get frame"sv, "\x21"); // 0x20 => detail::MessageType::CLIENT_REQ
                            CmwLightHeader msg;
                            msg.requestType() = static_cast<int8_t>(detail::RequestType::GET);
                            char *reqIdEnd    = req.reqId.data() + req.reqId.size();
                            msg.id()          = std::strtol(req.reqId.data(), &reqIdEnd, 10);
                            msg.sessionId()   = detail::createClientId();
                            URI<uri_check::STRICT> uri{ req.uri };
                            msg.device()   = uri.path()->substr(1, uri.path()->find('/', 1) - 1);
                            msg.property() = uri.path()->substr(uri.path()->find('/', 1) + 1);
                            // msg.reqContext() = "";
                            msg.updateType() = static_cast<uint8_t>(detail::UpdateType::NORMAL);
                            detail::send(con._socket, ZMQ_SNDMORE, "failed to send message header"sv, detail::serialiseCmwLight(msg)); // send message header
                            bool hasRequestCtx = false;
                            if (hasRequestCtx) {
                                CmwLightRequestContext ctx;
                                ctx.selector() = "asdf"; // todo: set correct ctx values
                                // ctx.data = {};
                                // ctx.filters = {"triggerName", "asdf"};
                                IoBuffer buffer{};
                                serialise<CmwLight>(buffer, ctx);
                                detail::send(con._socket, ZMQ_SNDMORE, "failed to send context frame"sv, std::move(buffer)); // send requestContext
                                detail::send(con._socket, ZMQ_SNDMORE, "failed to send data frame"sv, std::move(req.data));  // send requestContext
                                detail::send(con._socket, 0, "failed to send descriptor frame"sv, descriptorToString(HEADER, BODY_REQUEST_CONTEXT, BODY));
                            } else {
                                detail::send(con._socket, ZMQ_SNDMORE, "failed to send data frame"sv, std::move(req.data)); // send requestContext
                                detail::send(con._socket, 0, "failed to send descriptor frame"sv, descriptorToString(HEADER, BODY));
                            }
                            req.state = RequestState::WAITING;
                        }
                    }
                }
                for (auto &[id, sub] : con._subscriptions) {
                    if (sub.state == INITIALIZED) {
                        detail::send(con._socket, ZMQ_SNDMORE, "error sending get frame"sv, "\x21"); // 0x20 => detail::MessageType::CLIENT_REQ
                        opencmw::URI   uri{ sub.uri };
                        CmwLightHeader header;
                        header.device()      = uri.path()->substr(1, uri.path()->find('/', 1) - 1);
                        header.property()    = uri.path()->substr(uri.path()->find('/', 1) + 1);
                        header.requestType() = static_cast<uint8_t>(detail::RequestType::SUBSCRIBE);
                        header.sessionId()   = detail::createClientId();
                        detail::send(con._socket, ZMQ_SNDMORE, "failed to send message header"sv, detail::serialiseCmwLight(header)); // send message header
                        bool hasRequestCtx = true;
                        if (hasRequestCtx) {
                            CmwLightRequestContext ctx;
                            ctx.selector() = "";                                                                                   // todo: set correct ctx values
                            ctx.filters()  = { { "acquisitonModeFilter", "0" }, { "channelNameFilter", "GS01QS1F:Current@1Hz" } }; // todo: correct filters from query // acquisitino mode filter needs to be enum/int => needs map of variant
                            // ctx.data() = {};
                            IoBuffer buffer{};
                            serialise<CmwLight>(buffer, ctx);
                            detail::send(con._socket, ZMQ_SNDMORE, "failed to send context frame"sv, std::move(buffer)); // send requestContext
                            detail::send(con._socket, 0, "failed to send descriptor frame"sv, descriptorToString(HEADER, BODY_REQUEST_CONTEXT));
                        } else {
                            detail::send(con._socket, 0, "failed to send descriptor frame"sv, descriptorToString(HEADER));
                        }
                        sub.state = SUBSCRIBING;
                    } else if (sub.state == UNSUBSCRIBING) {
                    }
                }
                if (con._lastHeartBeatSent < now - HEARTBEAT_INTERVAL) {
                    detail::send(con._socket, 0, "error sending connect frame"sv, "\x22"); // 0x22 => detail::MessageType::CLIENT_HB
                    con._lastHeartBeatSent = now;
                }
                if (con._lastHeartbeatReceived < now - HEARTBEAT_INTERVAL * 3) {
                    fmt::print("Missed 3 heartbeats -> connection seems to be broken"); // todo correct error handling
                }
                break; // do nothing
            }
        }
        return now + _clientTimeout / 2;
    }

private:
};

/*
 * Implementation of the Majordomo client protocol. Spawns a single thread which controls all client connections and sockets.
 * A dispatcher thread reads the requests from the command ring buffer and dispatches them to the zeromq poll loop using an inproc socket pair.
 * TODO: Q: merge with the mdp client? it basically uses the same loop and zeromq polling scheme.
 */
class CmwLightClientCtx : public ClientBase {
    using timeUnit = std::chrono::milliseconds;
    std::unordered_map<URI<STRICT>, std::unique_ptr<CMWLightClientBase>> _clients;
    const zmq::Context                                                  &_zctx;
    zmq::Socket                                                          _control_socket_send;
    zmq::Socket                                                          _control_socket_recv;
    std::jthread                                                         _poller;
    std::vector<zmq_pollitem_t>                                          _pollitems{};
    std::unordered_map<std::size_t, Request>                             _requests;
    std::unordered_map<std::string, Subscription>                        _subscriptions;
    timeUnit                                                             _timeout;
    std::string                                                          _clientId;
    std::size_t                                                          _request_id = 0;

public:
    explicit CmwLightClientCtx(const zmq::Context &zeromq_context, const timeUnit timeout = 1s, std::string clientId = "") // todo: also pass thread pool
        : _zctx{ zeromq_context }, _control_socket_send(zeromq_context, ZMQ_PAIR), _control_socket_recv(zeromq_context, ZMQ_PAIR), _timeout(timeout), _clientId(std::move(clientId)) {
        _poller = std::jthread([this](const std::stop_token &stoken) { this->poll(stoken); });
        zmq::invoke(zmq_bind, _control_socket_send, "inproc://mdclientControlSocket").assertSuccess();
        _pollitems.push_back({ .socket = _control_socket_recv.zmq_ptr, .fd = 0, .events = ZMQ_POLLIN, .revents = 0 });
    }

    std::vector<std::string> protocols() override {
        return { "rda3", "rda3tcp" }; // rda3 protocol, if transport is unspecified, tcp is used if authority contains a port
    }

    std::unique_ptr<CMWLightClientBase> &getClient(const URI<STRICT> &uri) {
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

    std::unique_ptr<CMWLightClientBase> createClient(const URI<STRICT> &uri) {
        return std::make_unique<CMWLightClient>(_zctx, _pollitems, _timeout, _clientId);
    }

    void stop() override {
        _poller.request_stop();
        _poller.join();
    }

    void request(Command cmd) override {
        std::size_t req_id = 0;
        if (cmd.callback) {
            if (cmd.command == mdp::Command::Get || cmd.command == mdp::Command::Set) {
                req_id = _request_id++;
                _requests.insert({ req_id, Request{ .uri = cmd.topic, .callback = std::move(cmd.callback), .timestamp_received = cmd.arrivalTime } });
            } else if (cmd.command == mdp::Command::Subscribe) {
                req_id = _request_id++;
                _subscriptions.insert({ mdp::Topic::fromMdpTopic(cmd.topic).toZmqTopic(), Subscription{ .uri = cmd.topic, .callback = std::move(cmd.callback), .timestamp_received = cmd.arrivalTime } });
            } else if (cmd.command == mdp::Command::Unsubscribe) {
                _requests.erase(0); // todo: lookup correct subscription
            }
        }
        sendCmd(cmd.topic, cmd.command, req_id, cmd.data);
    }

private:
    void sendCmd(const URI<STRICT> &uri, mdp::Command commandType, std::size_t req_id, IoBuffer data = {}) const {
        const bool        isSet = commandType == mdp::Command::Set;
        zmq::MessageFrame cmdType{ std::string{ static_cast<char>(commandType) } };
        cmdType.send(_control_socket_send, ZMQ_SNDMORE).assertSuccess();
        zmq::MessageFrame reqId{ std::to_string(req_id) };
        reqId.send(_control_socket_send, ZMQ_SNDMORE).assertSuccess();
        zmq::MessageFrame endpoint{ std::string(uri.str()) };
        endpoint.send(_control_socket_send, isSet ? ZMQ_SNDMORE : 0).assertSuccess();
        if (isSet) {
            zmq::MessageFrame dataframe{ std::move(data) };
            dataframe.send(_control_socket_send, 0).assertSuccess();
        }
    }

    void handleRequests() {
        zmq::MessageFrame cmd;
        zmq::MessageFrame reqId;
        zmq::MessageFrame endpoint;
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
                client->get(uri, reqId.data());
            } else if (cmd.data()[0] == static_cast<char>(mdp::Command::Set)) {
                zmq::MessageFrame data;
                if (!data.receive(_control_socket_recv, ZMQ_DONTWAIT).isValid()) {
                    throw std::logic_error("missing set str");
                }
                client->set(uri, reqId.data(), std::span(data.data().data(), data.data().size()));
            } else if (cmd.data()[0] == static_cast<char>(mdp::Command::Subscribe)) {
                client->subscribe(uri, reqId.data());
            } else if (cmd.data()[0] == static_cast<char>(mdp::Command::Unsubscribe)) {
                client->unsubscribe(uri, reqId.data());
            } else {
                throw std::logic_error("invalid request received"); // messages always consist of 2 frames
            }
        }
    }

    void poll(const std::stop_token &stoken) {
        auto nextHousekeeping = std::chrono::system_clock::now();
        zmq::invoke(zmq_connect, _control_socket_recv, "inproc://mdclientControlSocket").assertSuccess();
        while (!stoken.stop_requested() && zmq::invoke(zmq_poll, _pollitems.data(), static_cast<int>(_pollitems.size()), 200)) {
            if (auto now = std::chrono::system_clock::now(); nextHousekeeping < now) {
                nextHousekeeping = housekeeping(now);
                // expire old subscriptions/requests/connections
            }
            handleRequests();
            for (const auto &[uri, client] : _clients) {
                mdp::Message receivedEvent;
                while (client->receive(receivedEvent)) {
                    if (_subscriptions.contains(receivedEvent.serviceName)) {
                        _subscriptions.at(receivedEvent.serviceName).callback(receivedEvent); // callback
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
} // namespace opencmw::client::cmwlight
#endif // OPENCMW_CPP_CMWLIGHTCLIENT_HPP
