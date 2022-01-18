#ifndef OPENCMW_MAJORDOMO_WORKER_H
#define OPENCMW_MAJORDOMO_WORKER_H

#include <majordomo/Broker.hpp>
#include <majordomo/Constants.hpp>
#include <majordomo/Debug.hpp>
#include <majordomo/QuerySerialiser.hpp>
#include <majordomo/Rbac.hpp>
#include <majordomo/Settings.hpp>
#include <majordomo/Utils.hpp>

#include <IoSerialiserCmwLight.hpp>
#include <IoSerialiserJson.hpp>
#include <IoSerialiserYaS.hpp>

#include <MIME.hpp>
#include <opencmw.hpp>
#include <Utils.hpp>

#include <fmt/format.h>

#include <array>
#include <atomic>
#include <chrono>
#include <concepts>
#include <shared_mutex>
#include <string>
#include <thread>

namespace opencmw::majordomo {

struct RequestContext {
    const MdpMessage                             request;
    MdpMessage                                   reply;
    MIME::MimeType                               mimeType = MIME::BINARY;
    std::unordered_map<std::string, std::string> htmlData;
};

template<typename T>
concept HandlesRequest = requires(T handler, RequestContext &context) { std::invoke(handler, context); };

namespace detail {
    inline int nextWorkerId() {
        static std::atomic<int> idCounter = 0;
        return ++idCounter;
    }
} // namespace detail

template<HandlesRequest RequestHandler, rbac::role... Roles>
class BasicWorker {
private:
    using Clock     = std::chrono::steady_clock;
    using Timestamp = std::chrono::time_point<Clock>;

    struct NotificationHandler {
        Socket    socket;
        Timestamp lastUsed;

        explicit NotificationHandler(const Context &context, std::string_view notifyAddress)
            : socket(context, ZMQ_PUSH) {
            zmq_invoke(zmq_connect, socket, notifyAddress.data()).assertSuccess();
        }

        bool send(MdpMessage &&message) {
            lastUsed = Clock::now();
            return message.send(socket).isValid();
        }
    };

protected:
    const std::string _serviceName;

private:
    RequestHandler                                           _handler;
    const Settings                                           _settings;
    const opencmw::URI<STRICT>                               _brokerAddress;
    std::string                                              _serviceDescription;
    std::atomic<bool>                                        _shutdownRequested = false;
    int                                                      _liveness          = 0;
    Timestamp                                                _heartbeatAt;
    const Context                                           &_context;
    std::optional<Socket>                                    _workerSocket;
    std::optional<Socket>                                    _pubSocket;
    std::array<zmq_pollitem_t, 3>                            _pollerItems;
    SubscriptionMatcher                                      _subscriptionMatcher;
    std::set<URI<RELAXED>>                                   _activeSubscriptions;
    Socket                                                   _notifyListenerSocket;
    std::unordered_map<std::thread::id, NotificationHandler> _notificationHandlers;
    std::shared_mutex                                        _notificationHandlersLock;
    const std::string                                        _notifyAddress;
    static constexpr auto                                    _defaultRbacToken = std::string_view("RBAC=NONE,");

public:
    explicit BasicWorker(std::string_view serviceName, opencmw::URI<STRICT> brokerAddress, RequestHandler &&handler, const Context &context, Settings settings = {})
        : _serviceName{ std::move(serviceName) }, _handler{ std::forward<RequestHandler>(handler) }, _settings{ std::move(settings) }, _brokerAddress{ std::move(brokerAddress) }, _context(context), _notifyListenerSocket(_context, ZMQ_PULL), _notifyAddress(makeNotifyAddress(serviceName)) {
        zmq_invoke(zmq_bind, _notifyListenerSocket, _notifyAddress.data()).assertSuccess();
    }

    template<typename BrokerType>
    explicit BasicWorker(std::string_view serviceName, const BrokerType &broker, RequestHandler &&handler)
        : BasicWorker(serviceName, INPROC_BROKER, std::forward<RequestHandler>(handler), broker.context, broker.settings) {
    }

    // Sets the service description
    void setServiceDescription(std::string description) {
        _serviceDescription = std::move(description);
    }

    template<typename Filter>
    void addFilter(const std::string &key) {
        _subscriptionMatcher.addFilter<Filter>(key);
    }

    void shutdown() {
        _shutdownRequested = true;
    }

    bool notify(MdpMessage &&message) {
        message.setProtocol(Protocol::Worker);
        message.setCommand(Command::Notify);
        message.setServiceName(_serviceName, MessageFrame::dynamic_bytes_tag{});
        message.setRbacToken(_defaultRbacToken, MessageFrame::dynamic_bytes_tag{});
        return notificationHandlerForThisThread().send(std::move(message));
    }

    void disconnect() {
        auto msg = createMessage(Command::Disconnect);
        msg.send(*_workerSocket).assertSuccess();
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
                auto heartbeat = createMessage(Command::Heartbeat);
                heartbeat.send(*_workerSocket).assertSuccess();
                _heartbeatAt = Clock::now() + _settings.heartbeatInterval;

                cleanupNotificationHandlers();
            }
        } while (!_shutdownRequested
                 && zmq_invoke(zmq_poll, _pollerItems.data(), static_cast<int>(_pollerItems.size()), heartbeatIntervalMs).isValid());

        disconnect();
    }

private:
    std::string makeNotifyAddress(std::string_view &serviceName) const noexcept {
        return fmt::format("inproc://workers/{}-{}/notify", serviceName, detail::nextWorkerId());
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

    MdpMessage createMessage(Command command) const noexcept {
        auto message = MdpMessage::createWorkerMessage(command);
        message.setServiceName(_serviceName, MessageFrame::dynamic_bytes_tag{});
        message.setRbacToken(_defaultRbacToken, MessageFrame::dynamic_bytes_tag{});
        return message;
    }

    MdpMessage replyFromRequest(const MdpMessage &request) const noexcept {
        MdpMessage reply;
        reply.setProtocol(request.protocol());
        reply.setCommand(Command::Final);
        reply.setServiceName(request.serviceName(), MessageFrame::dynamic_bytes_tag{});
        reply.setClientSourceId(request.clientSourceId(), MessageFrame::dynamic_bytes_tag{});
        reply.setClientRequestId(request.clientRequestId(), MessageFrame::dynamic_bytes_tag{});
        reply.setTopic(request.topic(), MessageFrame::dynamic_bytes_tag{});
        reply.setRbacToken(request.rbacToken(), MessageFrame::dynamic_bytes_tag{});
        return reply;
    }

    bool receiveSubscriptionMessage() {
        MessageFrame frame;
        const auto   result = frame.receive(*_pubSocket, ZMQ_DONTWAIT);

        if (!result) {
            return false;
        }

        std::string_view data = frame.data();

        if (data.size() < 2 || !(data[0] == '\x0' || data[0] == '\x1')) {
            // will never happen if the broker works correctly (we could assert for inproc brokers)
            debug() << "Unexpected subscribe/unsubscribe message: " << data;
            return true;
        }

        const auto topicString = data.substr(1);
        const auto topic       = URI<RELAXED>(std::string(topicString));

        // this assumes that the broker does the subscribe/unsubscribe counting
        // for multiple clients and sends us a single sub/unsub for each topic
        if (data[0] == '\x1') {
            _activeSubscriptions.insert(topic);
        } else {
            _activeSubscriptions.erase(topic);
        }

        return true;
    }

    bool receiveNotificationMessage() {
        if (auto message = MdpMessage::receive(_notifyListenerSocket)) {
            const auto topic                    = URI<RELAXED>(std::string(message->topic()));
            const auto matchesNotificationTopic = [this, &topic](const auto &subscription) {
                return _subscriptionMatcher(topic, subscription);
            };

            // TODO what to do here if worker is disconnected?
            if (_workerSocket && std::any_of(_activeSubscriptions.begin(), _activeSubscriptions.end(), matchesNotificationTopic)) {
                message->send(*_workerSocket).assertSuccess();
            }
            return true;
        }

        return false;
    }

    bool receiveDealerMessage() {
        if (auto message = MdpMessage::receive(*_workerSocket)) {
            handleDealerMessage(std::move(*message));
            return true;
        }

        return false;
    }

    void handleDealerMessage(MdpMessage &&message) {
        _liveness = _settings.heartbeatLiveness;

        if (!message.isValid()) {
            debug() << "invalid MdpMessage received\n";
            return;
        }

        if (message.isWorkerMessage()) {
            switch (message.command()) {
            case Command::Get:
            case Command::Set: {
                auto reply = processRequest(std::move(message));
                reply.send(*_workerSocket).assertSuccess();
                return;
            }
            case Command::Heartbeat:
                return;
            case Command::Disconnect:
                if (message.body() == "broker shutdown") {
                    _shutdownRequested = true;
                } else {
                    connectToBroker();
                }
                return;
            default:
                assert(!"not implemented");
                return;
            }
        } else {
            assert(!"not implemented");
        }
    }

    MdpMessage processRequest(MdpMessage &&request) noexcept {
        const auto clientRole = rbac::parse::role(request.rbacToken());
        const auto permission = rbac::permission<Roles...>(clientRole);

        if (request.command() == Command::Get && !(permission == rbac::Permission::RW || permission == rbac::Permission::RO)) {
            auto errorReply = replyFromRequest(request);
            errorReply.setError(fmt::format("GET access denied to role '{}'", clientRole), MessageFrame::dynamic_bytes_tag{});
            return errorReply;
        } else if (request.command() == Command::Set && !(permission == rbac::Permission::RW || permission == rbac::Permission::WO)) {
            auto errorReply = replyFromRequest(request);
            errorReply.setError(fmt::format("SET access denied to role '{}'", clientRole), MessageFrame::dynamic_bytes_tag{});
            return errorReply;
        }

        RequestContext context{ .request = std::move(request), .reply = replyFromRequest(context.request), .htmlData = {} };

        try {
            std::invoke(_handler, context);
            return std::move(context.reply);
        } catch (const std::exception &e) {
            auto errorReply = replyFromRequest(context.request);
            errorReply.setError(fmt::format("Caught exception for service '{}'\nrequest message: {}\nexception: {}", _serviceName, context.request.body(), e.what()), MessageFrame::dynamic_bytes_tag{});
            return errorReply;
        } catch (...) {
            auto errorReply = replyFromRequest(context.request);
            errorReply.setError(fmt::format("Caught unexpected exception for service '{}'\nrequest message: {}", _serviceName, context.request.body()), MessageFrame::dynamic_bytes_tag{});
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
        if (!zmq_invoke(zmq_connect, *_workerSocket, toZeroMQEndpoint(routerEndpoint).data()).isValid()) {
            return false;
        }

        _pubSocket.emplace(_context, ZMQ_XPUB);

        const auto subEndpoint = opencmw::URI<STRICT>::factory(_brokerAddress).path(opencmw::majordomo::SUFFIX_SUBSCRIBE).build();
        if (!zmq_invoke(zmq_connect, *_pubSocket, toZeroMQEndpoint(subEndpoint).data()).isValid()) {
            _workerSocket.reset();
            return false;
        }

        auto ready = createMessage(Command::Ready);
        ready.setBody(_serviceDescription, MessageFrame::dynamic_bytes_tag{});
        ready.send(*_workerSocket).assertSuccess();

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

template<rbac::role... Roles, HandlesRequest RequestHandler>
BasicWorker(std::string_view, const opencmw::URI<> &, RequestHandler &&, const Context &, Settings) -> BasicWorker<RequestHandler, Roles...>;

// use same roles as broker
template<rbac::role... Roles, HandlesRequest RequestHandler>
BasicWorker(std::string_view, const Broker<Roles...> &, RequestHandler &&) -> BasicWorker<RequestHandler, Roles...>;

// Worker

template<typename T, typename ContextType, typename InputType, typename OutputType>
concept MajordomoHandler = isReflectableClass<ContextType>() && isReflectableClass<InputType>() && isReflectableClass<OutputType>() && requires(T t, opencmw::majordomo::RequestContext ctx, const ContextType &requestCtx, const InputType &request, ContextType &replyCtx, OutputType &reply) {
    t.handle(ctx, requestCtx, request, replyCtx, reply);
};

namespace detail {
    template<ReflectableClass I, typename Protocol>
    inline I deserialiseRequest(const MdpMessage &request) {
        IoBuffer buffer;
        buffer.put<IoBuffer::MetaInfo::WITHOUT>(request.body());
        I          input;
        const auto result = opencmw::deserialise<Protocol, opencmw::ProtocolCheck::ALWAYS>(buffer, input);
        if (!result.exceptions.empty()) {
            throw result.exceptions.front();
        }

        return input;
    }

    template<ReflectableClass I>
    inline I deserialiseRequest(const RequestContext &rawCtx) {
        try {
            if (rawCtx.mimeType == MIME::JSON) {
                return deserialiseRequest<I, opencmw::Json>(rawCtx.request);
            } else if (rawCtx.mimeType == MIME::BINARY) {
                return deserialiseRequest<I, opencmw::YaS>(rawCtx.request);
            } else if (rawCtx.mimeType == MIME::CMWLIGHT) {
                // TODO the following line does not compile
                // return deserialiseRequest<I, opencmw::CmwLight>(rawCtx.request);
            }
        } catch (const ProtocolException &e) { // TODO if ProtocolException would inherit from std::exception, we could omit this catch/try and leave it to the generic exception handling in BasicMdpWorker
            throw std::runtime_error(std::string(e.what()));
        }

        throw std::runtime_error(fmt::format("MIME type '{}' not supported", rawCtx.mimeType.typeName()));
    }

    template<typename Protocol>
    inline void serialiseAndWriteToBody(RequestContext & rawCtx, const ReflectableClass auto &output) {
        IoBuffer buffer;
        opencmw::serialise<Protocol>(buffer, output);
        rawCtx.reply.setBody(buffer.asString(), MessageFrame::dynamic_bytes_tag{});
    }

    inline void writeResult(RequestContext & rawCtx, const auto &replyContext, const auto &output) {
        auto       replyQuery = query::serialise(replyContext);
        const auto baseUri    = URI<RELAXED>(std::string(rawCtx.reply.topic().empty() ? rawCtx.request.topic() : rawCtx.reply.topic()));
        const auto topicUri   = URI<RELAXED>::factory(baseUri).setQuery(std::move(replyQuery)).build();

        rawCtx.reply.setTopic(topicUri.str, MessageFrame::dynamic_bytes_tag{});
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
        }

        throw std::runtime_error(fmt::format("MIME type '{}' not supported", mimeType.typeName()));
    }

    template<ReflectableClass ContextType, ReflectableClass InputType, ReflectableClass OutputType, MajordomoHandler<ContextType, InputType, OutputType> Handler>
    class HandlerImpl {
        Handler _handler;

    public:
        explicit HandlerImpl(Handler handler)
            : _handler(std::forward<Handler>(handler)) {}

        void operator()(RequestContext &rawCtx) {
            const auto  reqTopic          = opencmw::URI<RELAXED>(std::string(rawCtx.request.topic()));
            const auto  queryMap          = reqTopic.queryParamMap();

            ContextType requestCtx        = query::deserialise<ContextType>(queryMap);
            ContextType replyCtx          = requestCtx;
            const auto  requestedMimeType = query::getMimeType(requestCtx);
            //  no MIME type given -> map default to BINARY
            rawCtx.mimeType  = requestedMimeType == MIME::UNKNOWN ? MIME::BINARY : requestedMimeType;

            const auto input = deserialiseRequest<InputType>(rawCtx);

            OutputType output;
            _handler.handle(rawCtx, requestCtx, input, replyCtx, output);
            writeResult(rawCtx, replyCtx, output);
        }
    };

} // namespace detail

// TODO docs, see worker_tests.cpp for a documented example
template<ReflectableClass ContextType, ReflectableClass InputType, ReflectableClass OutputType, MajordomoHandler<ContextType, InputType, OutputType> UserHandler, rbac::role... Roles>
class Worker : public BasicWorker<detail::HandlerImpl<ContextType, InputType, OutputType, UserHandler>, Roles...> {
public:
    explicit Worker(std::string_view serviceName, URI<STRICT> brokerAddress, UserHandler userHandler, const Context &context, Settings settings = {})
        : BasicWorker<detail::HandlerImpl<ContextType, InputType, OutputType, UserHandler, Roles...>>(serviceName, std::move(brokerAddress), detail::HandlerImpl<ContextType, InputType, OutputType, UserHandler>(std::forward<UserHandler>(userHandler)), context, settings) {
        query::registerTypes(ContextType(), *this);
    }

    template<typename BrokerType>
    explicit Worker(std::string_view serviceName, const BrokerType &broker, UserHandler userHandler)
        : BasicWorker<detail::HandlerImpl<ContextType, InputType, OutputType, UserHandler>, Roles...>(serviceName, broker, detail::HandlerImpl<ContextType, InputType, OutputType, UserHandler>(std::forward<UserHandler>(userHandler))) {
        query::registerTypes(ContextType(), *this);
    }

    bool notify(const ContextType &context, const OutputType &reply) {
        return notify("", context, reply);
    }

    bool notify(std::string_view path, const ContextType &context, const OutputType &reply) {
        // Java does _serviceName + path, do we want that?
        // std::string topicString = this->_serviceName;
        // topicString.append(path);
        // auto topicURI = URI<RELAXED>(topicString);

        auto       query    = query::serialise(context);
        const auto topicURI = URI<RELAXED>::factory(URI<RELAXED>(std::string(path))).setQuery(std::move(query)).build();

        // TODO java does subscription handling here which BasicMdpWorker does in the sender thread. check what we need there.

        RequestContext rawCtx;
        rawCtx.reply.setTopic(topicURI.str, MessageFrame::dynamic_bytes_tag{});
        detail::writeResult(rawCtx, context, reply);
        return BasicWorker<detail::HandlerImpl<ContextType, InputType, OutputType, UserHandler>, Roles...>::notify(std::move(rawCtx.reply));
    }
};

} // namespace opencmw::majordomo

#endif
