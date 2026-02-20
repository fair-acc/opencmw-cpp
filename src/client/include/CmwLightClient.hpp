#ifndef OPENCMW_CPP_CMWLIGHTCLIENT_HPP
#define OPENCMW_CPP_CMWLIGHTCLIENT_HPP

#include <algorithm>
#include <chrono>
#include <format>
#include <string_view>

#include <ClientContext.hpp>
#include <IoSerialiserCmwLight.hpp>
#include <DirectoryLightClient.hpp>
#include <MdpMessage.hpp>
#include <opencmw.hpp>
#include <Topic.hpp>
#include <URI.hpp>
#include <zmq/ZmqUtils.hpp>

// TODO:
// - subscription filters: allow filters with different types by prefixing "int:" etc to the string map value
// - see if simplification is possible: now requests are first queued in a ring buffer and then dispatched to the corresponding client via local zeromq before the actual zeromq request is made
// - speed up initial setup by not having to wait multiple houskeeping timeouts

namespace opencmw::client::cmwlight {

struct Request {
    URI<>                               uri;
    std::function<void(mdp::Message &)> callback;
    timePoint                           timestamp_received = std::chrono::system_clock::now();
};

struct Subscription {
    URI<>                               uri;
    std::function<void(mdp::Message &)> callback;
    timePoint                           timestamp_received = std::chrono::system_clock::now();
    std::size_t                         reqId;
};

struct CmwLightHeaderOptions {
    int64_t                            b{}; // SOURCE_ID
    std::map<std::string, std::string> e; // SESSION_BODY
    // can potentially contain more and arbitrary data
    // accessors to make code more readable
    int64_t                            &sourceId() { return b; }
    std::map<std::string, std::string> &sessionBody() { return e; }
};

struct CmwLightHeader {
    int8_t                                 x_2{}; // REQ_TYPE_TAG
    int64_t                                x_0{}; // ID_TAG
    std::string                            x_1; // DEVICE_NAME
    std::string                            f;   // PROPERTY_NAME
    int8_t                                 x_7{}; // UPDATE_TYPE
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
    std::string                                                       x_8; // SELECTOR
    std::map<std::string, std::variant<bool, int, long, std::string>> c;   // FILTERS
    std::map<std::string, std::variant<bool, int, long, std::string>> x;   // DATA
    // accessors to make code more readable
    std::string                                                       &selector() { return x_8; };
    std::map<std::string, std::variant<bool, int, long, std::string>> &filters() { return c; }
    std::map<std::string, std::variant<bool, int, long, std::string>> &data() { return x; }
};

struct CmwLightDataContext {
    std::string                        x_4; // CYCLE_NAME
    int64_t                            x_6; // CYCLE_STAMP
    int64_t                            x_5; // ACQ_STAMP
    std::map<std::string, std::string> x;   // DATA // todo: support arbitrary filter data std::map<std::string, std::variant<std::string, int, long, bool>>
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
 * Sent as the first frame of an RDA3 message determining the type of message
 */
enum class MessageType : char {
    SERVER_CONNECT_ACK = 0x01,
    SERVER_REP         = 0x02,
    SERVER_HB          = 0x03,
    CLIENT_CONNECT     = 0x20,
    CLIENT_REQ         = 0x21,
    CLIENT_HB          = 0x22
};

/**
 * Frame Types in the descriptor (Last frame of a message containing the type of each sub message)
 */
enum class FrameType : char {
    HEADER               = 0,
    BODY                 = 1,
    BODY_DATA_CONTEXT    = 2,
    BODY_REQUEST_CONTEXT = 3,
    BODY_EXCEPTION       = 4
};

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
enum class UpdateType : char {
    NORMAL           = 0,
    FIRST_UPDATE     = 1,
    IMMEDIATE_UPDATE = 2
};

inline std::string getHostName() {
    std:: string hostname;
    hostname.resize(255);
    if (const int result = gethostname(hostname.data(), hostname.capacity()); !result) {
        hostname = "localhost";
    } else {
        hostname.resize(strnlen(hostname.data(), hostname.size()));
        hostname.shrink_to_fit();
    }
    return hostname;
}

inline std::string getIdentity() {
    std::string hostname = getHostName();
    static int CONNECTION_ID_GENERATOR = 1;
    static int channelIdGenerator      = 1; // todo: make this per connection
    return std::format("{}/{}/{}/{}", hostname, getpid(), ++CONNECTION_ID_GENERATOR, ++channelIdGenerator); // N.B. this scheme is parsed/enforced by CMW
}

inline std::string percentEncode(const std::string_view &str) {
    std::string result;
    result.reserve(str.size() * 3); // this is the upper bound if all characters have to be percent encoded, avoids reallocation
    for (const char c : str) {
        if (isalnum(c)) {
            result += c;
        } else {
            result += '%' + std::format("{:02X}", static_cast<unsigned char>(c));
        }
    }
    return result;
}

inline std::string createClientInfo() {
    const std::string hostname = getHostName();
    const char *usernamePtr = getlogin();
    const std::string username = usernamePtr ? usernamePtr : "unknown";
    const std::string processName = program_invocation_short_name;
    const std::string language = "cpp";
    const long startTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    const int pid = getpid();
    const std::string version = "2026.2.1";
    std::size_t applicationIdSize = 26 + processName.size() + username.size() + hostname.size() + std::to_string(pid).size();
    const std::string applicationId = std::format("app={};ver={};uid={};host={};pid={};", percentEncode(processName), percentEncode(version), percentEncode(username), percentEncode(hostname), std::to_string(pid));
    return "9" // number of fields
            + std::format("#Address:#string#{}#tcp:%2F%2F{}:0", hostname.size() + 8, percentEncode(hostname))
            + std::format("#ApplicationId:#string#{}#{}", applicationIdSize, applicationId)
            + std::format("#UserName:#string#{}#{}", username.size(), percentEncode(username))
            + std::format("#ProcessName:#string#{}#{}", processName.size(), percentEncode(processName))
            + std::format("#Language:#string#{}#{}", language.size(), percentEncode(language))
            + std::format("#StartTime:#long#{}", startTime)
            + std::format("#Name:#string#{}#{}", processName.size(), percentEncode(processName))
            + std::format("#Pid:#int#{}", pid)
            + std::format("#Version:#string#{}#{}", version.size(), percentEncode(version));
}

inline std::string createClientId() {
    const char *usernamePtr = getlogin();
    const std::string hostname = getHostName();
    const std::string username = usernamePtr ? usernamePtr : "unknown";
    const std::string processName = program_invocation_short_name;
    const int pid = getpid();
    const std::string version = "2026.2.1";
    const auto startTime = std::chrono::system_clock::now();
    return std::format("RemoteHostInfoImpl[name={}; userName={}; appId=[app={};ver={};uid={};host={};pid={};]; process={}; pid={}; address=tcp://{}:0; startTime={:%F %R%z}; connectionTime=About ago; version={}; language=CPP]1", processName, username, processName, version, username, hostname, pid, processName, pid, hostname, startTime, version);
}

struct PendingRequest {
    enum class RequestState {
        INITIALIZED,
        WAITING,
        FINISHED
    };
    std::string  reqId;
    IoBuffer     data{};
    RequestType  requestType{ RequestType::GET };
    RequestState state{ RequestState::INITIALIZED };
    std::string  uri;
};

struct OpenSubscription {
    enum class SubscriptionState {
        INITIALIZED,
        SUBSCRIBING,
        SUBSCRIBED,
        UNSUBSCRIBING,
        UNSUBSCRIBED
    };
    std::chrono::milliseconds             backOff = 20ms;
    long                                  updateId{};
    long                                  reqId = 0L;
    long                                  replyId{};
    SubscriptionState                     state = SubscriptionState::SUBSCRIBING;
    std::chrono::system_clock::time_point nextTry;
    std::string                           uri;
};

struct Connection {
    enum class ConnectionState {
        DISCONNECTED,
        NS_LOOKUP,
        CONNECTING1,
        CONNECTING2,
        CONNECTED,
    };
    std::string                             _deviceName;
    std::string                             _authority;
    zmq::Socket                             _socket;
    ConnectionState                         _connectionState               = ConnectionState::DISCONNECTED;
    timePoint                               _nextReconnectAttemptTimeStamp = std::chrono::system_clock::now();
    timePoint                               _lastHeartbeatReceived         = std::chrono::system_clock::now();
    timePoint                               _lastHeartBeatSent             = std::chrono::system_clock::now();
    std::chrono::milliseconds               _backoff                       = 20ms; // implements exponential back-off to get
    std::vector<zmq::MessageFrame>          _frames{};                             // currently received frames; will be accumulated until the message is complete
    std::map<std::string, OpenSubscription> _subscriptions;                        // all subscriptions requested for (un)subscribe
    int64_t                                 _subscriptionIdGenerator = 1;
    int64_t                                 _requestIdGenerator = 1;
    std::map<std::string, PendingRequest>   _pendingRequests;

    Connection(const zmq::Context &context, const std::string_view authority, const int zmq_dealer_type) : _authority{ authority }, _socket{ context, zmq_dealer_type } {
        zmq::initializeSocket(_socket).assertSuccess();
    }
};

static void send(const zmq::Socket &socket, const int param, std::string_view errorMsg, auto &&data) {
    if (zmq::MessageFrame connectFrame{ FWD(data) }; !connectFrame.send(socket, param).isValid()) {
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

inline void sendConnectRequest(Connection &con) {
    using namespace std::string_view_literals;
    detail::send(con._socket, ZMQ_SNDMORE, "error sending get frame"sv, static_cast<char>(MessageType::CLIENT_REQ));
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
    virtual ~CMWLightClientBase()                                                         = default;
    virtual bool      receive(mdp::Message &message)                                      = 0;
    virtual timePoint housekeeping(const timePoint &now)                                  = 0;
    virtual void      get(const URI<> &, std::string_view)                                = 0;
    virtual void      set(const URI<> &, std::string_view, const std::span<const char> &) = 0;
    virtual void      subscribe(const URI<> &, std::string_view)                          = 0;
    virtual void      unsubscribe(const URI<> &, std::string_view)                        = 0;
};

class CMWLightClient : public CMWLightClientBase {
    using timeUnit = std::chrono::milliseconds;
    const timeUnit                  _clientTimeout;
    const zmq::Context             &_context;
    DirectoryLightClient           &_directoryClient;
    const std::string               _clientId;
    const std::string               _sourceName;
    std::vector<detail::Connection> _connections;
    std::vector<zmq_pollitem_t>    &_pollItems;
    constexpr static auto           HEARTBEAT_INTERVAL = 2000ms;

public:
    explicit CMWLightClient(const zmq::Context &context,
            std::vector<zmq_pollitem_t>        &pollItems,
            DirectoryLightClient               &directoryClient,
            const timeUnit                      timeout  = 1s,
            std::string                         clientId = "")
        : _clientTimeout(timeout), _context(context), _directoryClient(directoryClient), _clientId(std::move(clientId)), _sourceName(std::format("CMWLightClient(clientId: {})", _clientId)), _pollItems(pollItems) {}

    void connect(detail::Connection &con) const {
        using enum detail::Connection::ConnectionState;
        using namespace std::string_view_literals;
        // todo: for now we expect rda3tcp://host:port, but this should allow be rda3:///devicename which will be looked up on the cmw directory server
        auto        endpoint = std::format("tcp://{}", con._authority);
        std::string id       = detail::getIdentity();
        if (!zmq::invoke(zmq_setsockopt, con._socket, ZMQ_IDENTITY, id.data(), id.size()).isValid()) { // hostname/process/id/channel -- the server verifies this
            throw std::runtime_error("failed set socket identity");
        }
        if (zmq::invoke(zmq_connect, con._socket, endpoint).isValid()) {
            _pollItems.push_back({ .socket = con._socket.zmq_ptr, .fd = 0, .events = ZMQ_POLLIN, .revents = 0 });
        }
        // send rda3 connect message
        detail::send(con._socket, ZMQ_SNDMORE, "error sending connect frame"sv, static_cast<char>(detail::MessageType::CLIENT_CONNECT));
        detail::send(con._socket, 0, "error sending connect frame"sv, "1.0.0");
        con._connectionState = CONNECTING1;
    }

    detail::Connection &findConnection(const URI<> &uri) {
        const std::string uriPath = uri.path().value();
        const auto deviceEndPos = uriPath.find('/', 1);
        const std::string_view deviceName = std::string_view{uriPath}.substr(1, (deviceEndPos == std::string_view::npos ? uriPath.size() : deviceEndPos) - 1);
        const auto con = std::ranges::find_if(_connections, [deviceName](const detail::Connection &c) { return c._deviceName == deviceName; });
        if (con == _connections.end()) {
            auto newCon = detail::Connection(_context, uri.authority().value(), ZMQ_DEALER);
            newCon._deviceName = deviceName;
            newCon._authority  = uri.authority().value_or("");
            _connections.push_back(std::move(newCon));
            return _connections.back();
        }
        return *con;
    }

    void get(const URI<> &uri, const std::string_view req_id) override {
        using namespace std::string_view_literals;
        using enum detail::FrameType;
        auto                  &con = findConnection(uri); // send message header
        detail::PendingRequest req{};
        req.reqId       = req_id;
        req.requestType = detail::RequestType::GET;
        req.state       = detail::PendingRequest::RequestState::INITIALIZED;
        req.uri         = uri.str();
        con._pendingRequests.insert({ std::string(req_id), std::move(req) });
    }

    void set(const URI<> &uri, const std::string_view req_id, const std::span<const char> &request) override {
        using namespace std::string_view_literals;
        using enum detail::FrameType;
        auto                  &con = findConnection(uri); // send message header
        detail::PendingRequest req{};
        req.reqId       = req_id;
        req.requestType = detail::RequestType::SET;
        req.data        = IoBuffer{ request.data(), request.size() };
        req.state       = detail::PendingRequest::RequestState::INITIALIZED;
        req.uri         = uri.str();
        con._pendingRequests.insert({ std::string(req_id), std::move(req) });
    }

    void subscribe(const URI<> &uri, const std::string_view req_id) override {
        using namespace std::string_view_literals;
        using enum detail::FrameType;
        auto                    &con = findConnection(uri);
        detail::OpenSubscription sub{};
        sub.state = detail::OpenSubscription::SubscriptionState::INITIALIZED;
        sub.uri   = uri.str();
        std::string req_id_string{ req_id };
        char       *req_id_end = req_id_string.data() + req_id_string.size();
        sub.reqId              = strtol(req_id_string.data(), &req_id_end, 10);
        con._subscriptions.insert({ std::string(req_id), std::move(sub) });
    }

    void unsubscribe(const URI<> &uri, const std::string_view req_id) override {
        using namespace std::string_view_literals;
        auto &con                                       = findConnection(uri);
        con._subscriptions[std::string{ req_id }].state = detail::OpenSubscription::SubscriptionState::UNSUBSCRIBING;
        CmwLightHeader header;
        header.requestType() = static_cast<int8_t>(detail::RequestType::UNSUBSCRIBE);
        std::string reqIdString{ req_id };
        char       *end = reqIdString.data() + req_id.size();
        header.id()     = std::strtol(req_id.data(), &end, 10);
        detail::send(con._socket, ZMQ_SNDMORE, "failed to send message header"sv, detail::serialiseCmwLight(header)); // send message header
        CmwLightRequestContext ctx;
        // send requestContext
        using enum detail::FrameType;
        detail::send(con._socket, 0, "failed to send descriptor frame"sv, descriptorToString(HEADER));
    }

    bool disconnect(detail::Connection &con) const {
#if not defined(__EMSCRIPTEN__) and (not defined(__clang__) or (__clang_major__ >= 16))
        const auto remove = std::ranges::remove_if(_pollItems, [&con](const zmq_pollitem_t &pollItem) { return pollItem.socket == con._socket.zmq_ptr; });
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
        if (con._frames.size() < 2 || con._frames.back().data().size() != con._frames.size() - 2 || static_cast<detail::FrameType>(con._frames.back().data()[0]) != detail::FrameType::HEADER) {
            throw std::runtime_error(std::format("received malformed response: wrong number of frames({}) or mismatch with frame descriptor({})", con._frames.size(), con._frames.back().size()));
        }
        // deserialise header frames[1]
        IoBuffer         data(con._frames[1].data().data(), con._frames[1].data().size());
        DeserialiserInfo info = checkHeaderInfo<CmwLight>(data, DeserialiserInfo{}, ProtocolCheck::LENIENT);
        CmwLightHeader   header;
        auto             result = opencmw::deserialise<CmwLight, ProtocolCheck::LENIENT>(data, header);

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
        switch (detail::RequestType{ static_cast<detail::RequestType>(header.requestType()) }) {
        case REPLY: {
            auto request = con._pendingRequests[std::format("{}", header.id())];
            // con._pendingRequests.erase(header.id());
            output.arrivalTime  = std::chrono::system_clock::now(); /// timePoint   < UTC time when the message was sent/received by the client
            output.command      = mdp::Command::Final;              /// Command     < command type (GET, SET, SUBSCRIBE, UNSUBSCRIBE, PARTIAL, FINAL, NOTIFY, READY, DISCONNECT, HEARTBEAT)
            char *end           = &request.reqId.back();
            output.id           = std::strtoul(request.reqId.data(), &end, 10);             /// std::size_t
            output.protocolName = "RDA3";                                                   /// std::string < unique protocol name including version (e.g. 'MDPC03' or 'MDPW03')
            std::string clientRequestID = std::format("{}", header.id());
            output.clientRequestID = IoBuffer{clientRequestID.data(), clientRequestID.size()};        /// IoBuffer    < stateful: worker mirrors clientRequestID; stateless: worker generates unique increasing IDs (to detect packet loss)
            output.topic           = URI{request.uri};                                                      /// URI         < URI containing at least <path> and optionally <query> parameters
            output.serviceName     = output.topic.path().value_or("/");                                   /// std::string < service endpoint name (normally the URI path only), or client source ID (for broker <-> worker messages)
            output.data  = IoBuffer{ con._frames[2].data().data(), con._frames[2].size() }; /// IoBuffer    < request/reply body -- opaque binary, e.g. YaS-, CmwLight-, JSON-, or HTML-based
            output.error = "";                                                              /// std::string < UTF-8 strings containing  error code and/or stack-trace (e.g. "404 Not Found")
            // output.rbac;                                         ///IoBuffer    < optional RBAC meta-info -- may contain token, role, signed message hash (implementation dependent)
            return true;
        }
        case EXCEPTION: {
            auto request = con._pendingRequests[std::format("{}", header.id())];
            // con._pendingRequests.erase(header.id());
            output.arrivalTime     = std::chrono::system_clock::now();                                /// timePoint   < UTC time when the message was sent/received by the client
            output.command         = mdp::Command::Final;                                             /// Command     < command type (GET, SET, SUBSCRIBE, UNSUBSCRIBE, PARTIAL, FINAL, NOTIFY, READY, DISCONNECT, HEARTBEAT)
            char *end              = &request.reqId.back();
            output.id              = std::strtoul(request.reqId.data(), &end, 10);             /// std::size_t
            output.protocolName    = "RDA3";                                                          /// std::string < unique protocol name including version (e.g. 'MDPC03' or 'MDPW03')
            std::string clientRequestID = std::format("{}", header.id());
            output.clientRequestID = IoBuffer{clientRequestID.data(), clientRequestID.size()};        /// IoBuffer    < stateful: worker mirrors clientRequestID; stateless: worker generates unique increasing IDs (to detect packet loss)
            output.topic           = URI{request.uri};                                                      /// URI         < URI containing at least <path> and optionally <query> parameters
            output.serviceName     = output.topic.path().value_or("/");                                   /// std::string < service endpoint name (normally the URI path only), or client source ID (for broker <-> worker messages)
            output.data            = IoBuffer{}; /// IoBuffer    < request/reply body -- opaque binary, e.g. YaS-, CmwLight-, JSON-, or HTML-based
            output.error           = std::string{con._frames[2].data().data(), con._frames[2].size()};                                                              /// std::string < UTF-8 strings containing  error code and/or stack-trace (e.g. "404 Not Found")
            return true;
        }
        case SUBSCRIBE: {
            auto &sub   = con._subscriptions[std::format("{}", header.id())];
            // todo: handle nonexisting subscription
            sub.replyId = header.options()->sourceId();
            sub.state   = detail::OpenSubscription::SubscriptionState::SUBSCRIBED;
            sub.backOff = 20ms; // reset back-off
            return false;
        }
        case UNSUBSCRIBE: {
            auto subscriptionForUnsub  = con._subscriptions.find(std::format("{}", header.id()));
            con._subscriptions.erase(subscriptionForUnsub);
            return false;
        }
        case NOTIFICATION_DATA: {
            if (auto sub = std::ranges::find_if(con._subscriptions, [&header](auto &pair) { return pair.second.replyId == header.id(); });sub == con._subscriptions.end()) {
                // std::println("received unexpected subscription for replyId: {}", header.id());
                // todo: add a temporary subscription to properly unsubscribe this untracked subscription?
                return false;
            } else {
                auto subscriptionForNotification = sub->second; // con._subscriptions[replyId];
                output.arrivalTime     = std::chrono::system_clock::now();                                /// timePoint   < UTC time when the message was sent/received by the client
                output.command         = mdp::Command::Notify;                                            /// Command     < command type (GET, SET, SUBSCRIBE, UNSUBSCRIBE, PARTIAL, FINAL, NOTIFY, READY, DISCONNECT, HEARTBEAT)
                output.id              = static_cast<size_t>(sub->second.replyId);                        /// std::size_t
                output.protocolName    = "RDA3";                                                          /// std::string < unique protocol name including version (e.g. 'MDPC03' or 'MDPW03')
                std::string clientRequestID = std::format("{}", subscriptionForNotification.reqId);
                output.clientRequestID = IoBuffer{clientRequestID.data(), clientRequestID.size()};        /// IoBuffer    < stateful: worker mirrors clientRequestID; stateless: worker generates unique increasing IDs (to detect packet loss)
                output.topic           = URI{subscriptionForNotification.uri};                            /// URI         < URI containing at least <path> and optionally <query> parameters
                output.serviceName     = output.topic.path().value_or("/");                             /// std::string < service endpoint name (normally the URI path only), or client source ID (for broker <-> worker messages)
                output.data            = IoBuffer{ con._frames[2].data().data(), con._frames[2].size() }; /// IoBuffer    < request/reply body -- opaque binary, e.g. YaS-, CmwLight-, JSON-, or HTML-based
                output.error           = "";                                                              /// std::string < UTF-8 strings containing  error code and/or stack-trace (e.g. "404 Not Found")
                return true;
            }
        }
        case NOTIFICATION_EXC: {
            std::string replyId;
            if (auto subIt = con._subscriptions.find(replyId); subIt == con._subscriptions.end()) {
                return false;
            } else {
                auto subscriptionForNotifyExc = subIt->second;
                output.arrivalTime            = std::chrono::system_clock::now();                                /// timePoint   < UTC time when the message was sent/received by the client
                output.command                = mdp::Command::Notify;                                            /// Command     < command type (GET, SET, SUBSCRIBE, UNSUBSCRIBE, PARTIAL, FINAL, NOTIFY, READY, DISCONNECT, HEARTBEAT)
                output.id                     = 0;                                                               /// std::size_t
                output.protocolName           = "RDA3";                                                          /// std::string < unique protocol name including version (e.g. 'MDPC03' or 'MDPW03')
                std::string clientRequestID = std::format("{}", subscriptionForNotifyExc.reqId);
                output.clientRequestID = IoBuffer{clientRequestID.data(), clientRequestID.size()};        /// IoBuffer    < stateful: worker mirrors clientRequestID; stateless: worker generates unique increasing IDs (to detect packet loss)
                output.topic                  = URI{subscriptionForNotifyExc.uri};                                                      /// URI         < URI containing at least <path> and optionally <query> parameters
                output.serviceName            = output.topic.path().value_or("/");                                   /// std::string < service endpoint name (normally the URI path only), or client source ID (for broker <-> worker messages)
                output.data            = IoBuffer{}; /// IoBuffer    < request/reply body -- opaque binary, e.g. YaS-, CmwLight-, JSON-, or HTML-based
                output.error           = std::string{con._frames[2].data().data(), con._frames[2].size()};                                                              /// std::string < UTF-8 strings containing  error code and/or stack-trace (e.g. "404 Not Found") // TODO: parse error message as cmwlight with fields "ContextAcqStamp", "ContextCycleStamp", "Message", "Type"
                return true;
            }
        }
        case SUBSCRIBE_EXCEPTION: {
            auto subForSubExc    = con._subscriptions[std::format("{}", header.id())];
            subForSubExc.state   = detail::OpenSubscription::SubscriptionState::UNSUBSCRIBED;
            subForSubExc.nextTry = currentTime + subForSubExc.backOff;
            subForSubExc.backOff *= 2;
            // exception during subscription, retrying
            output.arrivalTime     = std::chrono::system_clock::now();                                /// timePoint   < UTC time when the message was sent/received by the client
            output.command         = mdp::Command::Notify;                                            /// Command     < command type (GET, SET, SUBSCRIBE, UNSUBSCRIBE, PARTIAL, FINAL, NOTIFY, READY, DISCONNECT, HEARTBEAT)
            output.id              = static_cast<std::size_t>(subForSubExc.reqId);             /// std::size_t
            output.protocolName    = "RDA3";                                                          /// std::string < unique protocol name including version (e.g. 'MDPC03' or 'MDPW03')
            std::string clientRequestID = std::format("{}", subForSubExc.reqId);
            output.clientRequestID = IoBuffer{clientRequestID.data(), clientRequestID.size()};        /// IoBuffer    < stateful: worker mirrors clientRequestID; stateless: worker generates unique increasing IDs (to detect packet loss)
            output.clientRequestID = IoBuffer{clientRequestID.data(), clientRequestID.size()};        /// IoBuffer    < stateful: worker mirrors clientRequestID; stateless: worker generates unique increasing IDs (to detect packet loss)
            output.topic           = URI{subForSubExc.uri};                                                      /// URI         < URI containing at least <path> and optionally <query> parameters
            output.serviceName     = output.topic.path().value_or("/");                                   /// std::string < service endpoint name (normally the URI path only), or client source ID (for broker <-> worker messages)
            output.data            = IoBuffer{}; /// IoBuffer    < request/reply body -- opaque binary, e.g. YaS-, CmwLight-, JSON-, or HTML-based
            output.error           = std::string{con._frames[2].data().data(), con._frames[2].size()};                                                              /// std::string < UTF-8 strings containing  error code and/or stack-trace (e.g. "404 Not Found") // TODO: parse error message as cmwlight with fields "ContextAcqStamp", "ContextCycleStamp", "Message", "Type"
            return true;
        }
        case SESSION_CONFIRM: {
            return false;
        }
        // These request types should never be returned by the server or are unsupported by this implementation
        case GET:
        case SET:
        case CONNECT:
        case EVENT:
        default:
            // std::println("unsupported message: {}", header.requestType());
            return false;
        }
    }

    static bool handleMessage(mdp::Message &output, detail::Connection &con) {
        assert(!con._frames.empty() && "this function can only be ever called with at least one frame");
        const auto currentTime = std::chrono::system_clock::now();
        using enum detail::MessageType;
        using enum detail::Connection::ConnectionState;
        switch (static_cast<detail::MessageType>(con._frames[0].data().at(0))) {
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
                // std::println("received a heart-beat message on an unconnected connection!");
                return false;
            }
            con._lastHeartbeatReceived = currentTime;
            break;
        case SERVER_REP:
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
                if (const auto byteCountResultId = frame.receive(con._socket, ZMQ_DONTWAIT); !byteCountResultId.isValid() || byteCountResultId.value() < 1) {
                    break;
                }
                con._frames.push_back(std::move(frame));
                int64_t more;
                size_t  moreSize = sizeof(more);
                if (!zmq::invoke(zmq_getsockopt, con._socket, ZMQ_RCVMORE, &more, &moreSize)) {
                    throw std::runtime_error("error checking rcvmore");
                } else if (more == 0) { // the message is complete
                    const bool received = handleMessage(output, con);
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
                        if (con._authority.empty()) {
                            con._connectionState = ConnectionState::NS_LOOKUP;
                        } else {
                            connect(con);
                        }
                    }
                    break;
                case ConnectionState::NS_LOOKUP: {
                    auto deviceName = con._deviceName;
                    if (auto address = _directoryClient.lookup(deviceName); address.has_value()) {
                        con._authority = address.value();
                        connect(con);
                    }
                    break;
                }
                case ConnectionState::CONNECTING1:
                case ConnectionState::CONNECTING2: {
                    if (con._nextReconnectAttemptTimeStamp + _clientTimeout < now) {
                        // abort this connection attempt and start a new one
                    }
                    break;
                }
                case ConnectionState::CONNECTED:
                    for (auto &req : con._pendingRequests | std::views::values) {
                        using enum detail::RequestType;
                        if (req.state == RequestState::INITIALIZED) {
                            if (req.requestType == GET && !req.reqId.empty()) {
                                detail::send(con._socket, ZMQ_SNDMORE, "error sending get frame"sv, static_cast<char>(detail::MessageType::CLIENT_REQ) );
                                CmwLightHeader msg;
                                msg.requestType() = static_cast<int8_t>(GET);
                                char *reqIdEnd    = req.reqId.data() + req.reqId.size();
                                msg.id()          = std::strtol(req.reqId.data(), &reqIdEnd, 10);
                                msg.sessionId()   = detail::createClientId();
                                URI uri{ req.uri };
                                msg.device()   = uri.path()->substr(1, uri.path()->find('/', 1) - 1);
                                msg.property() = uri.path()->substr(uri.path()->find('/', 1) + 1);
                                msg.options()  = std::make_unique<CmwLightHeaderOptions>();
                                msg.updateType() = static_cast<uint8_t>(detail::UpdateType::NORMAL);
                                detail::send(con._socket, ZMQ_SNDMORE, "failed to send message header"sv, detail::serialiseCmwLight(msg)); // send message header
                                if (auto params = uri.queryParamMap(); params.contains("ctx")) {
                                    CmwLightRequestContext ctx;
                                    for (auto &[key, value] : params) {
                                        if (key == "ctx") {
                                            ctx.selector() = value.value_or("FAIR.SELECTOR.ALL");
                                        } else {
                                            ctx.filters().insert({key, value.value_or("")});
                                        }
                                    }
                                    IoBuffer buffer{};
                                    serialise<CmwLight>(buffer, ctx);
                                    detail::send(con._socket, ZMQ_SNDMORE, "failed to send context frame"sv, std::move(buffer)); // send requestContext
                                    detail::send(con._socket, 0, "failed to send descriptor frame"sv, descriptorToString(HEADER, BODY_REQUEST_CONTEXT));
                                } else {
                                    detail::send(con._socket, 0, "failed to send descriptor frame"sv, descriptorToString(HEADER));
                                }
                                req.state = RequestState::WAITING;
                            } else if (req.requestType == SET) {
                                detail::send(con._socket, ZMQ_SNDMORE, "error sending get frame"sv, static_cast<char>(detail::MessageType::CLIENT_REQ));
                                CmwLightHeader msg;
                                msg.requestType() = static_cast<int8_t>(detail::RequestType::GET);
                                char *reqIdEnd    = req.reqId.data() + req.reqId.size();
                                msg.id()          = std::strtol(req.reqId.data(), &reqIdEnd, 10);
                                msg.sessionId()   = detail::createClientId();
                                URI uri{ req.uri };
                                msg.device()   = uri.path()->substr(1, uri.path()->find('/', 1) - 1);
                                msg.property() = uri.path()->substr(uri.path()->find('/', 1) + 1);
                                msg.updateType() = static_cast<uint8_t>(detail::UpdateType::NORMAL);
                                detail::send(con._socket, ZMQ_SNDMORE, "failed to send message header"sv, detail::serialiseCmwLight(msg)); // send message header
                                if (auto params = uri.queryParamMap(); params.contains("ctx")) {
                                    CmwLightRequestContext ctx;
                                    ctx.selector() = params["ctx"].value_or("FAIR.SELECTOR.ALL");
                                    for (auto &[key, value] : params) {
                                        if (key == "ctx") continue;
                                        ctx.filters().insert({key, value.value_or("")});
                                    }
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
                    for (auto &sub : con._subscriptions | std::views::values) {
                        if (sub.state == INITIALIZED) {
                            detail::send(con._socket, ZMQ_SNDMORE, "error sending client req frame"sv, static_cast<char>(detail::MessageType::CLIENT_REQ));
                            URI   uri{ sub.uri };
                            CmwLightHeader header;
                            header.id()          = sub.reqId;
                            header.device()      = uri.path()->substr(1, uri.path()->find('/', 1) - 1);
                            header.property()    = uri.path()->substr(uri.path()->find('/', 1) + 1);
                            header.requestType() = static_cast<uint8_t>(detail::RequestType::SUBSCRIBE);
                            header.options() = std::make_unique<CmwLightHeaderOptions>();
                            header.sessionId()   = detail::createClientId();
                            detail::send(con._socket, ZMQ_SNDMORE, "failed to send message header"sv, detail::serialiseCmwLight(header)); // send message header
                            auto queryParams = uri.queryParamMap();
                            CmwLightRequestContext ctx;
                            for (auto & [key, value] : queryParams) {
                                if (key == "ctx") {
                                    ctx.selector() = queryParams.contains("ctx") ? queryParams["ctx"].value_or("") : "";
                                } else {
                                    ctx.filters().insert({key, value.value_or("")});
                                }
                            }
                            IoBuffer buffer{};
                            serialise<CmwLight>(buffer, ctx);
                            detail::send(con._socket, ZMQ_SNDMORE, "failed to send context frame"sv, std::move(buffer)); // send requestContext
                            detail::send(con._socket, 0, "failed to send descriptor frame"sv, descriptorToString(HEADER, BODY_REQUEST_CONTEXT));
                            con._lastHeartBeatSent = now;
                            sub.state              = SUBSCRIBING;
                        } else if (sub.state == UNSUBSCRIBING) {
                            // TODO resend unsubscribe request on timeout, forcefully remove subscription after retries
                        }
                    }
                    if (con._lastHeartBeatSent < now - HEARTBEAT_INTERVAL) {
                        detail::send(con._socket, 0, "error sending connect frame"sv, static_cast<char>(detail::MessageType::CLIENT_HB));
                        con._lastHeartBeatSent = now;
                    }
                    if (con._lastHeartbeatReceived < now - HEARTBEAT_INTERVAL * 3) {
                        std::println("Missed 3 heartbeats -> connection seems to be broken"); // todo correct error handling
                    }
                    break; // do nothing
            }
        }
        return now + _clientTimeout / 2;
    }
};

/*
 * Implementation of the Majordomo client protocol. Spawns a single thread which controls all client connections and sockets.
 * A dispatcher thread reads the requests from the command ring buffer and dispatches them to the zeromq poll loop using an inproc socket pair.
 * TODO: Q: merge with the mdp client? it basically uses the same loop and zeromq polling scheme.
 */
class CmwLightClientCtx : public ClientBase {
    using timeUnit = std::chrono::milliseconds;
    std::unordered_map<URI<>, std::unique_ptr<CMWLightClientBase>> _clients;
    const zmq::Context                                            &_zctx;
    DirectoryLightClient                                           _nameserver;
    zmq::Socket                                                    _control_socket_send;
    zmq::Socket                                                    _control_socket_recv;
    std::jthread                                                   _poller;
    std::vector<zmq_pollitem_t>                                    _pollitems{};
    std::unordered_map<std::size_t, Request>                       _requests;
    std::unordered_map<std::string, Subscription>                  _subscriptions;
    timeUnit                                                       _timeout;
    std::string                                                    _clientId;
    std::size_t                                                    _request_id = 1;

public:
    explicit CmwLightClientCtx(const zmq::Context &zeromq_context, std::string nameserver, const timeUnit timeout = 100ms, std::string clientId = "") // todo: also pass thread pool
        : _zctx{ zeromq_context }, _nameserver{DirectoryLightClient{std::move(nameserver)}}, _control_socket_send(zeromq_context, ZMQ_PAIR), _control_socket_recv(zeromq_context, ZMQ_PAIR), _timeout(timeout), _clientId(std::move(clientId)) {
        zmq::invoke(zmq_bind, _control_socket_send, "inproc://mdclientControlSocket").assertSuccess();
        _pollitems.push_back({ .socket = _control_socket_recv.zmq_ptr, .fd = 0, .events = ZMQ_POLLIN, .revents = 0 });
        _poller = std::jthread([this](const std::stop_token &stoken) { this->poll(stoken); });
    }

    std::vector<std::string> protocols() override {
        return { "rda3", "rda3tcp" }; // rda3 protocol, if transport is unspecified, tcp is used if authority contains a port
    }

    std::unique_ptr<CMWLightClientBase> &getClient(const URI<> &uri) {
        auto baseUri = URI<>::factory(uri).setQuery({}).path("").fragment("").build();
        if (_clients.contains(baseUri)) {
            return _clients.at(baseUri);
        }
        auto [it, ins] = _clients.emplace(baseUri, createClient(baseUri, _nameserver));
        if (!ins) {
            throw std::logic_error("could not insert client into client list");
        }
        return it->second;
    }

    std::unique_ptr<CMWLightClientBase> createClient(const URI<> &uri, DirectoryLightClient &nameserver) { // todo: cleanup, uri is not used
        return std::make_unique<CMWLightClient>(_zctx, _pollitems, nameserver, _timeout, _clientId);
    }

    void stop() override {
        _poller.request_stop();
        _poller.join();
    }

    void request(Command cmd) override {
        std::size_t req_id = 0;
        if (cmd.command == mdp::Command::Get || cmd.command == mdp::Command::Set) {
            req_id = _request_id++;
            _requests.insert({ req_id, Request{ .uri = cmd.topic, .callback = std::move(cmd.callback), .timestamp_received = cmd.arrivalTime } });
        } else if (cmd.command == mdp::Command::Subscribe) {
            req_id = _request_id++;
            std::string clientRequestId = std::format("{}", req_id);
            _subscriptions.insert({clientRequestId, Subscription{ .uri = cmd.topic, .callback = std::move(cmd.callback), .timestamp_received = cmd.arrivalTime, .reqId = req_id } });
        } else if (cmd.command == mdp::Command::Unsubscribe) {
            std::string clientRequestId{};
            for (auto &[reqId, sub]: _subscriptions) {
                if (sub.uri == cmd.topic) {
                    clientRequestId = reqId;
                    break;
                }
            }
            if (const auto subIt = _subscriptions.find(clientRequestId); subIt != _subscriptions.end()) {
                req_id = subIt->second.reqId;;
                _subscriptions.erase(subIt);
            };
        }
        sendCmd(cmd.topic, cmd.command, req_id, cmd.data);
    }

    DirectoryLightClient& nameserverClient() {
        return _nameserver;
    };

private:
    void sendCmd(const URI<> &uri, mdp::Command commandType, std::size_t req_id, IoBuffer data = {}) const {
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
            URI uri{ std::string(endpoint.data()) };
            const auto &client = getClient(uri);
            if (cmd.data().size() != 1) {
                throw std::logic_error("invalid request received: wrong number of frames");
            } else if (cmd.data()[0] == static_cast<char>(mdp::Command::Get)) {
                client->get(uri, reqId.data());
            } else if (cmd.data()[0] == static_cast<char>(mdp::Command::Set)) {
                zmq::MessageFrame data;
                if (!data.receive(_control_socket_recv, ZMQ_DONTWAIT).isValid()) {
                    throw std::logic_error("missing set data");
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
            for (const auto &client: _clients | std::views::values) {
                mdp::Message receivedEvent;
                while (client->receive(receivedEvent)) {
                    if (auto now = std::chrono::system_clock::now(); nextHousekeeping < now) { // perform housekeeping duties periodically
                        nextHousekeeping = housekeeping(now);
                    }
                    if (receivedEvent.command == mdp::Command::Invalid) { // Protocol internal messages, which require further receives but no publishing
                        continue;
                    }
                    std::string clientRequestId{reinterpret_cast<char*>(receivedEvent.clientRequestID.data()), receivedEvent.clientRequestID.size()};
                    if (receivedEvent.command == mdp::Command::Notify && _subscriptions.contains(clientRequestId)) {
                        _subscriptions.at(clientRequestId).callback(receivedEvent); // callback
                    }
                    if (receivedEvent.command == mdp::Command::Final && _requests.contains(receivedEvent.id)) {
                        _requests.at(receivedEvent.id).callback(receivedEvent); // callback
                        _requests.erase(receivedEvent.id);
                    }
                }
            }
        }
    }

    timePoint housekeeping(const timePoint now) const {
        timePoint next = now + _timeout;
        for (const auto &client: _clients | std::views::values) {
            next = std::min(next, client->housekeeping(now));
        }
        return next;
    }
};
} // namespace opencmw::client::cmwlight
#endif // OPENCMW_CPP_CMWLIGHTCLIENT_HPP
