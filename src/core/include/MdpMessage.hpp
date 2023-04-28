#ifndef OPENCMW_CPP_MDPMESSAGE_HPP
#define OPENCMW_CPP_MDPMESSAGE_HPP

#include <IoBuffer.hpp>
#include <opencmw.hpp>
#include <URI.hpp>

#include <type_traits>
#include <variant>

namespace opencmw::mdp {

enum class MessageFormat {
    WithSourceId,   ///< 9-frame format, contains the source ID as frame 0, used with ROUTER sockets (broker)
    WithoutSourceId ///< 8-frame format, does not contain the source ID frame
};

enum class Command : unsigned char {
    Invalid     = 0x00,
    Get         = 0x01,
    Set         = 0x02,
    Partial     = 0x03,
    Final       = 0x04,
    Ready       = 0x05, ///< optional for client
    Disconnect  = 0x06, ///< optional for client
    Subscribe   = 0x07, ///< client-only
    Unsubscribe = 0x08, ///< client-only
    Notify      = 0x09, ///< worker-only
    Heartbeat   = 0x0a  ///< optional for client
};

constexpr auto clientProtocol     = std::string_view{ "MDPC03" };
constexpr auto workerProtocol     = std::string_view{ "MDPW03" };

/**
 * @brief Domain object representation of the Majordomo Protocol (MDP) message
 */
template<MessageFormat Format>
struct BasicMessage {
    using timePoint = std::chrono::time_point<std::chrono::system_clock>;
    using timeUnit  = std::chrono::milliseconds;
    using URI       = opencmw::URI<opencmw::uri_check::STRICT>;


    std::conditional_t<Format == MessageFormat::WithSourceId, std::string, std::monostate> sourceId = {};
    std::size_t id;
    timePoint   arrivalTime;                        // UTC time when the message was sent/received by the client
    timeUnit    timeout = std::chrono::seconds(10); // default request/reply timeout
    std::string protocolName;                       // unique protocol name including version (e.g. 'MDPC03' or 'MDPW03')
    Command     command = Command::Invalid;         // command type (GET, SET, SUBSCRIBE, UNSUBSCRIBE, PARTIAL, FINAL, NOTIFY, READY, DISCONNECT, HEARTBEAT)
    std::string serviceName{ "/" };                 // service endpoint name (normally the URI path only), or client source ID (for broker <-> worker messages)
    IoBuffer    clientRequestID;                    // stateful: worker mirrors clientRequestID; stateless: worker generates unique increasing IDs (to detect packet loss)
    URI         endpoint{ "/" };                    // URI containing at least <path> and optionally <query> parameters
    IoBuffer    data;                               // request/reply body -- opaque binary, e.g. YaS-, CmwLight-, JSON-, or HTML-based
    std::string error;                              // UTF-8 strings containing  error code and/or stack-trace (e.g. "404 Not Found")
    IoBuffer    rbac;                               // optional RBAC meta-info -- may contain token, role, signed message hash (implementation dependent)
};

using Message = BasicMessage<MessageFormat::WithoutSourceId>;

} // namespace opencmw::mdp

#endif // OPENCMW_CPP_MDPMESSAGE_HPP
