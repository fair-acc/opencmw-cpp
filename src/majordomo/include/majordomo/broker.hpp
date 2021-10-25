#ifndef BROKER_H
#define BROKER_H

#include <atomic>
#include <optional>
#include <string>

#include <majordomo/Message.hpp>
#include <yaz/yaz.hpp>

#include "utils.hpp"

using namespace std::string_literals;

namespace Majordomo::OpenCMW {

/*constexpr*/ std::string SCHEME_TCP                 = "tcp";
/*constexpr*/ std::string SCHEME_INPROC              = "inproc";
/*constexpr*/ std::string SUFFIX_ROUTER              = "/router";
/*constexpr*/ std::string SUFFIX_PUBLISHER           = "/publisher";
/*constexpr*/ std::string SUFFIX_SUBSCRIBE           = "/subscribe";
/*constexpr*/ std::string INPROC_BROKER              = "inproc://broker";
/*constexpr*/ std::string INTERNAL_ADDRESS_BROKER    = INPROC_BROKER + SUFFIX_ROUTER;
/*constexpr*/ std::string INTERNAL_ADDRESS_PUBLISHER = INPROC_BROKER + SUFFIX_PUBLISHER;
/*constexpr*/ std::string INTERNAL_ADDRESS_SUBSCRIBE = INPROC_BROKER + SUFFIX_SUBSCRIBE;

constexpr int             HIGH_WATER_MARK            = 0;
constexpr int             HEARTBEAT_LIVENESS         = 3;
constexpr int             HEARTBEAT_INTERVAL         = 1000;

using Majordomo::OpenCMW::MdpMessage;

template<typename Message, typename Handler>
class BaseSocket : public yaz::Socket<Message, Handler> {
public:
    using message_t = Message;

    explicit BaseSocket(yaz::Context &context, int type, Handler &&handler)
        : yaz::Socket<Message, Handler>(context, type, std::move(handler)) {
        // From setDefaultSocketParameters (io/opencmw/OpenCmwConstants.java)
        // TODO: Does not exist in zmq.h/hpp
        // socket.setHeartbeatContext(PROT_CLIENT.getData());

        bool result = this->set_hwm(HIGH_WATER_MARK)
                   && this->set_heartbeat_ttl(HEARTBEAT_INTERVAL * HEARTBEAT_LIVENESS)
                   && this->set_heartbeat_timeout(HEARTBEAT_INTERVAL * HEARTBEAT_LIVENESS)
                   && this->set_heartbeat_ivl(HEARTBEAT_INTERVAL)
                   && this->set_linger(HEARTBEAT_INTERVAL);

        assert(result);
    }
};

template<typename Handler>
class RouterSocket : public BaseSocket<MdpMessage, Handler> {
public:
    explicit RouterSocket(yaz::Context &context, Handler &&handler)
        : BaseSocket<MdpMessage, Handler>(context, ZMQ_ROUTER, std::move(handler)) {
        debug() << "Creating an instance of RouterSocket\n";
        this->bind(INTERNAL_ADDRESS_BROKER);
    }
};

template<typename Handler>
class SubSocket : public BaseSocket<MdpMessage, Handler> {
public:
    explicit SubSocket(yaz::Context &context, Handler &&handler)
        : BaseSocket<MdpMessage, Handler>(context, ZMQ_XPUB, std::move(handler)) {
        this->bind(INTERNAL_ADDRESS_SUBSCRIBE);
    }
};

template<typename Handler>
class DnsSocket : public BaseSocket<MdpMessage, Handler> {
public:
    explicit DnsSocket(yaz::Context &context, Handler &&handler)
        : BaseSocket<MdpMessage, Handler>(context, ZMQ_DEALER, std::move(handler)) {
    }
};

template<typename Handler>
class PubSocket : public BaseSocket<yaz::Message, Handler> {
public:
    explicit PubSocket(yaz::Context &context, Handler &&handler)
        : BaseSocket<yaz::Message, Handler>(context, ZMQ_XPUB, std::move(handler)) {
        auto result = this->set_xpub_verbose(true);
        assert(result);
        this->bind(INTERNAL_ADDRESS_PUBLISHER);
    }
};

class Broker {
private:
    static constexpr std::string_view SUFFIX_ROUTER    = "/router";
    static constexpr std::string_view SUFFIX_PUBLISHER = "/publisher";
    static constexpr std::string_view SUFFIX_SUBSCRIBE = "/subscribe";
    static constexpr std::string_view INPROC_BROKER    = "inproc://broker";

    yaz::Context                      _context;
    static std::atomic<int>           s_broker_counter;

    std::string                       _broker_name;
    std::string                       _dns_address;

    int                               _loopCount = 0;

    // Sockets collection. The Broker class will be used as the handler
    // yaz::SocketGroup<Broker *, RouterSocket> _sockets;
    yaz::SocketGroup<Broker *, RouterSocket, PubSocket, SubSocket, DnsSocket> _sockets;

    // Common
    void        purge_workers() {}
    void        process_clients() {}
    void        purge_clients() {}
    void        send_heartbeats() {}

    std::string replace_scheme(std::string_view address, std::string_view scheme_replacement) {
        auto scheme_end = address.find(':');
        if (utils::iequal(address.substr(0, scheme_end), SCHEME_INPROC)) {
            return std::string{ address };
        }

        std::string result;
        result.append(scheme_replacement);
        result.append(address.substr(scheme_end));
        return result;
    }

public:
    Broker(std::string broker_name, std::string dns_address)
        : _broker_name{ std::move(broker_name) }
        , _dns_address{ dns_address.empty() ? dns_address : replace_scheme(std::move(dns_address), SCHEME_TCP) }
        , _sockets(_context, this) {
        _sockets.get<DnsSocket>().connect(_dns_address.empty() ? INTERNAL_ADDRESS_BROKER : _dns_address);
    }

    template<typename Socket>
    requires yaz::meta::is_instantiation_of_v<PubSocket, Socket>
    bool receive_message(Socket &socket, bool /*wait*/) {
        // was receive plus handleSubscriptionMessage
        yaz::Message message = socket.receive();
        return false;
    }

    template<typename Socket>
    bool receive_message(Socket &socket, bool /*wait*/) {
        // was receive plus handleReceivedMessage
        MdpMessage message = socket.receive();
        return false;
    }

    bool continue_after_messages_read(bool anything_received) {
        process_clients();

        if (_loopCount == 0) {
            _loopCount = 10;
            purge_workers();
            purge_clients();
            send_heartbeats();
        } else {
            _loopCount--;
        }

        return anything_received; // TODO: && thread_not_interrupted && run
    }

    void run() {
        do {
            _loopCount = 0;
            _sockets.read();
        } while (true);
    }
};

} // namespace Majordomo::OpenCMW

#endif // BROKER_H
