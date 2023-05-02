#ifndef OPENCMW_MAJORDOMO_UTILS_H
#define OPENCMW_MAJORDOMO_UTILS_H

#include <URI.hpp>
#include <ZmqPtr.hpp>

namespace opencmw::majordomo {

/**
 * Converts an address URI to the format expected by ZeroMQ, i.e. replace mds:/ and mdp:/ by tcp:/
 */
inline std::string toZeroMQEndpoint(const opencmw::URI<> &uri) {
    if (uri.scheme() == "mdp" || uri.scheme() == "mds") {
        return opencmw::URI<>::factory(uri).scheme("tcp").toString();
    }

    return uri.str();
}

inline zmq::Result<int> initializeZmqSocket(const zmq::Socket &sock, const majordomo::Settings &settings = {}) {
    const int heartbeatInterval = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(settings.heartbeatInterval).count());
    const int ttl               = heartbeatInterval * settings.heartbeatLiveness;
    const int hb_timeout        = heartbeatInterval * settings.heartbeatLiveness;
    return zmq::invoke(zmq_setsockopt, sock, ZMQ_SNDHWM, &settings.highWaterMark, sizeof(settings.highWaterMark))
        && zmq::invoke(zmq_setsockopt, sock, ZMQ_RCVHWM, &settings.highWaterMark, sizeof(settings.highWaterMark))
        && zmq::invoke(zmq_setsockopt, sock, ZMQ_HEARTBEAT_TTL, &ttl, sizeof(ttl))
        && zmq::invoke(zmq_setsockopt, sock, ZMQ_HEARTBEAT_TIMEOUT, &hb_timeout, sizeof(hb_timeout))
        && zmq::invoke(zmq_setsockopt, sock, ZMQ_HEARTBEAT_IVL, &heartbeatInterval, sizeof(heartbeatInterval))
        && zmq::invoke(zmq_setsockopt, sock, ZMQ_LINGER, &heartbeatInterval, sizeof(heartbeatInterval));
}

} // namespace opencmw::majordomo

#endif
