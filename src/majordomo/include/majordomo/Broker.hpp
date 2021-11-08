#ifndef OPENCMW_MAJORDOMO_BROKER_H
#define OPENCMW_MAJORDOMO_BROKER_H

#include <atomic>
#include <chrono>
#include <deque>
#include <optional>
#include <set>
#include <string>
#include <string_view>

#include <fmt/core.h>

#include <majordomo/Message.hpp>
#include <majordomo/Utils.hpp>
#include <majordomo/ZmqPtr.hpp>

using namespace std::string_literals;

namespace opencmw::majordomo {

// TODO: Make constexpr as std::string is not yet constexpr
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

class Broker {
private:
    // static constexpr std::string_view SUFFIX_ROUTER    = "/router";
    // static constexpr std::string_view SUFFIX_PUBLISHER = "/publisher";
    // static constexpr std::string_view SUFFIX_SUBSCRIBE = "/subscribe";
    // static constexpr std::string_view INPROC_BROKER    = "inproc://broker";

    // Shorten chrono names
    using Clock     = std::chrono::steady_clock;
    using Timestamp = std::chrono::time_point<Clock>;

    struct Client {
        Socket                   &socket;
        const std::string         id;
        std::deque<BrokerMessage> requests;

        explicit Client(Socket &s, std::string id_)
            : socket(s)
            , id(std::move(id_)) {
        }

        Client(const Client &) = delete;
        Client operator=(const Client &c) = delete;
    };

    struct Worker {
        Socket     &socket;
        Timestamp   expiry;
        std::string id;
        std::string serviceName;

        explicit Worker(Socket &s, std::string id_, std::string serviceName_)
            : socket(s)
            , id{ std::move(id_) }
            , serviceName{ std::move(serviceName_) } {
        }

        void updateExpiry() {
            expiry = Clock::now() + CLIENT_TIMEOUT;
        }

        bool isExpired(auto now) {
            return now >= expiry;
        }
    };

    struct Service {
        std::string               name;
        std::string               description;
        std::deque<Worker *>      waiting;
        std::deque<BrokerMessage> requests;

        explicit Service(std::string name_, std::string description_)
            : name(std::move(name_))
            , description(std::move(description_)) {
        }

        void putMessage(BrokerMessage &&message) {
            // TODO prioritise by RBAC role
            requests.emplace_back(std::move(message));
        }

        BrokerMessage takeNextMessage() {
            assert(!requests.empty());
            auto msg = std::move(requests.front());
            requests.pop_front();
            return msg;
        }

        Worker *takeNextWorker() {
            assert(!waiting.empty());
            auto worker = waiting.front();
            waiting.pop_front();
            return worker;
        }
    };

    static constexpr std::string_view                      _rbac              = "TODO (RBAC)";
    std::unordered_map<std::string, std::set<std::string>> _subscribedClientsByTopic; // topic -> client IDs
    std::unordered_map<std::string, int>                   _subscribedTopics;         // topic -> subscription count
    std::unordered_map<std::string, Client>                _clients;
    std::unordered_map<std::string, Worker>                _workers;
    std::unordered_map<std::string, Service>               _services;

    std::string                                            _brokerName;
    std::string                                            _dnsAddress;

    int                                                    _loopCount         = 0;
    std::atomic<bool>                                      _shutdownRequested = false;

    // Sockets collection. The Broker class will be used as the handler
    Socket   _routerSocket;
    Socket   _pubSocket;
    Socket   _subSocket;
    Socket   _dnsSocket;

    Service &requireService(std::string serviceName, std::string serviceDescription) {
        // TODO handle serviceDescription differing between workers? or is "first one wins" ok?
        auto it = _services.try_emplace(serviceName, std::move(serviceName), std::move(serviceDescription));
        return it.first->second;
    }

    Service *bestMatchingService(std::string_view serviceName) {
        // TODO use some smart reactive filtering once available, maybe optimize or cache
        std::vector<Service *> services;
        services.reserve(_services.size());
        for (auto &it : _services) {
            services.push_back(&it.second);
        }

        auto doesNotStartWith = [&serviceName](auto service) {
            return !service->name.starts_with(serviceName);
        };

        services.erase(std::remove_if(services.begin(), services.end(), doesNotStartWith), services.end());

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
        purgeWorkers();

        while (!service.waiting.empty() && !service.requests.empty()) {
            auto message = service.takeNextMessage();
            auto worker  = service.takeNextWorker();
            assert(worker);
            message.setClientSourceId(message.sourceId(), MessageFrame::dynamic_bytes_tag{});
            message.setSourceId(worker->id, MessageFrame::dynamic_bytes_tag{});
            message.setProtocol(BrokerMessage::Protocol::Worker);
            // TODO assert that command exists in both protocols?
            message.send(worker->socket).assertSuccess();
        }
    }

    void sendWithSourceId(BrokerMessage &&message, std::string_view sourceId) {
        message.setSourceId(sourceId, MessageFrame::dynamic_bytes_tag{});
        message.send(_routerSocket).assertSuccess();
    }

    static bool matchesSubscriptionTopic(std::string_view topic, std::string_view subscriptionTopic) {
        // TODO check what this actually is supposed to do
        return subscriptionTopic.starts_with(topic);
    }

    void dispatchMessageToMatchingSubscribers(BrokerMessage &&message) {
        const auto it                     = _subscribedClientsByTopic.find(std::string(message.topic()));
        const auto hasRouterSubscriptions = it != _subscribedClientsByTopic.end();

        message.setSourceId(message.topic(), MessageFrame::dynamic_bytes_tag{});
        std::vector<std::string> pubsubSubscriptions;
        for (const auto &topicIt : _subscribedTopics) {
            if (matchesSubscriptionTopic(message.topic(), topicIt.first)) {
                pubsubSubscriptions.push_back(topicIt.first);
            }
        }

        for (std::size_t i = 0; i < pubsubSubscriptions.size(); ++i) {
            auto copy = message.clone();
            copy.send(_pubSocket).assertSuccess();
        }

        if (hasRouterSubscriptions) {
            std::size_t sent = 0;
            for (const auto &clientId : it->second) {
                sendWithSourceId(message.clone(), clientId);
                ++sent;
            }
        }
    }

    void workerWaiting(Worker &worker) {
        // Queue to broker and service waiting lists
        // TODO
        // waiting.addLast(worker);
        auto service = _services.find(worker.serviceName);
        assert(service != _services.end());
        service->second.waiting.push_back(&worker);
        worker.updateExpiry();
        dispatch(service->second);
    }

    void purgeWorkers() {}

    void processClients() {
        for (auto &clientIt : _clients) {
            auto &client = clientIt.second;
            if (client.requests.empty())
                continue;

            auto clientMessage = std::move(client.requests.back());
            client.requests.pop_back();

            assert(clientMessage.isValid());

            if (auto service = bestMatchingService(clientMessage.serviceName())) {
                service->putMessage(std::move(clientMessage));
                dispatch(*service);
                return;
            }

            // not implemented -- reply according to Majordomo Management Interface (MMI) as defined in http://rfc.zeromq.org/spec:8

            auto           reply       = std::move(clientMessage);
            constexpr auto dynamic_tag = MessageFrame::dynamic_bytes_tag{};
            constexpr auto static_tag = MessageFrame::static_bytes_tag{};
            reply.setClientCommand(BrokerMessage::ClientCommand::Final);
            reply.setTopic(INTERNAL_SERVICE_NAMES, static_tag);
            reply.setBody("", static_tag);
            reply.setError(fmt::format("unknown service (error 501): '{}'", reply.serviceName()), dynamic_tag);
            reply.setRbac(_rbac, static_tag);

            reply.send(client.socket).assertSuccess();
        }
    }

    void        purgeClients() {}
    void        sendHeartbeats() {}

    std::string getScheme(std::string_view address) {
        auto schemeEnd = address.find(':');
        if (schemeEnd == std::string_view::npos)
            return {};
        return std::string(address.substr(0, schemeEnd));
    }

    std::string replaceScheme(std::string_view address, std::string_view replacement) {
        auto schemeEnd = address.find(':');
        if (utils::iequal(address.substr(0, schemeEnd), SCHEME_INPROC)) {
            return std::string{ address };
        }

        std::string result;
        result.append(replacement);
        result.append(address.substr(schemeEnd));
        return result;
    }

    void processWorker(Socket &socket, BrokerMessage &&message) {
        assert(message.isWorkerMessage());

        const auto serviceName = std::string(message.serviceName());
        const auto serviceId   = std::string(message.sourceId());
        const auto knownWorker = _workers.contains(serviceId);
        auto      &worker      = _workers.try_emplace(serviceId, socket, serviceId, serviceName).first->second;
        worker.updateExpiry();

        switch (message.workerCommand()) {
        case BrokerMessage::WorkerCommand::Ready: {
            debug() << "log new local/external worker for service " << serviceName << " - " << message;
            std::ignore = requireService(serviceName, std::string(message.body()));
            workerWaiting(worker);

            // notify potential listeners
            auto       notify      = BrokerMessage::createWorkerMessage(BrokerMessage::WorkerCommand::Notify);
            const auto dynamic_tag = MessageFrame::dynamic_bytes_tag{};
            notify.setServiceName(INTERNAL_SERVICE_NAMES, dynamic_tag);
            notify.setTopic(INTERNAL_SERVICE_NAMES, dynamic_tag);
            notify.setClientRequestId(_brokerName, dynamic_tag);
            notify.setSourceId(INTERNAL_SERVICE_NAMES, dynamic_tag);
            notify.send(_pubSocket).assertSuccess();
            break;
        }
        case BrokerMessage::WorkerCommand::Disconnect:
            // deleteWorker(worker); // TODO handle? also commented out in java impl
            break;
        case BrokerMessage::WorkerCommand::Partial:
        case BrokerMessage::WorkerCommand::Final: {
            if (knownWorker) {
                auto clientId       = std::make_unique<std::string>(message.clientSourceId());
                auto       client   = _clients.find(*clientId);
                if (client == _clients.end()) {
                    return; // drop if client unknown/disappeared
                }

                message.setSourceId(clientId.release(), MessageFrame::dynamic_bytes_tag{});
                message.setServiceName(worker.serviceName, MessageFrame::dynamic_bytes_tag{});
                const auto clientCommand = [](auto workerCommand) {
                    switch (workerCommand) {
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
                message.setClientCommand(clientCommand);
                message.send(client->second.socket).assertSuccess();
                workerWaiting(worker);
            } else {
                disconnectWorker(worker);
            }
            break;
        }
        case BrokerMessage::WorkerCommand::Notify: {
            message.setProtocol(BrokerMessage::Protocol::Client);
            message.setClientCommand(BrokerMessage::ClientCommand::Final);
            message.setSourceId(message.serviceName(), MessageFrame::dynamic_bytes_tag{});
            message.setServiceName(worker.serviceName, MessageFrame::dynamic_bytes_tag{});

            dispatchMessageToMatchingSubscribers(std::move(message));
            break;
        }
        default:
            break;
        }
    }

    void deleteWorker(Worker &worker) {
        auto serviceIt = _services.find(worker.serviceName);
        if (serviceIt != _services.end()) {
            auto &waiting = serviceIt->second.waiting;
            waiting.erase(std::remove(waiting.begin(), waiting.end(), &worker), waiting.end());
        }

        _workers.erase(worker.id);
    }

    void disconnectWorker(Worker &worker) {
        auto           disconnect  = BrokerMessage::createWorkerMessage(BrokerMessage::WorkerCommand::Disconnect);
        constexpr auto dynamic_tag = MessageFrame::dynamic_bytes_tag{};
        constexpr auto static_tag  = MessageFrame::static_bytes_tag{};
        disconnect.setSourceId(worker.id, dynamic_tag);
        disconnect.setServiceName(worker.serviceName, dynamic_tag);
        disconnect.setTopic(worker.serviceName, dynamic_tag);
        disconnect.setBody("broker shutdown", static_tag);
        disconnect.setRbac(_rbac, static_tag);
        disconnect.send(worker.socket).assertSuccess();
        deleteWorker(worker);
    }

public:
    Broker(std::string brokerName, std::string dnsAddress, const Context &context)
        : _brokerName{ std::move(brokerName) }
        , _dnsAddress{ dnsAddress.empty() ? std::move(dnsAddress) : replaceScheme(std::move(dnsAddress), SCHEME_TCP) }
        , _routerSocket(context, ZMQ_ROUTER)
        , _pubSocket(context, ZMQ_XPUB)
        , _subSocket(context, ZMQ_XSUB)
        , _dnsSocket(context, ZMQ_DEALER) {
        auto commonSocketInit = [](Socket &socket) {
            int hwm     = HIGH_WATER_MARK;
            int ttl     = HEARTBEAT_INTERVAL * HEARTBEAT_LIVENESS;
            int timeout = HEARTBEAT_INTERVAL * HEARTBEAT_LIVENESS;
            int ivl     = HEARTBEAT_INTERVAL;
            int linger  = HEARTBEAT_INTERVAL;
            return zmq_invoke(zmq_setsockopt, socket, ZMQ_SNDHWM, &hwm, sizeof(hwm))
                && zmq_invoke(zmq_setsockopt, socket, ZMQ_RCVHWM, &hwm, sizeof(hwm))
                && zmq_invoke(zmq_setsockopt, socket, ZMQ_HEARTBEAT_TTL, &ttl, sizeof(ttl))
                && zmq_invoke(zmq_setsockopt, socket, ZMQ_HEARTBEAT_TIMEOUT, &timeout, sizeof(timeout))
                && zmq_invoke(zmq_setsockopt, socket, ZMQ_HEARTBEAT_IVL, &ivl, sizeof(ivl))
                && zmq_invoke(zmq_setsockopt, socket, ZMQ_LINGER, &linger, sizeof(linger));
        };

        // From setDefaultSocketParameters (io/opencmw/OpenCmwConstants.java)
        // TODO: Does not exist in zmq.h/hpp
        // socket.setHeartbeatContext(PROT_CLIENT.getData());

        commonSocketInit(_routerSocket).assertSuccess();
        zmq_invoke(zmq_bind, _routerSocket, INTERNAL_ADDRESS_BROKER.data()).assertSuccess();

        commonSocketInit(_subSocket).assertSuccess();
        zmq_invoke(zmq_bind, _subSocket, INTERNAL_ADDRESS_SUBSCRIBE.data()).assertSuccess();

        commonSocketInit(_pubSocket).assertSuccess();
        int verbose = 1;
        zmq_invoke(zmq_setsockopt, _pubSocket, ZMQ_XPUB_VERBOSE, &verbose, sizeof(verbose)).assertSuccess();
        zmq_invoke(zmq_bind, _pubSocket, INTERNAL_ADDRESS_PUBLISHER.data()).assertSuccess();

        commonSocketInit(_dnsSocket).assertSuccess();
        if (_dnsAddress.empty()) {
            zmq_invoke(zmq_connect, _dnsSocket, INTERNAL_ADDRESS_BROKER.data()).ignoreResult();
        } else {
            zmq_invoke(zmq_connect, _dnsSocket, _dnsAddress.data()).ignoreResult();
        }
    }

    Broker(const Broker &) = delete;
    Broker &operator=(const Broker &) = delete;

    enum class BindOption {
        DetectFromURI, ///< detect from URI which socket is meant (@see bind)
        Router,        ///< Always bind ROUTER socket
        Pub            ///< Always bind PUB socket
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
        const auto requestedScheme = getScheme(endpoint);
        assert(!(option == BindOption::DetectFromURI && requestedScheme == "inproc"));
        const auto isRouterSocket = option != BindOption::Pub && (option == BindOption::Router || requestedScheme.starts_with(SCHEME_MDP) || requestedScheme.starts_with(SCHEME_TCP));

        // Bind
        const auto result = [&] {
            const auto withTcp = replaceScheme(endpoint, SCHEME_TCP);
            return isRouterSocket ? zmq_invoke(zmq_bind, _routerSocket, withTcp.data()) : zmq_invoke(zmq_bind, _pubSocket, withTcp.data());
        }();

        if (!result) {
            debug() << fmt::format("Could not bind broker to '{}'\n", endpoint);
            return {};
        }

        const auto endpointAdjusted      = replaceScheme(endpoint, isRouterSocket ? SCHEME_MDP : SCHEME_MDS);
        const auto adjustedAddressPublic = endpointAdjusted; // TODO (java) resolveHost(endpointAdjusted, getLocalHostName());

        debug() << fmt::format("Majordomo broker/0.1 is active at '{}'\n", adjustedAddressPublic); // TODO do not hardcode version
        sendHeartbeats();                                                                          // TODO check that ported correctly: sendDnsHeartbeats(true);
        return adjustedAddressPublic;
    }

    bool receivePubMessage(Socket &socket) {
        // was receive plus handleSubscriptionMessage

        MessageFrame frame;
        const auto   result = frame.receive(socket, ZMQ_DONTWAIT);

        if (!result) {
            return false;
        }

        int64_t more;
        size_t  moreSize = sizeof(more);

        // We should not have more than one frame here
        if (!zmq_invoke(zmq_getsockopt, socket, ZMQ_RCVMORE, &more, &moreSize)) {
            return false;
        }

        if (more) {
            assert(!"There should be no more frames here");
            return false;
            // TODO: Should we read all frames and then ignore the message?
        }

        std::string_view data = frame.data();

        if (data.size() < 2 || !(data[0] == '\x0' || data[0] == '\x1')) {
            debug() << "Unexpected subscribe/unsubscribe message: " << data;
            return false;
        }

        const auto topic = std::string(data.substr(1));

        if (data[0] == '\x1') {
            auto it = _subscribedTopics.try_emplace(topic, 0);
            it.first->second++;
            if (it.first->second == 1) {
                frame.send(_subSocket, ZMQ_DONTWAIT).assertSuccess();
            }
        } else {
            auto it = _subscribedTopics.find(topic);
            if (it != _subscribedTopics.end()) {
                it->second--;
                if (it->second == 0) {
                    _subscribedTopics.erase(topic);
                    frame.send(_subSocket, ZMQ_DONTWAIT).assertSuccess();
                }
            }
        }

        return true;
    }

    bool receiveMessage(Socket &socket) {
        // was receive plus handleReceivedMessage
        auto maybeMessage = BrokerMessage::receive(socket);

        if (!maybeMessage) {
            return false;
        }

        auto &message = maybeMessage.value();

        if (!message.isValid()) {
            // TODO log properly, but not too verbose
            return false;
        }

        if (message.isClientMessage()) {
            switch (message.clientCommand()) {
            // TODO handle READY (client)?
            case BrokerMessage::ClientCommand::Subscribe: {
                auto it = _subscribedClientsByTopic.try_emplace(std::string(message.topic()), std::set<std::string>{});
                // TODO check for duplicate subscriptions?
                it.first->second.emplace(message.sourceId());
                if (it.first->second.size() == 1) {
                    // TODO correct?
                    MessageFrame frame(std::string("\x1") + std::string(message.topic()), MessageFrame::dynamic_bytes_tag{});
                    frame.send(socket, ZMQ_DONTWAIT).assertSuccess();
                }
                return true;
            }
            case BrokerMessage::ClientCommand::Unsubscribe: {
                auto it = _subscribedClientsByTopic.find(std::string(message.topic()));
                if (it != _subscribedClientsByTopic.end()) {
                    it->second.erase(std::string(message.sourceId()));
                    if (it->second.empty()) {
                        _subscribedClientsByTopic.erase(it);
                        // TODO correct?
                        MessageFrame frame(std::string("\x0") + std::string(message.topic()), MessageFrame::dynamic_bytes_tag{});
                        frame.send(socket, ZMQ_DONTWAIT).assertSuccess();
                    }
                }
                return true;
            }
            // TODO handle HEARTBEAT (client)?
            default:
                break;
            }

            const auto senderId = std::string(message.sourceId());
            auto       client   = _clients.try_emplace(senderId, socket, senderId);
            client.first->second.requests.emplace_back(std::move(message));

            return true;
        }

        assert(message.isWorkerMessage());
        processWorker(socket, std::move(message));
        return true;
    }

    void run() {
        do {
            processOneMessage();
        } while (!_shutdownRequested);

        cleanup();
    }

    void shutdown() {
        _shutdownRequested = true;
    }

    // test interface

    void processOneMessage() {
        _loopCount = 0;

        std::array<zmq_pollitem_t, 4> pollerItems;
        pollerItems[0].socket = _routerSocket.zmq_ptr;
        pollerItems[0].events = ZMQ_POLLIN;
        pollerItems[1].socket = _pubSocket.zmq_ptr;
        pollerItems[1].events = ZMQ_POLLIN;
        pollerItems[2].socket = _subSocket.zmq_ptr;
        pollerItems[2].events = ZMQ_POLLIN;
        pollerItems[3].socket = _dnsSocket.zmq_ptr;
        pollerItems[3].events = ZMQ_POLLIN;

        while (true) {
            bool anythingReceived = false;
            anythingReceived |= receiveMessage(_routerSocket);
            anythingReceived |= receiveMessage(_dnsSocket);
            anythingReceived |= receiveMessage(_subSocket);
            anythingReceived |= receivePubMessage(_pubSocket);

            processClients();

            if (_loopCount == 0) {
                _loopCount = 10;
                purgeWorkers();
                purgeClients();
                sendHeartbeats();
            } else {
                _loopCount--;
            }

            if (!anythingReceived) {
                break;
            } // TODO: && thread_not_interrupted && run

            if (!zmq_invoke(zmq_poll, pollerItems.data(), static_cast<int>(pollerItems.size()), 0L)) {
                break;
            }
        }
    }

    void cleanup() {
        // iterate and delete workers (safe in >= C++14)
        auto it = _workers.begin();
        while (it != _workers.end()) {
            auto &worker = it->second;
            ++it;
            disconnectWorker(worker);
        }
    }
};

} // namespace opencmw::majordomo

#endif
