#ifndef OPENCMW_MAJORDOMO_WORKER_H
#define OPENCMW_MAJORDOMO_WORKER_H
#include <array>
#include <atomic>
#include <chrono>
#include <shared_mutex>
#include <string>
#include <thread>

#include <unordered_map>
#include <unordered_set>

#include <format>

#include <Debug.hpp>
#include <IoSerialiserCmwLight.hpp>
#include <IoSerialiserJson.hpp>
#include <IoSerialiserYaS.hpp>
#include <majordomo/Broker.hpp>
#include <majordomo/Constants.hpp>
#include <majordomo/Rbac.hpp>
#include <majordomo/Settings.hpp>
#include <majordomo/SubscriptionMatcher.hpp>
#include <MdpMessage.hpp>
#include <MIME.hpp>
#include <MustacheSerialiser.hpp>
#include <opencmw.hpp>
#include <zmq/ZmqUtils.hpp>
#include <QuerySerialiser.hpp>

namespace opencmw::majordomo {

struct RequestContext {
    const mdp::Message request = {};
    mdp::Message       reply;
    MIME::MimeType     mimeType = MIME::BINARY;
};

namespace worker_detail {
inline int nextWorkerId() {
    static std::atomic<int> idCounter = 0;
    return ++idCounter;
}

template<typename WrappedValue>
struct description_impl {
    static constexpr auto value = WrappedValue::value;
};

template<auto Value>
struct to_type {
    static constexpr auto value = Value;
};

} // namespace worker_detail

template<units::basic_fixed_string Value>
using description = worker_detail::description_impl<worker_detail::to_type<Value>>;

template<units::basic_fixed_string serviceName, typename... Meta>
class BasicWorker {
    using Clock        = std::chrono::steady_clock;
    using Timestamp    = std::chrono::time_point<Clock>;
    using Description  = opencmw::find_type<worker_detail::description_impl, Meta...>;
    using DefaultRoles = std::tuple<ADMIN>;
    using Roles        = opencmw::tuple_unique<opencmw::tuple_cat_t<DefaultRoles, find_roles<Meta...>>>;

    struct NotificationHandler {
        zmq::Socket socket;
        Timestamp   lastUsed;

        explicit NotificationHandler(const zmq::Context &context, std::string_view notifyAddress)
            : socket(context, ZMQ_PUSH) {
            zmq::invoke(zmq_connect, socket, notifyAddress.data()).assertSuccess();
        }

        bool send(mdp::Message &&message) {
            lastUsed = Clock::now();
            return zmq::send(std::move(message), socket).isValid();
        }
    };

    std::function<void(RequestContext &)>                    _handler;
    const Settings                                           _settings;
    const opencmw::URI<STRICT>                               _brokerAddress;
    std::atomic<bool>                                        _shutdownRequested = false;
    int                                                      _liveness          = 0;
    Timestamp                                                _heartbeatAt;
    const zmq::Context                                      &_context;
    std::optional<zmq::Socket>                               _workerSocket;
    std::optional<zmq::Socket>                               _pubSocket;
    std::array<zmq_pollitem_t, 3>                            _pollerItems;
    SubscriptionMatcher                                      _subscriptionMatcher;
    mutable std::mutex                                       _activeSubscriptionsLock;
    std::unordered_set<mdp::Topic>                           _activeSubscriptions;
    zmq::Socket                                              _notifyListenerSocket;
    std::unordered_map<std::thread::id, NotificationHandler> _notificationHandlers;
    std::shared_mutex                                        _notificationHandlersLock;
    const std::string                                        _notifyAddress;
    const IoBuffer                                           _defaultRbacToken = IoBuffer("RBAC=NONE,");

public:
    static constexpr std::string_view name = serviceName.data();
    static_assert(mdp::isValidServiceName(name));

    explicit BasicWorker(opencmw::URI<STRICT> brokerAddress, std::function<void(RequestContext &)> handler, const zmq::Context &context, Settings settings = {})
        : _handler{ std::move(handler) }, _settings{ std::move(settings) }, _brokerAddress{ std::move(brokerAddress) }, _context(context), _notifyListenerSocket(_context, ZMQ_PULL), _notifyAddress(makeNotifyAddress()) {
        zmq::invoke(zmq_bind, _notifyListenerSocket, _notifyAddress.data()).assertSuccess();
    }

    template<typename BrokerType>
    explicit BasicWorker(const BrokerType &broker, std::function<void(RequestContext &)> handler)
        : BasicWorker(INPROC_BROKER, std::move(handler), broker.context, broker.settings) {
    }

    [[nodiscard]] static constexpr std::string_view serviceDescription() {
        if constexpr (std::tuple_size<Description>() == 0) {
            return std::string_view{};
        } else {
            return std::string_view{ std::tuple_element<0, Description>::type::value.data() };
        }
    }

    template<typename Filter>
    void addFilter(const std::string &key) {
        _subscriptionMatcher.addFilter<Filter>(key);
    }

    std::unordered_set<mdp::Topic> activeSubscriptions() const noexcept {
        std::lock_guard                lockGuard(_activeSubscriptionsLock);
        std::unordered_set<mdp::Topic> copy = _activeSubscriptions;
        return copy;
    }

    void shutdown() {
        _shutdownRequested = true;
    }

    bool notify(mdp::Message &&message) {
        message.protocolName = mdp::workerProtocol;
        message.command      = mdp::Command::Notify;
        message.serviceName  = serviceName.data(); // TODO unnecessary copy
        message.rbac         = _defaultRbacToken;
        return notificationHandlerForThisThread().send(std::move(message));
    }

    void disconnect() {
        auto msg = createMessage(mdp::Command::Disconnect);
        zmq::send(std::move(msg), *_workerSocket).assertSuccess();
        _workerSocket.reset();
        _pubSocket.reset();
    }

    void run() {
        if (!connectToBroker()) {
            return;
        }

        const auto heartbeatIntervalMs = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(_settings.heartbeatInterval).count());

        do {
            bool anythingReceived;
            do {
                anythingReceived = receiveDealerMessage();
                anythingReceived |= receiveSubscriptionMessage();
                anythingReceived |= receiveNotificationMessage();
            } while (anythingReceived);

            if (Clock::now() > _heartbeatAt && --_liveness == 0) {
                std::this_thread::sleep_for(_settings.workerReconnectInterval);
                if (!connectToBroker())
                    return;
            }

            if (Clock::now() > _heartbeatAt) {
                zmq::send(createMessage(mdp::Command::Heartbeat), *_workerSocket).assertSuccess();
                _heartbeatAt = Clock::now() + _settings.heartbeatInterval;

                cleanupNotificationHandlers();
            }
        } while (!_shutdownRequested
                 && zmq::invoke(zmq_poll, _pollerItems.data(), static_cast<int>(_pollerItems.size()), heartbeatIntervalMs).isValid());

        disconnect();
    }

protected:
    void setHandler(std::function<void(RequestContext &)> handler) {
        _handler = std::move(handler);
    }

private:
    std::string makeNotifyAddress() const noexcept {
        return std::format("inproc://workers{}-{}/notify", name, worker_detail::nextWorkerId());
    }

    NotificationHandler &notificationHandlerForThisThread() {
        const auto threadId = std::this_thread::get_id();
        {
            const std::shared_lock readLock(_notificationHandlersLock); // optimistic read -- thread is known/already initialised
            if (_notificationHandlers.contains(threadId)) {
                return _notificationHandlers.at(threadId);
            }
        }
        const std::unique_lock writeLock(_notificationHandlersLock); // nothing found -> need to allocate/initialise new socket
        _notificationHandlers.emplace(std::piecewise_construct, std::forward_as_tuple(threadId), std::forward_as_tuple(_context, _notifyAddress));
        return _notificationHandlers.at(threadId);
    }

    void cleanupNotificationHandlers() {
        const auto      expiryThreshold = Clock::now() - std::chrono::seconds(30); // cleanup unused handlers every 30 seconds -- TODO: move this to Settings
        std::lock_guard lock{ _notificationHandlersLock };

        auto            isExpired = [&expiryThreshold](const auto &p) {
            return p.second.lastUsed < expiryThreshold;
        };

        std::erase_if(_notificationHandlers, isExpired);
    }

    mdp::Message createMessage(mdp::Command command) const noexcept {
        mdp::Message message;
        message.protocolName = mdp::workerProtocol;
        message.command      = command;
        message.serviceName  = serviceName.data(); // TODO unnecessary copy
        message.rbac         = _defaultRbacToken;
        return message;
    }

    mdp::Message replyFromRequest(const mdp::Message &request) const noexcept {
        mdp::Message reply;
        reply.protocolName    = request.protocolName;
        reply.command         = mdp::Command::Final;
        reply.serviceName     = request.serviceName; // serviceName == clientSourceId
        reply.clientRequestID = request.clientRequestID;
        reply.topic           = request.topic;
        reply.rbac            = request.rbac;
        return reply;
    }

    bool receiveSubscriptionMessage() {
        zmq::MessageFrame frame;
        const auto        result = frame.receive(*_pubSocket, ZMQ_DONTWAIT);

        if (!result) {
            return false;
        }

        std::string_view data = frame.data();

        if (data.size() < 2 || !(data[0] == '\x0' || data[0] == '\x1')) {
            // will never happen if the broker works correctly (we could assert for inproc brokers)
            return true;
        }

        const auto                topicString = data.substr(1);
        std::optional<mdp::Topic> subscription;
        try {
            subscription = mdp::Topic::fromZmqTopic(topicString);
        } catch (...) {
            return false;
        }
        if (subscription->service() != name) {
            return true;
        }

        // this assumes that the broker does the subscribe/unsubscribe counting
        // for multiple clients and sends us a single sub/unsub for each topic
        if (data[0] == '\x1') {
            std::lock_guard lockGuard(_activeSubscriptionsLock);
            _activeSubscriptions.insert(*subscription);
        } else {
            std::lock_guard lockGuard(_activeSubscriptionsLock);
            _activeSubscriptions.erase(*subscription);
        }

        return true;
    }

    bool receiveNotificationMessage() {
        if (auto message = zmq::receive<mdp::MessageFormat::WithoutSourceId>(_notifyListenerSocket)) {
            const auto currentSubscription      = mdp::Topic::fromMdpTopic(message->topic);

            const auto matchesNotificationTopic = [this, &currentSubscription](const auto &activeSubscription) {
                return _subscriptionMatcher(currentSubscription, activeSubscription);
            };

            // TODO what to do here if worker is disconnected?
            std::lock_guard lockGuard(_activeSubscriptionsLock);
            if (_workerSocket && std::any_of(_activeSubscriptions.begin(), _activeSubscriptions.end(), matchesNotificationTopic)) {
                zmq::send(std::move(*message), *_workerSocket).assertSuccess();
            }
            return true;
        }

        return false;
    }

    bool receiveDealerMessage() {
        if (auto message = zmq::receive<mdp::MessageFormat::WithoutSourceId>(*_workerSocket)) {
            handleDealerMessage(std::move(*message));
            return true;
        }

        return false;
    }

    void handleDealerMessage(mdp::Message &&message) {
        _liveness = _settings.heartbeatLiveness;

        if (message.protocolName == mdp::workerProtocol) {
            switch (message.command) {
            case mdp::Command::Get:
            case mdp::Command::Set: {
                zmq::send(processRequest(std::move(message)), *_workerSocket).assertSuccess();
                return;
            }
            case mdp::Command::Heartbeat:
                return;
            case mdp::Command::Disconnect:
                if (message.data.asString() == "broker shutdown") {
                    _shutdownRequested = true;
                } else {
                    connectToBroker();
                }
                return;
            default:
                assert(false && "not implemented");
                return;
            }
        } else {
            assert(false && "not implemented");
        }
    }

    static constexpr auto permissionMap() {
        constexpr auto                                         N = std::tuple_size<Roles>();
        std::array<std::pair<std::string_view, Permission>, N> data;

        opencmw::MIME::detail::static_for<std::size_t, 0, N>([&](auto i) {
            using role = typename std::tuple_element<i, Roles>::type;
            data[i]    = std::pair(role::name(), role::rights());
        });

        return ConstExprMap<std::string_view, Permission, N>(std::move(data));
    }

    static constexpr auto _permissionsByRole = permissionMap();
    static constexpr auto _defaultPermission = _permissionsByRole.data.back().second;

    mdp::Message          processRequest(mdp::Message &&request) noexcept {
        const auto clientRole = parse_rbac::role(request.rbac.asString());
        const auto permission = _permissionsByRole.at(clientRole, _defaultPermission);

        if (request.command == mdp::Command::Get && !(permission == Permission::RW || permission == Permission::RO)) {
            auto errorReply  = replyFromRequest(request);
            errorReply.error = std::format("GET access denied to role '{}'", clientRole);
            return errorReply;
        } else if (request.command == mdp::Command::Set && !(permission == Permission::RW || permission == Permission::WO)) {
            auto errorReply  = replyFromRequest(request);
            errorReply.error = std::format("SET access denied to role '{}'", clientRole);
            return errorReply;
        }

        RequestContext context{ .request = std::move(request), .reply = replyFromRequest(context.request) };

        try {
            std::invoke(_handler, context);
            return std::move(context.reply);
        } catch (const std::exception &e) {
            auto errorReply  = replyFromRequest(context.request);
            errorReply.error = std::format("Caught exception for service '{}'\nrequest message: {}\nexception: {}", serviceName.data(), context.request.data, e.what());
            return errorReply;
        } catch (...) {
            auto errorReply  = replyFromRequest(context.request);
            errorReply.error = std::format("Caught unexpected exception for service '{}'\nrequest message: {}", serviceName.data(), context.request.data);
            return errorReply;
        }
    }

    bool connectToBroker() {
        _pollerItems[0].socket = nullptr;
        _pollerItems[1].socket = nullptr;
        _workerSocket.reset();
        _pubSocket.reset();

        _workerSocket.emplace(_context, ZMQ_DEALER);

        const auto routerEndpoint = opencmw::URI<STRICT>::factory(_brokerAddress).path(opencmw::majordomo::SUFFIX_ROUTER).build();
        if (!zmq::invoke(zmq_connect, *_workerSocket, mdp::toZeroMQEndpoint(routerEndpoint).data()).isValid()) {
            return false;
        }

        _pubSocket.emplace(_context, ZMQ_XPUB);

        const auto subEndpoint = opencmw::URI<STRICT>::factory(_brokerAddress).path(opencmw::majordomo::SUFFIX_SUBSCRIBE).build();
        if (!zmq::invoke(zmq_connect, *_pubSocket, mdp::toZeroMQEndpoint(subEndpoint).data()).isValid()) {
            _workerSocket.reset();
            return false;
        }

        auto ready = createMessage(mdp::Command::Ready);
        ready.data = IoBuffer(serviceDescription().data());
        zmq::send(std::move(ready), *_workerSocket).assertSuccess();

        _pollerItems[0].socket = _workerSocket->zmq_ptr;
        _pollerItems[0].events = ZMQ_POLLIN;
        _pollerItems[1].socket = _pubSocket->zmq_ptr;
        _pollerItems[1].events = ZMQ_POLLIN;
        _pollerItems[2].socket = _notifyListenerSocket.zmq_ptr;
        _pollerItems[2].events = ZMQ_POLLIN;

        _liveness              = _settings.heartbeatLiveness;
        _heartbeatAt           = Clock::now() + _settings.heartbeatInterval;

        return true;
    }
};

// Worker

namespace worker_detail {
template<ReflectableClass I, typename Protocol>
inline I deserialiseRequest(const mdp::Message &request) {
    I input;

    if (!request.data.empty()) {
        IoBuffer   tmp(request.data);
        const auto result = opencmw::deserialise<Protocol, opencmw::ProtocolCheck::ALWAYS>(tmp, input);
        if (!result.exceptions.empty()) {
            throw result.exceptions.front();
        }
    }

    return input;
}

template<ReflectableClass I>
inline I deserialiseRequest(const RequestContext &rawCtx) {
    if (rawCtx.mimeType == MIME::JSON || rawCtx.mimeType == MIME::HTML) {
        // We accept JSON requests with HTML output
        return deserialiseRequest<I, opencmw::Json>(rawCtx.request);
    } else if (rawCtx.mimeType == MIME::BINARY) {
        return deserialiseRequest<I, opencmw::YaS>(rawCtx.request);
    } else if (rawCtx.mimeType == MIME::CMWLIGHT) {
        return deserialiseRequest<I, opencmw::CmwLight>(rawCtx.request);
    }

    throw std::runtime_error(std::format("MIME type '{}' not supported", rawCtx.mimeType.typeName()));
}

template<typename Protocol>
inline void serialiseAndWriteToBody(RequestContext &rawCtx, const ReflectableClass auto &output) {
    IoBuffer buffer;
    opencmw::serialise<Protocol>(buffer, output);
    rawCtx.reply.data = std::move(buffer);
}

inline void writeResult(std::string_view workerName, RequestContext &rawCtx, const auto &replyContext, const auto &output) {
    auto       replyQuery    = query::serialise(replyContext);
    const auto baseUri       = rawCtx.reply.topic.empty() ? rawCtx.request.topic : rawCtx.reply.topic;
    const auto topicUri      = mdp::Message::URI::factory(baseUri).setQuery(std::move(replyQuery)).build();

    rawCtx.reply.topic       = topicUri;
    const auto replyMimetype = query::getMimeType(replyContext);
    const auto mimeType      = replyMimetype != MIME::UNKNOWN ? replyMimetype : rawCtx.mimeType;
    if (mimeType == MIME::JSON) {
        serialiseAndWriteToBody<opencmw::Json>(rawCtx, output);
        return;
    } else if (mimeType == MIME::BINARY) {
        serialiseAndWriteToBody<opencmw::YaS>(rawCtx, output);
        return;
    } else if (mimeType == MIME::CMWLIGHT) {
        serialiseAndWriteToBody<opencmw::CmwLight>(rawCtx, output);
        return;
    } else if (mimeType == MIME::HTML) {
        using namespace std::string_literals;
        std::stringstream stream;
        mustache::serialise(cmrc::assets::get_filesystem(), std::string(workerName), stream,
                std::pair<std::string, const decltype(output) &>{ "result"s, output },
                std::pair<std::string, const RequestContext &>{ "rawCtx"s, rawCtx });
        // TODO move the data?
        const auto buf    = stream.str();
        rawCtx.reply.data = IoBuffer(buf.data(), buf.size());
        return;
    }

    throw std::runtime_error(std::format("MIME type '{}' not supported", mimeType.typeName()));
}

inline void writeResultFull(std::string_view workerName, RequestContext &rawCtx, const auto &requestContext, const auto &replyContext, const auto &input, const auto &output) {
    auto       replyQuery    = query::serialise(replyContext);
    const auto baseUri       = rawCtx.reply.topic.empty() ? rawCtx.request.topic : rawCtx.reply.topic;
    const auto topicUri      = mdp::Message::URI::factory(baseUri).setQuery(std::move(replyQuery)).build();

    rawCtx.reply.topic       = topicUri;
    const auto replyMimetype = query::getMimeType(replyContext);
    const auto mimeType      = replyMimetype != MIME::UNKNOWN ? replyMimetype : rawCtx.mimeType;
    if (mimeType == MIME::JSON) {
        serialiseAndWriteToBody<opencmw::Json>(rawCtx, output);
        return;
    } else if (mimeType == MIME::BINARY) {
        serialiseAndWriteToBody<opencmw::YaS>(rawCtx, output);
        return;
    } else if (mimeType == MIME::CMWLIGHT) {
        serialiseAndWriteToBody<opencmw::CmwLight>(rawCtx, output);
        return;
    } else if (mimeType == MIME::HTML) {
        using namespace std::string_literals;
        std::stringstream stream;

        try {
            mustache::serialise(cmrc::assets::get_filesystem(), std::string(workerName), stream,
                    std::pair<std::string, const decltype(output) &>{ "result"s, output },
                    std::pair<std::string, const decltype(input) &>{ "input"s, input },
                    std::pair<std::string, const decltype(requestContext) &>{ "requestContext"s, requestContext },
                    std::pair<std::string, const decltype(replyContext) &>{ "replyContext"s, replyContext });
            const auto buf = stream.str();
            // TODO move data?
            rawCtx.reply.data = IoBuffer(buf.data(), buf.size());
        } catch (const ProtocolException &e) {
            rawCtx.reply.error = e.what();
        } catch (const std::exception &e) {
            rawCtx.reply.error = e.what();
        } catch (...) {
            rawCtx.reply.error = "Unexpected exception";
        }

        return;
    }

    throw std::runtime_error(std::format("MIME type '{}' not supported", mimeType.typeName()));
}

template<typename Worker, ReflectableClass ContextType, ReflectableClass InputType, ReflectableClass OutputType>
struct HandlerImpl {
    using CallbackFunction = std::function<void(RequestContext &, const ContextType &, const InputType &, ContextType &, OutputType &)>;

    CallbackFunction _callback;

    explicit HandlerImpl(CallbackFunction callback)
        : _callback(std::forward<CallbackFunction>(callback)) {
    }

    void operator()(RequestContext &rawCtx) {
        const auto  reqTopic          = rawCtx.request.topic;
        const auto  queryMap          = reqTopic.queryParamMap();

        ContextType requestCtx        = query::deserialise<ContextType>(queryMap);
        ContextType replyCtx          = requestCtx;
        const auto  requestedMimeType = query::getMimeType(requestCtx);
        //  no MIME type given -> map default to BINARY
        rawCtx.mimeType  = requestedMimeType == MIME::UNKNOWN ? MIME::BINARY : requestedMimeType;

        const auto input = deserialiseRequest<InputType>(rawCtx);

        OutputType output;
        _callback(rawCtx, requestCtx, input, replyCtx, output);
        try {
            writeResultFull(Worker::name, rawCtx, requestCtx, replyCtx, input, output);
        } catch (const ProtocolException &e) {
            throw std::runtime_error(e.what());
        }
    }
};

} // namespace worker_detail

// TODO docs, see worker_tests.cpp for a documented example
template<units::basic_fixed_string serviceName, ReflectableClass ContextType, ReflectableClass InputType, ReflectableClass OutputType, typename... Meta>
class Worker : public BasicWorker<serviceName, Meta...> {
public:
    using HandlerImpl      = worker_detail::HandlerImpl<Worker, ContextType, InputType, OutputType>;
    using CallbackFunction = typename HandlerImpl::CallbackFunction;

    explicit Worker(URI<STRICT> brokerAddress, CallbackFunction callback, const zmq::Context &context, Settings settings = {})
        : BasicWorker<serviceName, Meta...>(std::move(brokerAddress), HandlerImpl(std::move(callback)), context, settings) {
        query::registerTypes(ContextType(), *this);
    }

    template<typename BrokerType>
    explicit Worker(const BrokerType &broker, CallbackFunction callback)
        : BasicWorker<serviceName, Meta...>(broker, HandlerImpl(std::move(callback))) {
        query::registerTypes(ContextType(), *this);
    }

    bool notify(const ContextType &context, const OutputType &reply) {
        RequestContext rawCtx;
        rawCtx.reply.topic = URI<>(std::string(Worker::name));
        worker_detail::writeResult(Worker::name, rawCtx, context, reply);
        return BasicWorker<serviceName, Meta...>::notify(std::move(rawCtx.reply));
    }

protected:
    void setCallback(CallbackFunction callback) {
        BasicWorker<serviceName, Meta...>::setHandler(HandlerImpl(std::move(callback)));
    }
};

/**
 * Empty type that can be used e.g. as Input type without parameters
 */
struct Empty {};

} // namespace opencmw::majordomo

ENABLE_REFLECTION_FOR(opencmw::majordomo::RequestContext, mimeType);
// this replaces ENABLE_REFLECTION_FOR for the empty type
REFL_TYPE(opencmw::majordomo::Empty)
REFL_END

#endif
