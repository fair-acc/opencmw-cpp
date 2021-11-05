#ifndef BROKER_H
#define BROKER_H

#include <atomic>
#include <chrono>
#include <deque>
#include <optional>
#include <set>
#include <string>
#include <string_view>

#include <fmt/core.h>

#include <majordomo/Message.hpp>
#include <yaz/yaz.hpp>

#include "utils.hpp"

using namespace std::string_literals;

namespace Majordomo::OpenCMW {

/*constexpr*/ std::string SCHEME_TCP                 = "tcp";
/*constexpr*/ std::string SCHEME_MDP                 = "mdp";
/*constexpr*/ std::string SCHEME_MDS                 = "mds";
/*constexpr*/ std::string SCHEME_INPROC              = "inproc";
/*constexpr*/ std::string SUFFIX_ROUTER              = "/router";
/*constexpr*/ std::string SUFFIX_PUBLISHER           = "/publisher";
/*constexpr*/ std::string SUFFIX_SUBSCRIBE           = "/subscribe";
/*constexpr*/ std::string INPROC_BROKER              = "inproc://broker";
/*constexpr*/ std::string INTERNAL_ADDRESS_BROKER    = INPROC_BROKER + SUFFIX_ROUTER;
/*constexpr*/ std::string INTERNAL_ADDRESS_PUBLISHER = INPROC_BROKER + SUFFIX_PUBLISHER;
/*constexpr*/ std::string INTERNAL_ADDRESS_SUBSCRIBE = INPROC_BROKER + SUFFIX_SUBSCRIBE;
/*constexpr*/ std::string INTERNAL_SERVICE_NAMES     = "mmi.service";

constexpr int             HIGH_WATER_MARK            = 0;
constexpr int             HEARTBEAT_LIVENESS         = 3;
constexpr int             HEARTBEAT_INTERVAL         = 1000;
constexpr auto            CLIENT_TIMEOUT             = std::chrono::seconds(10); // TODO

using BrokerMessage                                  = BasicMdpMessage<MessageFormat::WithSourceId>;

template<typename Message, typename Handler>
class BaseSocket : public yaz::Socket<Message, Handler> {
public:
    using message_t = Message;

    explicit BaseSocket(yaz::Context &context, int type, Handler &&handler)
        : yaz::Socket<Message, Handler>(context, type, std::move(handler)) {
        // From setDefaultSocketParameters (io/opencmw/OpenCmwConstants.java)
        // TODO: Does not exist in zmq.h/hpp
        // socket.setHeartbeatContext(PROT_CLIENT.getData());

        [[maybe_unused]] bool result = this->set_hwm(HIGH_WATER_MARK)
                   && this->set_heartbeat_ttl(HEARTBEAT_INTERVAL * HEARTBEAT_LIVENESS)
                   && this->set_heartbeat_timeout(HEARTBEAT_INTERVAL * HEARTBEAT_LIVENESS)
                   && this->set_heartbeat_ivl(HEARTBEAT_INTERVAL)
                   && this->set_linger(HEARTBEAT_INTERVAL);

        assert(result);
    }
};

template<typename Handler>
class RouterSocket : public BaseSocket<BrokerMessage, Handler> {
public:
    explicit RouterSocket(yaz::Context &context, Handler &&handler)
        : BaseSocket<BrokerMessage, Handler>(context, ZMQ_ROUTER, std::move(handler)) {
        this->bind(INTERNAL_ADDRESS_BROKER);
    }
};

template<typename Handler>
class SubSocket : public BaseSocket<BrokerMessage, Handler> {
public:
    explicit SubSocket(yaz::Context &context, Handler &&handler)
        : BaseSocket<BrokerMessage, Handler>(context, ZMQ_XSUB, std::move(handler)) {
        this->bind(INTERNAL_ADDRESS_SUBSCRIBE);
    }
};

template<typename Handler>
class DnsSocket : public BaseSocket<BrokerMessage, Handler> {
public:
    explicit DnsSocket(yaz::Context &context, Handler &&handler)
        : BaseSocket<BrokerMessage, Handler>(context, ZMQ_DEALER, std::move(handler)) {
    }
};

template<typename Handler>
class PubSocket : public BaseSocket<yaz::Message, Handler> {
public:
    explicit PubSocket(yaz::Context &context, Handler &&handler)
        : BaseSocket<yaz::Message, Handler>(context, ZMQ_XPUB, std::move(handler)) {
        [[maybe_unused]] auto result = this->set_xpub_verbose(true);
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

    using Timestamp                                    = std::chrono::time_point<std::chrono::steady_clock>;
    using SocketGroup                                  = yaz::SocketGroup<Broker *, RouterSocket, PubSocket, SubSocket, DnsSocket>;
    using SocketType                                   = yaz::Socket<BrokerMessage, Broker::SocketGroup *>;

    struct Client {
        SocketType            *socket;
        const std::string      id;
        std::deque<BrokerMessage> requests;

        explicit Client(SocketType *s, std::string id_)
            : socket(s)
            , id(std::move(id_)) {
        }

        Client(const Client &) = delete;
        Client operator=(const Client &c) = delete;
    };

    struct Worker {
        SocketType *socket;
        Timestamp   expiry;
        std::string id;
        std::string service_name;

        explicit Worker(SocketType *s, std::string id_, std::string service_name_)
            : socket(s)
            , id{ std::move(id_) }
            , service_name{ std::move(service_name_) } {
        }

        void update_expiry() {
            expiry = std::chrono::steady_clock::now() + CLIENT_TIMEOUT;
        }

        bool is_expired(auto now) {
            return now >= expiry;
        }
    };

    struct Service {
        std::string            name;
        std::string            description;
        std::deque<Worker *>   waiting;
        std::deque<BrokerMessage> requests;

        explicit Service(std::string name_, std::string description_)
            : name(std::move(name_))
            , description(std::move(description_)) {
        }

        void put_message(BrokerMessage &&message) {
            // TODO prioritise by RBAC role
            requests.emplace_back(std::move(message));
        }

        BrokerMessage take_next_message() {
            assert(!requests.empty());
            auto msg = std::move(requests.front());
            requests.pop_front();
            return msg;
        }

        Worker *take_next_worker() {
            assert(!waiting.empty());
            auto worker = waiting.front();
            waiting.pop_front();
            return worker;
        }
    };

    std::unordered_map<std::string, std::set<std::string>> _subscribed_clients_by_topic; // topic -> client IDs
    std::unordered_map<std::string, int>                   _subscribed_topics;           // topic -> subscription count
    std::unordered_map<std::string, Client>                _clients;
    std::unordered_map<std::string, Worker>                _workers;
    std::unordered_map<std::string, Service>               _services;

    static std::atomic<int>                                s_broker_counter;

    std::string                                            _broker_name;
    std::string                                            _dns_address;
    const std::string                                      _rbac               = "TODO (RBAC)";

    int                                                    _loopCount          = 0;
    std::atomic<bool>                                      _shutdown_requested = false;

    // Sockets collection. The Broker class will be used as the handler
    // yaz::SocketGroup<Broker *, RouterSocket> _sockets;
    SocketGroup _sockets;

    // Common
    auto &router_socket() {
        return _sockets.get<RouterSocket>();
    }

    auto &pub_socket() {
        return _sockets.get<PubSocket>();
    }

    auto &sub_socket() {
        return _sockets.get<SubSocket>();
    }

    Service &require_service(std::string service_name, std::string service_description) {
        // TODO handle service_description differing between workers? or is "first one wins" ok?
        auto it = _services.try_emplace(service_name, std::move(service_name), std::move(service_description));
        return it.first->second;
    }

    Service *best_matching_service(std::string_view service_name) {
        // TODO use some smart reactive filtering once available, maybe optimize or cache
        std::vector<Service *> services;
        services.reserve(_services.size());
        for (auto &it : _services) {
            services.push_back(&it.second);
        }

        auto does_not_start_with = [&service_name](auto service) {
            return !service->name.starts_with(service_name);
        };

        services.erase(std::remove_if(services.begin(), services.end(), does_not_start_with), services.end());

        if (services.empty())
            return nullptr;

        auto lessByLength = [](auto lhs, auto rhs) {
            if (lhs->name.size() == rhs->name.size())
                return lhs->name < rhs->name;
            return lhs->name.size() < rhs->name.size();
        };

        return *std::min_element(services.begin(), services.end(), lessByLength);
    }

    void dispatch(Service &service) {
        purge_workers();

        while (!service.waiting.empty() && !service.requests.empty()) {
            auto message = service.take_next_message();
            auto worker  = service.take_next_worker();
            assert(worker);
            message.setClientSourceId(message.sourceId(), MessagePart::dynamic_bytes_tag{});
            message.setSourceId(worker->id, MessagePart::dynamic_bytes_tag{});
            message.setProtocol(BrokerMessage::Protocol::Worker);
            // TODO assert that command exists in both protocols?
            worker->socket->send(std::move(message));
        }
    }

    void send_with_source_id(BrokerMessage &&message, std::string_view source_id) {
        message.setSourceId(source_id, MessagePart::dynamic_bytes_tag{});
        _sockets.get<RouterSocket>().send(std::move(message));
    }

    static bool matches_subscription_topic(std::string_view topic, std::string_view subscription_topic) {
        // TODO check what this actually is supposed to do
        return subscription_topic.starts_with(topic);
    }

    void dispatch_message_to_matching_subscribers(BrokerMessage &&message) {
        const auto               it                       = _subscribed_clients_by_topic.find(std::string(message.topic()));
        const auto               has_router_subscriptions = it != _subscribed_clients_by_topic.end();

        message.setSourceId(message.topic(), MessagePart::dynamic_bytes_tag{});
        std::vector<std::string> pubsub_subscriptions;
        for (const auto &topic_it : _subscribed_topics) {
            if (matches_subscription_topic(message.topic(), topic_it.first)) {
                pubsub_subscriptions.push_back(topic_it.first);
            }
        }

        for (std::size_t i = 0; i < pubsub_subscriptions.size(); ++i) {
            const auto is_last = !has_router_subscriptions && i + 1 == pubsub_subscriptions.size();
            auto copy = is_last ? std::move(message) : message.clone();
            pub_socket().send(std::move(copy));
        }

        if (has_router_subscriptions) {
            std::size_t sent = 0;
            for (const auto &client_id : it->second) {
                const auto is_last = sent + 1 == it->second.size();

                send_with_source_id(is_last ? std::move(message) : message.clone(), client_id);
                ++sent;
            }
        }
    }

    void worker_waiting(Worker &worker) {
        // Queue to broker and service waiting lists
        // TODO
        // waiting.addLast(worker);
        auto service = _services.find(worker.service_name);
        assert(service != _services.end());
        service->second.waiting.push_back(&worker);
        worker.update_expiry();
        dispatch(service->second);
    }

    void purge_workers() {}

    void process_clients() {
        for (auto &clientIt : _clients) {
            auto &client = clientIt.second;
            if (client.requests.empty())
                continue;

            auto client_message = std::move(client.requests.back());
            client.requests.pop_back();

            assert(client_message.isValid());

            if (auto service = best_matching_service(client_message.serviceName())) {
                service->put_message(std::move(client_message));
                dispatch(*service);
                return;
            }

            // not implemented -- reply according to Majordomo Management Interface (MMI) as defined in http://rfc.zeromq.org/spec:8

            auto           reply = BrokerMessage::createClientMessage(BrokerMessage::ClientCommand::Final);
            constexpr auto tag   = yaz::MessagePart::dynamic_bytes_tag{};
            reply.setSourceId(client_message.sourceId(), tag);
            reply.setClientSourceId(client_message.clientSourceId(), tag);
            reply.setClientRequestId(client_message.clientRequestId(), tag);
            reply.setServiceName(client_message.serviceName(), tag);
            reply.setTopic(INTERNAL_SERVICE_NAMES, tag);
            reply.setError(fmt::format("unknown service (error 501): '{}'", client_message.serviceName()), tag);
            reply.setRbac(_rbac, tag);

            client.socket->send(std::move(reply));
        }
    }

    void        purge_clients() {}
    void        send_heartbeats() {}

    std::string get_scheme(std::string_view address) {
        auto scheme_end = address.find(':');
        if (scheme_end == std::string_view::npos)
            return {};
        return std::string(address.substr(0, scheme_end));
    }

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

    template<typename Socket>
    void process_worker(Socket &socket, BrokerMessage &&message) {
        assert(message.isWorkerMessage());

        const auto service_name = std::string(message.serviceName());
        const auto worker_id    = std::string(message.sourceId());
        const auto known_worker = _workers.contains(worker_id);
        auto      &worker       = _workers.try_emplace(worker_id, &socket, worker_id, service_name).first->second;
        worker.update_expiry();

        switch (message.workerCommand()) {
        case BrokerMessage::WorkerCommand::Ready: {
            debug() << "log new local/external worker for service " << service_name << " - " << message << std::endl;
            std::ignore = require_service(service_name, std::string(message.body()));
            worker_waiting(worker);

            // notify potential listeners
            auto       notify      = BrokerMessage::createWorkerMessage(BrokerMessage::WorkerCommand::Notify);
            const auto dynamic_tag = yaz::MessagePart::dynamic_bytes_tag{};
            notify.setServiceName(INTERNAL_SERVICE_NAMES, dynamic_tag);
            notify.setTopic(INTERNAL_SERVICE_NAMES, dynamic_tag);
            notify.setClientRequestId(_broker_name, dynamic_tag);
            notify.setSourceId(std::string(INTERNAL_SERVICE_NAMES), dynamic_tag);
            pub_socket().send(std::move(notify));
            break;
        }
        case BrokerMessage::WorkerCommand::Disconnect:
            // delete_worker(worker); // TODO handle? also commented out in java impl
            break;
        case BrokerMessage::WorkerCommand::Partial:
        case BrokerMessage::WorkerCommand::Final: {
            if (known_worker) {
                const auto client_id = message.clientSourceId();
                auto       client    = _clients.find(std::string(client_id));
                if (client == _clients.end()) {
                    return; // drop if client unknown/disappeared
                }

                message.setSourceId(client_id, yaz::MessagePart::dynamic_bytes_tag{});
                message.setServiceName(worker.service_name, yaz::MessagePart::dynamic_bytes_tag{});
                const auto client_command = [](auto worker_cmd) {
                    switch (worker_cmd) {
                    case BrokerMessage::WorkerCommand::Partial:
                        return BrokerMessage::ClientCommand::Partial;
                    case BrokerMessage::WorkerCommand::Final:
                        return BrokerMessage::ClientCommand::Final;
                    default:
                        assert(!"unexpected command");
                        return BrokerMessage::ClientCommand::Final;
                    }
                }(message.workerCommand());

                message.setProtocol(BrokerMessage::Protocol::Client);
                message.setClientCommand(client_command);
                client->second.socket->send(std::move(message));
                worker_waiting(worker);
            } else {
                disconnect_worker(worker);
            }
            break;
        }
        case BrokerMessage::WorkerCommand::Notify: {
            message.setProtocol(BrokerMessage::Protocol::Client);
            message.setClientCommand(BrokerMessage::ClientCommand::Final);
            message.setSourceId(message.serviceName(), yaz::MessagePart::dynamic_bytes_tag{});
            message.setServiceName(worker.service_name, yaz::MessagePart::dynamic_bytes_tag{});

            dispatch_message_to_matching_subscribers(std::move(message));
            break;
        }
        default:
            break;
        }
    }

    void delete_worker(Worker &worker) {
        auto serviceIt = _services.find(worker.service_name);
        if (serviceIt != _services.end()) {
            auto &waiting = serviceIt->second.waiting;
            waiting.erase(std::remove(waiting.begin(), waiting.end(), &worker), waiting.end());
        }

        _workers.erase(worker.id);
    }

    void disconnect_worker(Worker &worker) {
        auto           disconnect  = BrokerMessage::createWorkerMessage(BrokerMessage::WorkerCommand::Disconnect);
        constexpr auto dynamic_tag = yaz::MessagePart::dynamic_bytes_tag{};
        disconnect.setSourceId(worker.id, dynamic_tag);
        disconnect.setServiceName(worker.service_name, dynamic_tag);
        disconnect.setTopic(worker.service_name, dynamic_tag);
        disconnect.setBody("broker shutdown", yaz::MessagePart::static_bytes_tag{});
        disconnect.setRbac(_rbac, dynamic_tag);
        worker.socket->send(std::move(disconnect));
        delete_worker(worker);
    }

public:
    Broker(std::string broker_name, std::string dns_address, yaz::Context &context)
        : _broker_name{ std::move(broker_name) }
        , _dns_address{ dns_address.empty() ? dns_address : replace_scheme(std::move(dns_address), SCHEME_TCP) }
        , _sockets(context, this) {
        _sockets.get<DnsSocket>().connect(_dns_address.empty() ? INTERNAL_ADDRESS_BROKER : _dns_address);
    }


    enum class BindOption {
        DetectFromURI, ///< detect from URI which socket is meant (@see bind)
        Router, ///< Always bind ROUTER socket
        Pub ///< Always bind PUB socket
    };
    /**
     * Bind broker to endpoint, can call this multiple times. We use a single
     * socket for both clients and workers.
     *
     * @param endpoint the URI-based 'scheme://ip:port' endpoint definition the server should listen to
     * The protocol definition
     *  - 'mdp://' corresponds to a SocketType.ROUTER socket
     *  - 'mds://' corresponds to a SocketType.XPUB socket
     *  - 'tcp://' internally falls back to 'mdp://' and ROUTER socket
     *  - 'inproc://' requires the socket type to be specified explicitly
     *
     * @return adjusted public address to use for clients/workers to connect
     */
    std::optional<std::string> bind(std::string_view endpoint, BindOption option = BindOption::DetectFromURI) {
        // TODO use result<std::string,Error> forwarding error details
        assert(!endpoint.empty());
        const auto requested_scheme = get_scheme(endpoint);
        assert(!(option == BindOption::DetectFromURI && requested_scheme == "inproc"));
        const auto is_router_socket = option != BindOption::Pub &&
                (option == BindOption::Router || requested_scheme.starts_with(SCHEME_MDP) || requested_scheme.starts_with(SCHEME_TCP));
        const auto result = [&] {
            const auto with_tcp = replace_scheme(endpoint, SCHEME_TCP);
            if (is_router_socket)
                return router_socket().bind(with_tcp);
            else
                return pub_socket().bind(with_tcp);
        }();

        if (!result) {
            debug() << fmt::format("Could not bind broker to '{}'\n", endpoint);
            return {};
        }

        const auto endpoint_adjusted = replace_scheme(endpoint, is_router_socket ? SCHEME_MDP : SCHEME_MDS);
        const auto adjusted_address_public = endpoint_adjusted; // TODO (java) resolveHost(endpointAdjusted, getLocalHostName());

        debug() << fmt::format("Majordomo broker/0.1 is active at '{}'\n", adjusted_address_public); // TODO do not hardcode version
        send_heartbeats(); // TODO check that ported correctly: sendDnsHeartbeats(true);
        return adjusted_address_public;
    }

    template<typename Socket>
    requires yaz::meta::is_instantiation_of_v<PubSocket, Socket>
    bool receive_message(Socket &socket, bool /*wait*/) {
        // was receive plus handleSubscriptionMessage

        auto message = socket.receive_parts();
        if (message.empty())
            return false;

        if (message.size() > 1) {
            debug() << "Unexpected message on pub socket (" << message.size() << " frames)" << std::endl;
            return false;
        }

        const auto data = message[0].data();

        if (data.size() < 2 || !(data[0] == '\x0' || data[0] == '\x1')) {
            debug() << "Unexpected subscribe/unsubscribe message: " << data << std::endl;
            return false;
        }

        const auto topic = std::string(data.substr(1));

        if (data[0] == '\x1') {
            auto it = _subscribed_topics.try_emplace(topic, 0);
            it.first->second++;
            if (it.first->second == 1)
                sub_socket().send(std::move(message));
        } else {
            auto it = _subscribed_topics.find(topic);
            if (it != _subscribed_topics.end()) {
                it->second--;
                if (it->second == 0) {
                    _subscribed_topics.erase(topic);
                    sub_socket().send(std::move(message));
                }
            }
        }

        return true;
    }

    template<typename Socket>
    bool receive_message(Socket &socket, bool /*wait*/) {
        // was receive plus handleReceivedMessage
        auto maybeMessage = socket.receive();

        if (!maybeMessage.has_value()) {
            return false;
        }

        auto message = std::move(*maybeMessage);

        if (!message.isValid()) {
            // TODO log properly, but not too verbose
            debug() << "Majordomo broker invalid message: " << message << std::endl;
            return false;
        }

        if (message.isClientMessage()) {
            switch (message.clientCommand()) {
            // TODO handle READY (client)?
            case BrokerMessage::ClientCommand::Subscribe: {
                auto it = _subscribed_clients_by_topic.try_emplace(std::string(message.topic()), std::set<std::string>{});
                // TODO check for duplicate subscriptions?
                it.first->second.emplace(message.sourceId());
                if (it.first->second.size() == 1) {
                    // TODO correct?
                    sub_socket().send(std::string("\x1") + std::string(message.topic()));
                }
                return true;
            }
            case BrokerMessage::ClientCommand::Unsubscribe: {
                auto it = _subscribed_clients_by_topic.find(std::string(message.topic()));
                if (it != _subscribed_clients_by_topic.end()) {
                    it->second.erase(std::string(message.sourceId()));
                    if (it->second.empty()) {
                        _subscribed_clients_by_topic.erase(it);
                        // TODO correct?
                        sub_socket().send(std::string("\x0") + std::string(message.topic()));
                    }
                }
                return true;
            }
            // TODO handle HEARTBEAT (client)?
            default:
                break;
            }

            const auto sender_id = std::string(message.sourceId());
            auto       client    = _clients.try_emplace(sender_id, &socket, sender_id);
            client.first->second.requests.emplace_back(std::move(message));

            return true;
        }

        assert(message.isWorkerMessage());
        process_worker(socket, std::move(message));
        return true;
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
            process_one_message();
        } while (!_shutdown_requested);

        cleanup();
    }

    void shutdown() {
        _shutdown_requested = true;
    }

    // test interface

    void process_one_message() {
        _loopCount = 0;
        _sockets.read();
    }

    void cleanup() {
        // iterate and delete workers (safe in >= C++14)
        auto it = _workers.begin();
        while (it != _workers.end()) {
            auto &worker = it->second;
            ++it;
            disconnect_worker(worker);
        }
    }
};

} // namespace Majordomo::OpenCMW

#endif // BROKER_H
