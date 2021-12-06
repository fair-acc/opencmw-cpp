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

#include "URI.hpp"

#include <majordomo/Constants.hpp>
#include <majordomo/Message.hpp>
#include <majordomo/Settings.hpp>
#include <majordomo/SubscriptionMatcher.hpp>
#include <majordomo/Utils.hpp>
#include <majordomo/ZmqPtr.hpp>

using namespace std::string_literals;

namespace opencmw::majordomo {

using BrokerMessage = BasicMdpMessage<MessageFormat::WithSourceId>;

class Broker {
private:
    // Shorten chrono names
    using Clock       = std::chrono::steady_clock;
    using Timestamp   = std::chrono::time_point<Clock>;

    using EndpointURI = opencmw::URI<>;
    using TopicURI    = opencmw::URI<RELAXED>;

    struct Client {
        const Socket &            socket;
        const std::string         id;
        std::deque<BrokerMessage> requests;
        Timestamp                 expiry;

        explicit Client(const Socket &s, const std::string &id_, Timestamp expiry_)
            : socket(s), id(std::move(id_)), expiry{ std::move(expiry_) } {}
    };

    struct Worker {
        const Socket &    socket;
        const std::string id;
        const std::string serviceName;
        Timestamp         expiry;

        explicit Worker(const Socket &s, const std::string &id_, const std::string &serviceName_, Timestamp expiry_)
            : socket(s), id{ std::move(id_) }, serviceName{ std::move(serviceName_) }, expiry{ std::move(expiry_) } {}
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

public:
    const Context  context;
    const Settings settings;

private:
    Timestamp                                           _heartbeatAt = Clock::now() + settings.heartbeatInterval;
    std::unordered_map<TopicURI, std::set<std::string>> _subscribedClientsByTopic; // topic -> client IDs
    std::unordered_map<TopicURI, int>                   _subscribedTopics;         // topic -> subscription count
    std::unordered_map<std::string, Client>             _clients;
    std::unordered_map<std::string, Worker>             _workers;
    std::unordered_map<std::string, Service>            _services;

    const std::string                                   _brokerName;
    const std::optional<EndpointURI>                    _dnsAddress;
    const std::string                                   _rbac              = "TODO (RBAC)";

    std::atomic<bool>                                   _shutdownRequested = false;

    // Sockets collection. The Broker class will be used as the handler
    const Socket                  _routerSocket;
    const Socket                  _pubSocket;
    const Socket                  _subSocket;
    const Socket                  _dnsSocket;
    std::array<zmq_pollitem_t, 4> pollerItems;

public:
    Broker(std::string brokerName, Settings settings_ = {})
        : settings{ std::move(settings_) }
        , _brokerName{ std::move(brokerName) }
        , _routerSocket(context, ZMQ_ROUTER)
        , _pubSocket(context, ZMQ_XPUB)
        , _subSocket(context, ZMQ_SUB)
        , _dnsSocket(context, ZMQ_DEALER) {
        auto commonSocketInit = [settings_](const Socket &socket) {
            const int heartbeatInterval = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(settings_.heartbeatInterval).count());
            const int ttl               = heartbeatInterval * settings_.heartbeatLiveness;
            const int timeout           = heartbeatInterval * settings_.heartbeatLiveness;
            return zmq_invoke(zmq_setsockopt, socket, ZMQ_SNDHWM, &settings_.highWaterMark, sizeof(settings_.highWaterMark))
                && zmq_invoke(zmq_setsockopt, socket, ZMQ_RCVHWM, &settings_.highWaterMark, sizeof(settings_.highWaterMark))
                && zmq_invoke(zmq_setsockopt, socket, ZMQ_HEARTBEAT_TTL, &ttl, sizeof(ttl))
                && zmq_invoke(zmq_setsockopt, socket, ZMQ_HEARTBEAT_TIMEOUT, &timeout, sizeof(timeout))
                && zmq_invoke(zmq_setsockopt, socket, ZMQ_HEARTBEAT_IVL, &heartbeatInterval, sizeof(heartbeatInterval))
                && zmq_invoke(zmq_setsockopt, socket, ZMQ_LINGER, &heartbeatInterval, sizeof(heartbeatInterval));
        };

        // From setDefaultSocketParameters (io/opencmw/OpenCmwConstants.java)
        // TODO: Does not exist in zmq.h/hpp
        // socket.setHeartbeatContext(PROT_CLIENT.getData());

        commonSocketInit(_routerSocket).assertSuccess();
        zmq_invoke(zmq_bind, _routerSocket, INTERNAL_ADDRESS_BROKER.str.data()).assertSuccess();

        commonSocketInit(_subSocket).assertSuccess();
        zmq_invoke(zmq_bind, _subSocket, INTERNAL_ADDRESS_SUBSCRIBE.str.data()).assertSuccess();

        commonSocketInit(_pubSocket).assertSuccess();
        int verbose = 1;
        zmq_invoke(zmq_setsockopt, _pubSocket, ZMQ_XPUB_VERBOSE, &verbose, sizeof(verbose)).assertSuccess();
        zmq_invoke(zmq_bind, _pubSocket, INTERNAL_ADDRESS_PUBLISHER.str.data()).assertSuccess();

        commonSocketInit(_dnsSocket).assertSuccess();
        if (!settings_.dnsAddress.empty()) {
            zmq_invoke(zmq_connect, _dnsSocket, toZeroMQEndpoint(settings_.dnsAddress).data()).ignoreResult();
        } else {
            zmq_invoke(zmq_connect, _dnsSocket, INTERNAL_ADDRESS_BROKER.str.data()).ignoreResult();
        }

        pollerItems[0].socket = _routerSocket.zmq_ptr;
        pollerItems[0].events = ZMQ_POLLIN;
        pollerItems[1].socket = _pubSocket.zmq_ptr;
        pollerItems[1].events = ZMQ_POLLIN;
        pollerItems[2].socket = _subSocket.zmq_ptr;
        pollerItems[2].events = ZMQ_POLLIN;
        pollerItems[3].socket = _dnsSocket.zmq_ptr;
        pollerItems[3].events = ZMQ_POLLIN;
    }

    Broker(const Broker &) = delete;
    Broker &operator=(const Broker &) = delete;

    enum class BindOption {
        DetectFromURI, ///< detect from uri which socket is meant (@see bind)
        Router,        ///< Always bind ROUTER socket
        Pub            ///< Always bind PUB socket
    };

    /**
     * Bind broker to endpoint, can call this multiple times. We use a single
     * socket for both clients and workers.
     *
     * @param endpoint the uri-based 'scheme://ip:port' endpoint definition the server should listen to
     * The protocol definition
     *  - 'mdp://' corresponds to a SocketType.ROUTER socket
     *  - 'mds://' corresponds to a SocketType.XPUB socket
     *  - 'tcp://' internally falls back to 'mdp://' and ROUTER socket
     *  - 'inproc://' requires the socket type to be specified explicitly
     *
     * @return adjusted public address to use for clients/workers to connect
     */
    std::optional<EndpointURI> bind(const EndpointURI &endpoint, BindOption option = BindOption::DetectFromURI) {
        assert(!(option == BindOption::DetectFromURI && (endpoint.scheme() == SCHEME_INPROC || endpoint.scheme() == SCHEME_TCP)));
        const auto isRouterSocket = option != BindOption::Pub && (option == BindOption::Router || endpoint.scheme() == SCHEME_MDP || endpoint.scheme() == SCHEME_TCP);

        const auto zmqEndpoint    = toZeroMQEndpoint(endpoint);
        const auto result         = isRouterSocket ? zmq_invoke(zmq_bind, _routerSocket, zmqEndpoint.data())
                                                   : zmq_invoke(zmq_bind, _pubSocket, zmqEndpoint.data());

        if (!result) {
            debug() << fmt::format("Could not bind broker to '{}'\n", zmqEndpoint);
            return {};
        }

        const auto endpointAdjusted      = endpoint.scheme() == SCHEME_INPROC ? endpoint
                                                                              : EndpointURI::factory(endpoint).scheme(isRouterSocket ? SCHEME_MDP : SCHEME_MDS).build();
        const auto adjustedAddressPublic = endpointAdjusted; // TODO (java) resolveHost(endpointAdjusted, getLocalHostName());

        debug() << fmt::format("Majordomo broker/0.1 is active at '{}'\n", adjustedAddressPublic.str); // TODO do not hardcode version
        // TODO port sendDnsHeartbeats(true);
        return adjustedAddressPublic;
    }

    void run() {
        do {
        } while (processMessages() && !_shutdownRequested /* && thread is not interrupted */);

        cleanup();
    }

    void shutdown() {
        _shutdownRequested = true;
    }

    // test interface

    bool processMessages() {
        bool anythingReceived;
        int  loopCount = 0;
        do {
            anythingReceived = receiveMessage(_routerSocket);
            anythingReceived |= receiveMessage(_dnsSocket);
            anythingReceived |= receiveMessage(_subSocket);
            anythingReceived |= receivePubMessage(_pubSocket);

            processClients();

            if (loopCount % 10 == 0) { // perform maintenance task every 10th iteration
                purgeWorkers();
                purgeClients();
                sendHeartbeats();
            }
            loopCount++;
        } while (anythingReceived);

        // N.B. block until data arrived or for at most one heart-beat interval
        const auto result = zmq_invoke(zmq_poll, pollerItems.data(), static_cast<int>(pollerItems.size()), settings.heartbeatInterval.count());
        return result.isValid();
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

private:
    bool receivePubMessage(const Socket &socket) {
        MessageFrame frame;
        const auto   result = frame.receive(socket, ZMQ_DONTWAIT);

        if (!result) {
            return false;
        }

        std::string_view data = frame.data();

        if (data.size() < 2 || !(data[0] == '\x0' || data[0] == '\x1')) {
            debug() << "Unexpected subscribe/unsubscribe message: " << data;
            return false;
        }

        const auto topicString = data.substr(1);
        const auto topic       = parseTopicURI(topicString);
        if (!topic) {
            debug() << "Invalid topic: " << topicString;
            return false;
        }

        if (data[0] == '\x1') {
            auto it = _subscribedTopics.try_emplace(*topic, 0);
            it.first->second++;
            if (it.first->second == 1) {
                zmq_invoke(zmq_setsockopt, _subSocket, ZMQ_SUBSCRIBE, topicString.data(), topicString.size()).assertSuccess();
            }
        } else {
            auto it = _subscribedTopics.find(*topic);
            if (it != _subscribedTopics.end()) {
                it->second--;
                if (it->second == 0) {
                    zmq_invoke(zmq_setsockopt, _subSocket, ZMQ_UNSUBSCRIBE, topicString.data(), topicString.size()).assertSuccess();
                }
            }
        }

        return true;
    }

    bool receiveMessage(const Socket &socket) {
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
            switch (message.command()) {
            // TODO handle READY (client)?
            case Command::Subscribe: {
                if (message.topic().empty()) {
                    debug() << "received SUBSCRIBE with empty topic";
                    // TODO disconnect client?
                    return false;
                }
                const auto topicURI = parseTopicURI(message.topic());
                assert(topicURI); // TODO currently relaxed URI parsing always succeeds, handle error if this changes

                auto it = _subscribedClientsByTopic.try_emplace(*topicURI, std::set<std::string>{});
                // TODO check for duplicate subscriptions?
                it.first->second.emplace(message.sourceId());
                if (it.first->second.size() == 1) {
                    // TODO correct?
                    MessageFrame frame(std::string("\x1") + std::string(message.topic()), MessageFrame::dynamic_bytes_tag{});
                    frame.send(socket, ZMQ_DONTWAIT).assertSuccess();
                }
                return true;
            }
            case Command::Unsubscribe: {
                if (message.topic().empty()) {
                    debug() << "received UNSUBSCRIBE with empty topic";
                    // TODO disconnect client?
                    return false;
                }
                const auto topicURI = parseTopicURI(message.topic());
                assert(topicURI); // TODO currently relaxed URI parsing always succeeds, handle error if this changes

                auto it = _subscribedClientsByTopic.find(*topicURI);
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
            case Command::Heartbeat:
                // TODO sendDnsHeartbeats(true)
                break;
            default:
                break;
            }

            const auto senderId = std::string(message.sourceId());
            auto       client   = _clients.try_emplace(senderId, socket, senderId, updatedClientExpiry());
            client.first->second.requests.emplace_back(std::move(message));

            return true;
        }

        assert(message.isWorkerMessage());
        processWorker(socket, std::move(message));
        return true;
    }

    static std::optional<TopicURI> parseTopicURI(std::string_view s) {
        if (s.empty())
            return {};

        try {
            return TopicURI(std::string(s));
        } catch (const URISyntaxException &e) {
            debug() << fmt::format("Could not parse topic URI: {}", s);
            return {};
        }
    }

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
            message.setProtocol(Protocol::Worker);
            // TODO assert that command exists in both protocols?
            message.send(worker->socket).assertSuccess();
        }
    }

    void dispatchMessageToMatchingSubscribers(BrokerMessage &&message) {
        const auto topicURI = parseTopicURI(message.topic());
        if (!topicURI) {
            debug() << fmt::format("NOTIFY message has invalid topic: {}", message.topic());
            return;
        }

        const auto it                     = _subscribedClientsByTopic.find(*topicURI);
        const auto hasRouterSubscriptions = it != _subscribedClientsByTopic.end();

        // TODO avoid clone() for last message sent out
        for (const auto &topicIt : _subscribedTopics) {
            static const SubscriptionMatcher matcher;
            if (matcher(*topicURI, topicIt.first)) {
                // sends notification with the topic that is expected by the client for its subscription
                auto copy = message.clone();
                copy.setSourceId(topicIt.first.str, MessageFrame::dynamic_bytes_tag{});
                copy.send(_pubSocket).assertSuccess();
            }
        }

        if (hasRouterSubscriptions) {
            for (const auto &clientId : it->second) {
                auto copy = message.clone();
                copy.setSourceId(clientId, MessageFrame::dynamic_bytes_tag{});
                copy.send(_routerSocket).assertSuccess();
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
        worker.expiry = updatedWorkerExpiry();
        dispatch(service->second);
    }

    void purgeWorkers() {
        const auto now = Clock::now();

        for (auto &serviceIt : _services) {
            auto &service  = serviceIt.second;
            auto  workerIt = service.waiting.begin();
            while (workerIt != service.waiting.end()) {
                if ((*workerIt)->expiry < now) {
                    workerIt = service.waiting.erase(workerIt);
                    _workers.erase((*workerIt)->id);
                } else {
                    ++workerIt;
                }
            }
        }
    }

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
            constexpr auto static_tag  = MessageFrame::static_bytes_tag{};
            reply.setCommand(Command::Final);
            reply.setTopic(INTERNAL_SERVICE_NAMES, static_tag);
            reply.setBody("", static_tag);
            reply.setError(fmt::format("unknown service (error 501): '{}'", reply.serviceName()), dynamic_tag);
            reply.setRbacToken(_rbac, static_tag);

            reply.send(client.socket).assertSuccess();
        }
    }

    void purgeClients() {
        if (settings.clientTimeout.count() == 0) {
            return;
        }

        const auto now       = Clock::now();

        const auto isExpired = [&now](const auto &c) {
            return c.second.expiry < now;
        };

        std::erase_if(_clients, isExpired);
    }

    void sendHeartbeats() {
        if (Clock::now() < _heartbeatAt) {
            return;
        }

        for (auto &service : _services) {
            for (auto &worker : service.second.waiting) {
                auto heartbeat = BrokerMessage::createWorkerMessage(Command::Heartbeat);
                heartbeat.setSourceId(worker->id, MessageFrame::dynamic_bytes_tag{});
                heartbeat.setServiceName(worker->serviceName, MessageFrame::dynamic_bytes_tag{});
                heartbeat.setRbacToken(_rbac, MessageFrame::static_bytes_tag{});
                heartbeat.send(worker->socket).assertSuccess();
            }
        }

        _heartbeatAt = Clock::now() + settings.heartbeatInterval;
    }

    void processWorker(const Socket &socket, BrokerMessage &&message) {
        assert(message.isWorkerMessage());

        const auto serviceName = std::string(message.serviceName());
        const auto serviceId   = std::string(message.sourceId());
        const auto knownWorker = _workers.contains(serviceId);
        auto &     worker      = _workers.try_emplace(serviceId, socket, serviceId, serviceName, updatedWorkerExpiry()).first->second;

        switch (message.command()) {
        case Command::Ready: {
            debug() << "log new local/external worker for service " << serviceName << " - " << message;
            std::ignore = requireService(serviceName, std::string(message.body()));
            workerWaiting(worker);

            // notify potential listeners
            auto       notify      = BrokerMessage::createWorkerMessage(Command::Notify);
            const auto dynamic_tag = MessageFrame::dynamic_bytes_tag{};
            notify.setServiceName(INTERNAL_SERVICE_NAMES, dynamic_tag);
            notify.setTopic(INTERNAL_SERVICE_NAMES, dynamic_tag);
            notify.setClientRequestId(_brokerName, dynamic_tag);
            notify.setSourceId(INTERNAL_SERVICE_NAMES, dynamic_tag);
            notify.send(_pubSocket).assertSuccess();
            break;
        }
        case Command::Disconnect:
            // deleteWorker(worker); // TODO handle? also commented out in java impl
            break;
        case Command::Partial:
        case Command::Final: {
            if (knownWorker) {
                auto clientId = std::make_unique<std::string>(message.clientSourceId());
                auto client   = _clients.find(*clientId);
                if (client == _clients.end()) {
                    return; // drop if client unknown/disappeared
                }

                message.setSourceId(clientId.release(), MessageFrame::dynamic_bytes_tag{});
                message.setServiceName(worker.serviceName, MessageFrame::dynamic_bytes_tag{});
                message.setProtocol(Protocol::Client);
                message.send(client->second.socket).assertSuccess();
                workerWaiting(worker);
            } else {
                disconnectWorker(worker);
            }
            break;
        }
        case Command::Notify: {
            message.setProtocol(Protocol::Client);
            message.setCommand(Command::Final);
            message.setServiceName(worker.serviceName, MessageFrame::dynamic_bytes_tag{});

            dispatchMessageToMatchingSubscribers(std::move(message));
            break;
        }
        case Command::Heartbeat:
            if (knownWorker) {
                worker.expiry = updatedWorkerExpiry();
            } else {
                disconnectWorker(worker);
            }
        default:
            break;
        }
    }

    void deleteWorker(const Worker &worker) {
        auto serviceIt = _services.find(worker.serviceName);
        if (serviceIt != _services.end()) {
            auto &waiting = serviceIt->second.waiting;
            waiting.erase(std::remove(waiting.begin(), waiting.end(), &worker), waiting.end());
        }

        _workers.erase(worker.id);
    }

    void disconnectWorker(Worker &worker) {
        auto           disconnect  = BrokerMessage::createWorkerMessage(Command::Disconnect);
        constexpr auto dynamic_tag = MessageFrame::dynamic_bytes_tag{};
        constexpr auto static_tag  = MessageFrame::static_bytes_tag{};
        disconnect.setSourceId(worker.id, dynamic_tag);
        disconnect.setServiceName(worker.serviceName, dynamic_tag);
        disconnect.setTopic(worker.serviceName, dynamic_tag);
        disconnect.setBody("broker shutdown", static_tag);
        disconnect.setRbacToken(_rbac, dynamic_tag);
        disconnect.send(worker.socket).assertSuccess();
        deleteWorker(worker);
    }

    [[nodiscard]] Timestamp updatedClientExpiry() const { return Clock::now() + settings.clientTimeout; }
    [[nodiscard]] Timestamp updatedWorkerExpiry() const { return Clock::now() + settings.heartbeatInterval * settings.heartbeatLiveness; }
};

} // namespace opencmw::majordomo

#endif
