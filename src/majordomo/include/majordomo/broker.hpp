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
        this->bind(INTERNAL_ADDRESS_BROKER);
    }
};

template<typename Handler>
class SubSocket : public BaseSocket<MdpMessage, Handler> {
public:
    explicit SubSocket(yaz::Context &context, Handler &&handler)
        : BaseSocket<MdpMessage, Handler>(context, ZMQ_XSUB, std::move(handler)) {
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

    using Timestamp                                    = std::chrono::time_point<std::chrono::steady_clock>;
    using SocketGroup                                  = yaz::SocketGroup<Broker *, RouterSocket, PubSocket, SubSocket, DnsSocket>;
    using SocketType                                   = yaz::Socket<MdpMessage, Broker::SocketGroup *>;

    struct Client {
        SocketType            *socket;
        const std::string      id;
        std::deque<MdpMessage> requests;

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
        std::deque<MdpMessage> requests;

        explicit Service(std::string name_, std::string description_)
            : name(std::move(name_))
            , description(std::move(description_)) {
        }

        void put_message(MdpMessage &&message) {
            // TODO prioritise by RBAC role
            requests.emplace_back(std::move(message));
        }

        MdpMessage take_next_message() {
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
            return service->name.rfind(service_name, 0) != 0;
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
            message.setProtocol(MdpMessage::Protocol::Worker);
            // TODO assert that command exists in both protocols?
            worker->socket->send(std::move(message));
        }
    }

    void send_with_source_id(MdpMessage &&message, std::string_view source_id) {
        message.setSourceId(source_id, MessagePart::dynamic_bytes_tag{});
        _sockets.get<RouterSocket>().send(std::move(message));
    }

    static bool matches_subscription_topic(std::string_view topic, std::string_view subscription_topic) {
        // TODO check what this actually is supposed to do
        return subscription_topic.rfind(topic, 0) == 0;
    }

    void dispatch_message_to_matching_subscriber(MdpMessage &&message) {
        const auto               it                       = _subscribed_clients_by_topic.find(std::string(message.topic()));
        const auto               has_router_subscriptions = it != _subscribed_clients_by_topic.end();

        std::vector<std::string> pubsub_subscriptions;
        for (const auto &topic_it : _subscribed_topics) {
            if (matches_subscription_topic(message.topic(), topic_it.first)) {
                pubsub_subscriptions.push_back(topic_it.first);
            }
        }

        for (size_t i = 0; i < pubsub_subscriptions.size(); ++i) {
            const auto is_last = !has_router_subscriptions && i + 1 == pubsub_subscriptions.size();
            pub_socket().send_more(message.topic());
            pub_socket().send(is_last ? std::move(message) : message.clone());
        }

        if (has_router_subscriptions) {
            size_t sent = 0;
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

            auto           reply = MdpMessage::createClientMessage(MdpMessage::ClientCommand::Final);
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
    Broker(std::string broker_name, std::string dns_address, yaz::Context &context)
        : _broker_name{ std::move(broker_name) }
        , _dns_address{ dns_address.empty() ? dns_address : replace_scheme(std::move(dns_address), SCHEME_TCP) }
        , _sockets(context, this) {
        _sockets.get<DnsSocket>().connect(_dns_address.empty() ? INTERNAL_ADDRESS_BROKER : _dns_address);
    }

    template<typename Socket>
    requires yaz::meta::is_instantiation_of_v<PubSocket, Socket>
    bool receive_message(Socket &socket, bool /*wait*/) {
        // was receive plus handleSubscriptionMessage
        auto message = socket.receive();
        return false;
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
            case MdpMessage::ClientCommand::Subscribe: {
                auto it = _subscribed_clients_by_topic.try_emplace(std::string(message.topic()), std::set<std::string>{});
                // TODO check for duplicate subscriptions?
                it.first->second.emplace(message.sourceId());
                if (it.first->second.size() == 1) {
                    // TODO correct?
                    sub_socket().send(std::string("\x1") + std::string(message.topic()));
                }
                break;
            }
            case MdpMessage::ClientCommand::Unsubscribe: {
                auto it = _subscribed_clients_by_topic.find(std::string(message.topic()));
                if (it != _subscribed_clients_by_topic.end()) {
                    it->second.erase(std::string(message.sourceId()));
                    if (it->second.empty()) {
                        _subscribed_clients_by_topic.erase(it);
                        // TODO correct?
                        sub_socket().send(std::string("\x0") + std::string(message.topic()));
                    }
                }
                break;
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

    template<typename Socket>
    void process_worker(Socket &socket, MdpMessage &&message) {
        assert(message.isWorkerMessage());

        const auto service_name = std::string(message.serviceName());
        const auto worker_id    = std::string(message.sourceId());
        const auto known_worker = _workers.contains(worker_id);
        auto      &worker       = _workers.try_emplace(worker_id, &socket, worker_id, service_name).first->second;
        worker.update_expiry();

        switch (message.workerCommand()) {
        case MdpMessage::WorkerCommand::Ready: {
            debug() << "log new local/external worker for service " << service_name << " - " << message << std::endl;
            std::ignore = require_service(service_name, std::string(message.body()));
            worker_waiting(worker);

            // notify potential listeners
            auto       notify      = MdpMessage::createWorkerMessage(MdpMessage::WorkerCommand::Notify);
            const auto dynamic_tag = yaz::MessagePart::dynamic_bytes_tag{};
            notify.setServiceName(INTERNAL_SERVICE_NAMES, dynamic_tag);
            notify.setTopic(INTERNAL_SERVICE_NAMES, dynamic_tag);
            notify.setClientRequestId(_broker_name, dynamic_tag);

            pub_socket().send_more(std::string(INTERNAL_SERVICE_NAMES));
            pub_socket().send(std::move(notify));
            break;
        }

        case MdpMessage::WorkerCommand::Partial:
        case MdpMessage::WorkerCommand::Final: {
            if (known_worker) {
                const auto client_id = message.clientSourceId();
                auto       client    = _clients.find(std::string(client_id));
                if (client == _clients.end()) {
                    return; // drop if client unknown/disappeared
                }

                message.setSourceId(client_id, yaz::MessagePart::dynamic_bytes_tag{});
                message.setServiceName(worker.service_name, yaz::MessagePart::dynamic_bytes_tag{});
                message.setProtocol(MdpMessage::Protocol::Client);
                client->second.socket->send(std::move(message));
                worker_waiting(worker);
            } else {
                // TODO delete_worker(worker_id, true);
            }
            break;
        }
        case MdpMessage::WorkerCommand::Notify: {
            message.setProtocol(MdpMessage::Protocol::Client);
            message.setClientCommand(MdpMessage::ClientCommand::Final);
            message.setSourceId(message.serviceName(), yaz::MessagePart::dynamic_bytes_tag{});
            message.setServiceName(worker.service_name, yaz::MessagePart::dynamic_bytes_tag{});

            dispatch_message_to_matching_subscriber(std::move(message));
            break;
        }
        default:
            break;
        }
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
    }

    void shutdown() {
        _shutdown_requested = true;
    }

    // test interface

    void process_one_message() {
        _loopCount = 0;
        _sockets.read();
    }
};

} // namespace Majordomo::OpenCMW

#endif // BROKER_H
