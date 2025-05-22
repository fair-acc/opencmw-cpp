#ifndef OPENCMW_MAJORDOMO_BROKER_H
#define OPENCMW_MAJORDOMO_BROKER_H

#include <array>
#include <atomic>
#include <chrono>
#include <deque>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include <format>

#include "Rbac.hpp"
#include "Topic.hpp"
#include "URI.hpp"

#include <Formatter.hpp>
#include <MdpMessage.hpp>
#include <opencmw.hpp>

#include <IoSerialiserJson.hpp>

#include <majordomo/Constants.hpp>
#include <majordomo/Settings.hpp>
#include <majordomo/SubscriptionMatcher.hpp>
#include <zmq/ZmqUtils.hpp>
#include <QuerySerialiser.hpp>

using namespace std::string_literals;

struct ServiceNamesList {
    std::vector<std::string> services;
};
ENABLE_REFLECTION_FOR(ServiceNamesList, services)

namespace opencmw::majordomo::detail {
struct DnsServiceItem {
    std::string                                        address;
    std::string                                        serviceName;
    std::set<URI<RELAXED>>                             uris;
    std::chrono::time_point<std::chrono::steady_clock> expiry;

    explicit DnsServiceItem(std::string address_, std::string serviceName_)
        : address{ std::move(address_) }
        , serviceName{ std::move(serviceName_) } {}
};

inline std::string uriAsString(const URI<RELAXED> &uri) {
    return uri.str();
}

} // namespace opencmw::majordomo::detail

template<>
struct std::formatter<opencmw::majordomo::detail::DnsServiceItem> {
    template<typename ParseContext>
    constexpr auto parse(ParseContext &ctx) {
        return ctx.begin();
    }

    template<typename FormatContext>
    auto format(const opencmw::majordomo::detail::DnsServiceItem &v, FormatContext &ctx) const {
#if not defined(__EMSCRIPTEN__) and (not defined(__clang__) or (__clang_major__ >= 16))
        return std::format_to(ctx.out(), "[{}: {}]", v.serviceName, opencmw::join(v.uris | std::views::transform(opencmw::majordomo::detail::uriAsString), ","));
#else
        std::vector<std::string> uris{};
        uris.reserve(v.uris.size());
        for (auto &uri : v.uris) {
            uris.emplace_back(opencmw::majordomo::detail::uriAsString(uri));
        }
        return std::format_to(ctx.out(), "[{}: {}]", v.serviceName, opencmw::join(uris, ","));
#endif
    }
};

namespace opencmw::majordomo {

using BrokerMessage = mdp::BasicMessage<mdp::MessageFormat::WithSourceId>;

enum class BindOption {
    DetectFromURI, ///< detect from uri which socket is meant (@see bind)
    Router,        ///< Always bind ROUTER socket
    Pub            ///< Always bind PUB socket
};

namespace detail {

inline constexpr std::string_view trimmed(std::string_view s) {
    using namespace std::literals;
    constexpr auto whitespace   = " \x0c\x0a\x0d\x09\x0b"sv;
    const auto     first        = s.find_first_not_of(whitespace);
    const auto     prefixLength = first != std::string_view::npos ? first : s.size();
    s.remove_prefix(prefixLength);
    if (s.empty()) {
        return s;
    }
    const auto last = s.find_last_not_of(whitespace);
    s.remove_suffix(s.size() - 1 - last);
    return s;
}

inline std::vector<std::string_view> split(std::string_view s, std::string_view delim) {
    std::vector<std::string_view> segments;
    while (true) {
        const auto pos = s.find(delim);
        if (pos == std::string_view::npos) {
            segments.push_back(s);
            return segments;
        }

        segments.push_back(s.substr(0, pos));
        s.remove_prefix(pos + 1);
    }
}

inline std::string_view stripStart(std::string_view s, std::string_view stripChars) {
    const auto pos = s.find_first_not_of(stripChars);
    if (pos == std::string_view::npos) {
        return {};
    }

    s.remove_prefix(pos);
    return s;
}

template<typename Left, typename Right>
inline bool iequal(const Left &left, const Right &right) noexcept {
    return std::equal(std::cbegin(left), std::cend(left), std::cbegin(right), std::cend(right),
            [](auto l, auto r) { return std::tolower(l) == std::tolower(r); });
}

inline std::string findDnsEntry(std::string_view brokerName, std::unordered_map<std::string, detail::DnsServiceItem> &dnsCache, std::string_view s) {
    const auto query                    = URI<RELAXED>(std::string(s));

    const auto queryScheme              = query.scheme();
    const auto queryPath                = query.path().value_or("");
    const auto strippedQueryPath        = stripStart(queryPath, "/");
    const auto stripStartFromSearchPath = strippedQueryPath.starts_with("/mmi.") ? std::format("/{}", brokerName) : "/"; // crop initial broker name for broker-specific MMI services

    const auto entryMatches             = [&queryScheme, &strippedQueryPath, &stripStartFromSearchPath](const auto &dnsEntry) {
        if (queryScheme && !iequal(dnsEntry.scheme().value_or(""), *queryScheme)) {
            return false;
        }

        const auto entryPath = dnsEntry.path().value_or("");
        return stripStart(entryPath, stripStartFromSearchPath).starts_with(strippedQueryPath);
    };

    std::vector<std::string> results;
    for (const auto &[cachedQuery, cacheEntry] : dnsCache) {
#if not defined(__EMSCRIPTEN__) and (not defined(__clang__) or (__clang_major__ >= 16))
        using namespace std::views;
        auto matching = cacheEntry.uris | filter(entryMatches) | transform(uriAsString);
        results.insert(results.end(), matching.begin(), matching.end());
#else
        for (const auto &uri : cacheEntry.uris) {
            if (entryMatches(uri)) {
                results.emplace_back(uriAsString(uri));
            }
        }
#endif
    }

    std::sort(results.begin(), results.end());

    using namespace std::literals;
    return std::format("[{}: {}]", s, opencmw::join(results.empty() ? std::vector{ "null"s } : results, ","));
}

} // namespace detail

template<role... Roles>
class Broker {
private:
    using Clock     = std::chrono::steady_clock;
    using Timestamp = std::chrono::time_point<Clock>;

    struct Client {
        const zmq::Socket        &socket;
        const std::string         id;
        std::deque<BrokerMessage> requests;
        Timestamp                 expiry;

        explicit Client(const zmq::Socket &s, const std::string &id_, Timestamp expiry_)
            : socket(s), id(std::move(id_)), expiry{ std::move(expiry_) } {}
    };

    struct Worker {
        const zmq::Socket &socket;
        const std::string  id;
        const std::string  serviceName;
        Timestamp          expiry;

        explicit Worker(const zmq::Socket &s, const std::string &id_, const std::string &serviceName_, Timestamp expiry_)
            : socket(s), id{ std::move(id_) }, serviceName{ std::move(serviceName_) }, expiry{ std::move(expiry_) } {}
    };

    struct Service {
        using QueueEntry    = std::pair<std::string_view, std::deque<BrokerMessage>>;
        using PriorityQueue = std::array<QueueEntry, std::max(sizeof...(Roles), static_cast<std::size_t>(1))>;
        static constexpr PriorityQueue makePriorityQueue() {
            if constexpr (sizeof...(Roles) == 0) {
                return { QueueEntry{ "", std::deque<BrokerMessage>{} } };
            } else {
                return { QueueEntry{ Roles::name(), std::deque<BrokerMessage>{} }... };
            }
        }

        std::function<BrokerMessage(BrokerMessage &&)> internalHandler;
        std::string                                    name;
        std::string                                    description;
        std::deque<Worker *>                           waiting;
        PriorityQueue                                  requestsByPriority = makePriorityQueue();
        std::size_t                                    requestCount       = 0;

        auto                                          &queueForRole(const std::string_view role) {
            auto it = std::find_if(requestsByPriority.begin(), requestsByPriority.end(), [&role](const auto &v) { return v.first == role; });
            return it != requestsByPriority.end() ? it->second : requestsByPriority.back().second;
        }

        explicit Service(std::string name_, std::string description_)
            : name(std::move(name_))
            , description(std::move(description_)) {
        }

        explicit Service(std::string name_, std::function<BrokerMessage(BrokerMessage &&)> internalHandler_)
            : internalHandler{ std::move(internalHandler_) }
            , name{ std::move(name_) } {
        }

        void putMessage(BrokerMessage &&message) {
            const auto role = parse_rbac::role(message.rbac.asString());
            queueForRole(role).emplace_back(std::move(message));
            requestCount++;
        }

        BrokerMessage takeNextMessage() {
            auto queueIt = std::find_if(requestsByPriority.begin(), requestsByPriority.end(), [](const auto &v) { return !v.second.empty(); });
            assert(queueIt != requestsByPriority.end());
            auto msg = std::move(queueIt->second.front());
            queueIt->second.pop_front();
            requestCount--;
            return msg;
        }

        Worker *takeNextWorker() {
            auto worker = waiting.front();
            waiting.pop_front();
            return worker;
        }
    };

public:
    const zmq::Context context;
    const Settings     settings;
    const std::string  brokerName;

private:
    Timestamp                                               _heartbeatAt = Clock::now() + settings.heartbeatInterval;
    SubscriptionMatcher                                     _subscriptionMatcher;
    std::unordered_map<mdp::Topic, std::set<std::string>>   _subscribedClientsByTopic; // topic -> client IDs
    std::unordered_map<mdp::Topic, int>                     _subscribedTopics;         // topic -> subscription count
    std::unordered_map<std::string, Client>                 _clients;
    std::unordered_map<std::string, Worker>                 _workers;
    std::unordered_map<std::string, Service>                _services;
    std::unordered_map<std::string, detail::DnsServiceItem> _dnsCache;
    std::set<std::string>                                   _dnsAddresses;
    Timestamp                                               _dnsHeartbeatAt;
    bool                                                    _connectedToDns    = false;

    const IoBuffer                                          _rbac              = IoBuffer("RBAC=ADMIN,abcdef12345");

    std::atomic<bool>                                       _shutdownRequested = false;

    // Sockets collection. The Broker class will be used as the handler
    const zmq::Socket             _routerSocket;
    const zmq::Socket             _pubSocket;
    const zmq::Socket             _subSocket;
    const zmq::Socket             _dnsSocket;
    std::array<zmq_pollitem_t, 4> pollerItems;

public:
    Broker(std::string brokerName_, Settings settings_ = {})
        : settings{ std::move(settings_) }
        , brokerName{ std::move(brokerName_) }
        , _routerSocket(context, ZMQ_ROUTER)
        , _pubSocket(context, ZMQ_XPUB)
        , _subSocket(context, ZMQ_SUB)
        , _dnsSocket(context, ZMQ_DEALER) {
        assert(mdp::isValidServiceName(brokerName));

        addInternalService("/mmi.dns", [this](BrokerMessage &&message) {
            using namespace std::literals;
            message.command = mdp::Command::Final;

            std::string reply;
            if (message.data.empty() || message.data.asString().find_first_of(",:/") == std::string_view::npos) {
#if not defined(__EMSCRIPTEN__) and (not defined(__clang__) or (__clang_major__ >= 16))
                const auto entryView = std::views::values(_dnsCache);
                auto       entries   = std::vector(entryView.begin(), entryView.end());
                std::ranges::sort(entries, {}, &detail::DnsServiceItem::serviceName);
#else
                std::vector<detail::DnsServiceItem> entries;
                entries.reserve(_dnsCache.size());
                for (auto& [_, val] : _dnsCache) {
                    entries.emplace_back(val);
                }
                std::sort(entries.begin(), entries.end(), [](auto &a, auto &b) {return a.serviceName < b.serviceName;});
#endif
                reply = std::format("{}", opencmw::join(entries, ","));
            } else {
                // TODO std::views::split seems to have issues in GCC 11, maybe switch to views::split/transform
                // once it works with our then supported compilers
                const auto               body     = message.data.asString();
                auto                     segments = detail::split(body, ","sv);
                std::vector<std::string> results(segments.size());
                std::transform(segments.begin(), segments.end(), results.begin(), [this](const auto &v) {
                    return detail::findDnsEntry(brokerName, _dnsCache, detail::trimmed(v));
                });

                reply = std::format("{}", opencmw::join(results, ","));
            }

            message.data = IoBuffer(reply.data(), reply.size());
            return message;
        });

        addInternalService("/mmi.echo", [](BrokerMessage &&message) { return message; });

        addInternalService("/mmi.service", [this](BrokerMessage &&message) {
            message.command = mdp::Command::Final;
            if (message.data.empty()) {
#if not defined(__EMSCRIPTEN__) and (not defined(__clang__) or (__clang_major__ >= 16))
                const auto keyView = std::views::keys(_services);
                auto       keys    = std::vector<std::string>(keyView.begin(), keyView.end());
                std::ranges::sort(keys);
#else
                std::vector<std::string> keys;
                keys.reserve(_services.size());
                for (auto& [key, _] : _services) {
                    keys.emplace_back(key);
                }
                std::sort(keys.begin(), keys.end());
#endif

                auto body    = std::format("{}", opencmw::join(keys, ","));
                message.data = IoBuffer(body.data(), body.size());
                return message;
            }

            const auto exists = _services.contains(std::string(message.data.asString()));
            message.data      = IoBuffer(exists ? "200" : "404");
            return message;
        });

        addInternalService("/mmi.openapi", [this](BrokerMessage &&message) {
            message.command        = mdp::Command::Final;
            const auto serviceName = std::string(message.data.asString());
            const auto serviceIt   = _services.find(serviceName);
            if (serviceIt != _services.end()) {
                message.data = IoBuffer(serviceIt->second.description.data(), serviceIt->second.description.size());
                message.error.clear();
            } else {
                message.data  = IoBuffer();
                message.error = std::format("Requested invalid service '{}'", serviceName);
            }
            return message;
        });

        // From setDefaultSocketParameters (io/opencmw/OpenCmwConstants.java)
        // TODO: Does not exist in zmq.h/hpp
        // socket.setHeartbeatContext(PROT_CLIENT.getData());

        zmq::initializeSocket(_routerSocket, settings).assertSuccess();
        zmq::invoke(zmq_bind, _routerSocket, INTERNAL_ADDRESS_BROKER.str().data()).assertSuccess();

        zmq::initializeSocket(_subSocket, settings).assertSuccess();
        zmq::invoke(zmq_bind, _subSocket, INTERNAL_ADDRESS_SUBSCRIBE.str().data()).assertSuccess();

        zmq::initializeSocket(_pubSocket, settings).assertSuccess();
        int verbose = 1;
        zmq::invoke(zmq_setsockopt, _pubSocket, ZMQ_XPUB_VERBOSE, &verbose, sizeof(verbose)).assertSuccess();
        zmq::invoke(zmq_bind, _pubSocket, INTERNAL_ADDRESS_PUBLISHER.str().data()).assertSuccess();

        zmq::initializeSocket(_dnsSocket, settings).assertSuccess();
        if (!settings.dnsAddress.empty()) {
            zmq::invoke(zmq_connect, _dnsSocket, mdp::toZeroMQEndpoint(URI<>(settings.dnsAddress)).data()).assertSuccess();
        } else {
            zmq::invoke(zmq_connect, _dnsSocket, INTERNAL_ADDRESS_BROKER.str().data()).assertSuccess();
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

    Broker(const Broker &)            = delete;
    Broker &operator=(const Broker &) = delete;

    template<typename Filter>
    void addFilter(const std::string &key) {
        _subscriptionMatcher.addFilter<Filter>(key);
    }

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
    std::optional<URI<STRICT>> bind(const URI<STRICT> &endpoint, BindOption option = BindOption::DetectFromURI) {
        assert(!(option == BindOption::DetectFromURI && (endpoint.scheme() == SCHEME_INPROC || endpoint.scheme() == SCHEME_TCP)));
        const auto isRouterSocket = option != BindOption::Pub && (option == BindOption::Router || endpoint.scheme() == SCHEME_MDP || endpoint.scheme() == SCHEME_TCP);

        const auto zmqEndpoint    = mdp::toZeroMQEndpoint(endpoint);
        const auto result         = isRouterSocket ? zmq::invoke(zmq_bind, _routerSocket, zmqEndpoint.data())
                                                   : zmq::invoke(zmq_bind, _pubSocket, zmqEndpoint.data());

        if (!result) {
            return {};
        }

        const auto endpointAdjusted      = endpoint.scheme() == SCHEME_INPROC ? endpoint
                                                                              : URI<STRICT>::factory(endpoint).scheme(isRouterSocket ? SCHEME_MDP : SCHEME_MDS).build();
        const auto adjustedAddressPublic = endpointAdjusted; // TODO (java) resolveHost(endpointAdjusted, getLocalHostName());

        _dnsAddresses.insert(adjustedAddressPublic.str());
        sendDnsHeartbeats(true);
        return adjustedAddressPublic;
    }

    void run() {
        sendDnsHeartbeats(true); // initial register of default routes

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
                purgeDnsServices();
            }
            loopCount++;
        } while (anythingReceived);

        // N.B. block until data arrived or for at most one heart-beat interval
        const auto result = zmq::invoke(zmq_poll, pollerItems.data(), static_cast<int>(pollerItems.size()), settings.heartbeatInterval.count());
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

    void registerDnsAddress(const opencmw::URI<> &address) {
        // worker registers a new address for this broker (used the REST interface)
        _dnsAddresses.insert(address.str());
        auto [iter, inserted] = _dnsCache.try_emplace(brokerName, std::string(), brokerName);
        iter->second.uris.insert(URI<RELAXED>(address.str()));
        sendDnsHeartbeats(true);
    }

    template<typename Handler>
    void forEachService(Handler &&handler) const {
        for (const auto &[name, service] : _services) {
            handler(std::string_view{ name }, std::string_view{ service.description });
        }
    }

private:
    void subscribe(const mdp::Topic &topic) {
        auto [it, inserted] = _subscribedTopics.try_emplace(topic, 0);
        it->second++;
        if (it->second == 1) {
            const auto topicStr = topic.toZmqTopic();
            zmq::invoke(zmq_setsockopt, _subSocket, ZMQ_SUBSCRIBE, topicStr.data(), topicStr.size()).assertSuccess();
        }
    }

    void unsubscribe(const mdp::Topic &topic) {
        auto it = _subscribedTopics.find(topic);
        if (it == _subscribedTopics.end()) {
            return;
        }
        it->second--;
        if (it->second == 0) {
            const auto topicStr = topic.toZmqTopic();
            zmq::invoke(zmq_setsockopt, _subSocket, ZMQ_UNSUBSCRIBE, topicStr.data(), topicStr.size()).assertSuccess();
            _subscribedTopics.erase(it);
        }
    }

    bool receivePubMessage(const zmq::Socket &socket) {
        zmq::MessageFrame frame;
        const auto        result = frame.receive(socket, ZMQ_DONTWAIT);

        if (!result) {
            return false;
        }

        std::string_view data = frame.data();

        if (data.size() < 2 || !(data[0] == '\x0' || data[0] == '\x1')) {
            return false;
        }

        const auto                topicString = data.substr(1);
        std::optional<mdp::Topic> topic;
        try {
            topic = mdp::Topic::fromZmqTopic(topicString);
        } catch (...) {
            // malformed topic, ignore
            return false;
        }
        if (data[0] == '\x1') {
            subscribe(*topic);
        } else {
            unsubscribe(*topic);
        }
        return true;
    }

    bool receiveMessage(const zmq::Socket &socket) {
        auto maybeMessage = zmq::receive<mdp::MessageFormat::WithSourceId>(socket);

        if (!maybeMessage) {
            return false;
        }

        auto &message = maybeMessage.value();

        if (message.protocolName == mdp::clientProtocol) {
            switch (message.command) {
            case mdp::Command::Ready: {
                if (const auto topicURI = URI<RELAXED>(message.topic.str()); topicURI.scheme()) {
                    auto [iter, inserted] = _dnsCache.try_emplace(message.serviceName, std::string(message.sourceId), message.serviceName);
                    iter->second.uris.insert(topicURI);
                    iter->second.expiry = updatedDnsExpiry();
                }
                return true;
            }
            case mdp::Command::Subscribe: {
                std::optional<mdp::Topic> subscription;
                try {
                    subscription = mdp::Topic::fromMdpTopic(message.topic);
                } catch (...) {
                    // malformed topic, ignore
                    return false;
                }

                subscribe(*subscription);
                auto [it, inserted] = _subscribedClientsByTopic.try_emplace(*subscription);
                it->second.emplace(message.sourceId);
                return true;
            }
            case mdp::Command::Unsubscribe: {
                std::optional<mdp::Topic> subscription;
                try {
                    subscription = mdp::Topic::fromMdpTopic(message.topic);
                } catch (...) {
                    // malformed topic, ignore
                    return false;
                }

                unsubscribe(*subscription);
                auto it = _subscribedClientsByTopic.find(*subscription);
                if (it != _subscribedClientsByTopic.end()) {
                    it->second.erase(message.sourceId);
                    if (it->second.empty()) {
                        _subscribedClientsByTopic.erase(it);
                    }
                }
                return true;
            }
            case mdp::Command::Heartbeat:
                sendDnsHeartbeats(true);
                break;
            default:
                break;
            }

            auto [client, inserted] = _clients.try_emplace(message.sourceId, socket, message.sourceId, updatedClientExpiry());
            client->second.requests.emplace_back(std::move(message));

            return true;
        }

        processWorker(socket, std::move(message));
        return true;
    }

    Service &requireService(std::string serviceName, std::string serviceDescription) {
        // TODO handle serviceDescription differing between workers? or is "first one wins" ok?
        auto [it, inserted] = _services.try_emplace(serviceName, std::move(serviceName), std::move(serviceDescription));
        return it->second;
    }

    void addInternalService(std::string serviceName, std::function<BrokerMessage(BrokerMessage &&)> handler) {
        _services.try_emplace(serviceName, std::move(serviceName), std::move(handler));
    }

    Service *bestMatchingService(std::string_view serviceName) {
        // TODO use some smart reactive filtering once available, maybe optimize or cache
        std::string bestServiceName;
        Service    *bestService  = nullptr;

        auto        lessByLength = [](auto lhs, auto rhs) {
            return lhs.size() == rhs.size()
                                ? lhs < rhs
                                : lhs.size() < rhs.size();
        };

        for (auto &[name, service] : _services) {
            // remove leading slashes from both the name and the
            // service name, if present
            auto removeSlash = [](std::string_view s) {
                if (s[0] == '/') {
                    return std::string_view(s.data() + 1, s.size() - 1);
                }
                return s;
            };
            auto n  = removeSlash(name);
            auto sn = removeSlash(serviceName);
            if (!sn.starts_with(n)) continue;
            if (!bestService || lessByLength(n, bestServiceName)) {
                bestServiceName = n;
                bestService     = std::addressof(service);
            }
        }

        return bestService;
    }

    void dispatch(Service &service) {
        purgeWorkers();

        while (!service.waiting.empty() && service.requestCount > 0) {
            auto message = service.takeNextMessage();
            auto worker  = service.takeNextWorker();
            assert(worker);
            message.serviceName  = message.sourceId; // serviceName=clientSourceID
            message.sourceId     = worker->id;
            message.protocolName = mdp::workerProtocol;
            // TODO assert that command exists in both protocols?
            zmq::send(std::move(message), worker->socket).assertSuccess();
        }
    }

    void dispatchMessageToMatchingSubscribers(BrokerMessage &&message) {
        std::optional<mdp::Topic> notification;

        try {
            notification = mdp::Topic::fromMdpTopic(message.topic);
        } catch (...) {
            // malformed topic, ignore
            return;
        }
        // TODO avoid clone() for last message sent out
        for (const auto &[topic, _] : _subscribedTopics) {
            if (_subscriptionMatcher(*notification, topic)) {
                auto copy     = message;
                copy.sourceId = topic.toZmqTopic();
                zmq::send(std::move(copy), _pubSocket).assertSuccess();

                const auto it = _subscribedClientsByTopic.find(topic);
                if (it != _subscribedClientsByTopic.end()) {
                    for (const auto &clientId : it->second) {
                        auto clientCopy     = message;
                        clientCopy.sourceId = clientId;
                        zmq::send(std::move(clientCopy), _routerSocket).assertSuccess();
                    }
                }
            }
        }
    }

    void workerWaiting(Worker &worker) {
        // Queue to broker and service waiting lists
        // TODO
        // waiting.addLast(worker);
        auto service = _services.find(worker.serviceName);
        service->second.waiting.push_back(&worker);
        worker.expiry = updatedWorkerExpiry();
        dispatch(service->second);
    }

    void purgeWorkers() {
        const auto now = Clock::now();

        for (auto &[name, service] : _services) {
            auto workerIt = service.waiting.begin();
            while (workerIt != service.waiting.end()) {
                if ((*workerIt)->expiry < now) {
                    auto id  = (*workerIt)->id;
                    workerIt = service.waiting.erase(workerIt);
                    _workers.erase(id);
                } else {
                    ++workerIt;
                }
            }
        }
    }

    void processClients() {
        for (auto &[senderId, client] : _clients) {
            if (client.requests.empty())
                continue;

            auto clientMessage = std::move(client.requests.back());
            client.requests.pop_back();

            if (auto service = bestMatchingService(clientMessage.serviceName)) {
                if (service->internalHandler) {
                    auto reply = service->internalHandler(std::move(clientMessage));
                    zmq::send(std::move(reply), client.socket).assertSuccess();
                } else {
                    service->putMessage(std::move(clientMessage));
                    dispatch(*service);
                }
                return;
            }

            // not implemented -- reply according to Majordomo Management Interface (MMI) as defined in http://rfc.zeromq.org/spec:8

            auto reply    = std::move(clientMessage);
            reply.command = mdp::Command::Final;
            reply.topic   = INTERNAL_SERVICE_NAMES_URI;
            reply.data.clear();
            reply.error = std::format("unknown service (error 501): '{}'", reply.serviceName);
            reply.rbac  = _rbac;

            zmq::send(std::move(reply), client.socket).assertSuccess();
        }
    }

    void purgeClients() {
        if (settings.clientTimeout.count() == 0) {
            return;
        }

        const auto now       = Clock::now();

        const auto isExpired = [&now](const auto &c) {
            auto &[senderId, client] = c;
            return client.expiry < now;
        };

        std::erase_if(_clients, isExpired);
    }

    void purgeDnsServices() {
        const auto now = Clock::now();
        if (now < _dnsHeartbeatAt) {
            return;
        }

        sendDnsHeartbeats(false);

        BrokerMessage challenge;
        challenge.protocolName    = mdp::clientProtocol;
        challenge.command         = mdp::Command::Heartbeat;
        challenge.clientRequestID = IoBuffer("dnsChallenge");
        challenge.rbac            = _rbac;
        const auto newExpiry      = updatedDnsExpiry();

        for (auto &[broker, registeredService] : _dnsCache) {
            if (registeredService.serviceName == brokerName) { // TODO ignore case
                registeredService.expiry = newExpiry;
                continue;
            }
            // challenge remote broker with a HEARTBEAT
            auto toSend        = challenge;
            toSend.sourceId    = registeredService.address;
            toSend.serviceName = registeredService.serviceName;
            zmq::send(std::move(toSend), _routerSocket).assertSuccess();
        }
        std::erase_if(_dnsCache, [&now](const auto &entry) { auto& [broker, registeredService] = entry; return registeredService.expiry < now; });

        _dnsHeartbeatAt = now + settings.dnsTimeout;
    }

    void sendHeartbeats() {
        if (Clock::now() < _heartbeatAt) {
            return;
        }

        for (auto &[name, service] : _services) {
            for (auto &worker : service.waiting) {
                BrokerMessage heartbeat;
                heartbeat.protocolName = mdp::workerProtocol;
                heartbeat.command      = mdp::Command::Heartbeat;
                heartbeat.sourceId     = worker->id;
                heartbeat.serviceName  = worker->serviceName;
                heartbeat.rbac         = _rbac;
                zmq::send(std::move(heartbeat), worker->socket).assertSuccess();
            }
        }

        _heartbeatAt = Clock::now() + settings.heartbeatInterval;
    }

    void processWorker(const zmq::Socket &socket, BrokerMessage &&message) {
        const auto &serviceId   = message.sourceId;
        const auto  knownWorker = _workers.contains(serviceId);
        auto       &worker      = _workers.try_emplace(serviceId, socket, serviceId, message.serviceName, updatedWorkerExpiry()).first->second;

        switch (message.command) {
        case mdp::Command::Ready: {
            std::ignore = requireService(message.serviceName, std::string(message.data.asString()));
            workerWaiting(worker);
            registerNewService(message.serviceName);
            // notify potential listeners
            BrokerMessage notify;
            notify.serviceName     = INTERNAL_SERVICE_NAMES;
            notify.topic           = INTERNAL_SERVICE_NAMES_URI;
            notify.clientRequestID = IoBuffer(brokerName.data(), brokerName.size());
            notify.sourceId        = INTERNAL_SERVICE_NAMES;
            zmq::send(std::move(notify), _pubSocket).assertSuccess();
            break;
        }
        case mdp::Command::Disconnect:
            // deleteWorker(worker); // TODO handle? also commented out in java impl
            break;
        case mdp::Command::Partial:
        case mdp::Command::Final: {
            if (knownWorker) {
                auto client = _clients.find(message.serviceName); // serviceName=clientSourceID
                if (client == _clients.end()) {
                    return; // drop if client unknown/disappeared
                }

                message.sourceId     = message.serviceName; // serviceName=clientSourceID
                message.serviceName  = worker.serviceName;
                message.protocolName = mdp::clientProtocol;
                zmq::send(std::move(message), client->second.socket).assertSuccess();
                workerWaiting(worker);
            } else {
                disconnectWorker(worker);
            }
            break;
        }
        case mdp::Command::Notify: {
            message.protocolName = mdp::clientProtocol;
            message.command      = mdp::Command::Final;
            message.serviceName  = worker.serviceName;

            dispatchMessageToMatchingSubscribers(std::move(message));
            break;
        }
        case mdp::Command::Heartbeat:
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
        BrokerMessage disconnect;
        disconnect.protocolName = mdp::workerProtocol;
        disconnect.command      = mdp::Command::Disconnect;
        disconnect.sourceId     = worker.id;
        disconnect.serviceName  = worker.serviceName;
        disconnect.data         = IoBuffer("broker shutdown");
        disconnect.rbac         = _rbac;
        zmq::send(std::move(disconnect), worker.socket).assertSuccess();
        deleteWorker(worker);
    }

    mdp::Message createDnsReadyMessage() {
        mdp::Message ready;
        ready.protocolName    = mdp::clientProtocol;
        ready.command         = mdp::Command::Ready;
        ready.serviceName     = brokerName;
        ready.clientRequestID = IoBuffer("clientID");
        ready.rbac            = _rbac;
        return ready;
    }

    void sendDnsHeartbeats(bool force) {
        if (Clock::now() > _dnsHeartbeatAt || force) {
            const auto ready = createDnsReadyMessage();
            for (const auto &dnsAddress : _dnsAddresses) {
                auto toSend  = ready;
                toSend.topic = mdp::Message::URI(dnsAddress);
                registerWithDnsServices(std::move(toSend));
            }
            for (const auto &[name, service] : _services) {
                registerNewService(name);
            }
        }
    }

    void registerNewService(std::string_view serviceName) {
        for (const auto &dnsAddress : _dnsAddresses) {
            auto       ready   = createDnsReadyMessage();
            const auto address = std::format("{}/{}", dnsAddress, detail::stripStart(serviceName, "/"));
            // TODO use URI factory?
            ready.topic = mdp::Message::URI(address);
            registerWithDnsServices(std::move(ready));
        }
    }

    void registerWithDnsServices(mdp::Message &&readyMessage) {
        auto [it, inserted] = _dnsCache.try_emplace(brokerName, std::string(), brokerName);
        it->second.uris.insert(URI<RELAXED>(readyMessage.topic.str()));
        it->second.expiry = updatedDnsExpiry();
        zmq::send(std::move(readyMessage), _dnsSocket).ignoreResult();
    }

    [[nodiscard]] Timestamp updatedClientExpiry() const { return Clock::now() + settings.clientTimeout; }
    [[nodiscard]] Timestamp updatedWorkerExpiry() const { return Clock::now() + settings.heartbeatInterval * settings.heartbeatLiveness; }
    [[nodiscard]] Timestamp updatedDnsExpiry() const { return Clock::now() + settings.dnsTimeout * settings.heartbeatLiveness; }
};

} // namespace opencmw::majordomo

#endif
