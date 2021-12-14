#ifndef OPENCMW_MAJORDOMO_WORKER_H
#define OPENCMW_MAJORDOMO_WORKER_H

#include "Broker.hpp"
#include "Message.hpp"

#include <majordomo/Broker.hpp>
#include <majordomo/Constants.hpp>
#include <majordomo/Debug.hpp>
#include <majordomo/Settings.hpp>
#include <majordomo/Utils.hpp>

#include <MIME.hpp>

#include <fmt/format.h>

#include <array>
#include <atomic>
#include <chrono>
#include <concepts>
#include <string>
#include <thread>

namespace opencmw::majordomo {

struct RequestContext {
    const MdpMessage                             request;
    MdpMessage                                   reply;
    opencmw::MIME::MimeType                      mimeType = opencmw::MIME::BINARY;
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

template<HandlesRequest RequestHandler>
class BasicMdpWorker {
private:
    using Clock       = std::chrono::steady_clock;
    using Timestamp   = std::chrono::time_point<Clock>;

    using EndpointURI = opencmw::URI<>;
    using TopicURI    = opencmw::URI<RELAXED>;

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

    RequestHandler                                           _handler;
    const Settings                                           _settings;
    const EndpointURI                                        _brokerAddress;
    const std::string                                        _serviceName;
    std::string                                              _serviceDescription;
    std::string                                              _rbacRole;
    std::atomic<bool>                                        _shutdownRequested = false;
    int                                                      _liveness          = 0;
    Timestamp                                                _heartbeatAt;
    const Context                                           &_context;
    std::optional<Socket>                                    _workerSocket;
    std::optional<Socket>                                    _pubSocket;
    std::array<zmq_pollitem_t, 3>                            _pollerItems;
    std::set<TopicURI>                                       _activeSubcriptions;
    Socket                                                   _notifyListenerSocket;
    std::unordered_map<std::thread::id, NotificationHandler> _notificationHandlers;
    std::mutex                                               _notificationHandlersLock;
    const std::string                                        _notifyAddress;

public:
    explicit BasicMdpWorker(std::string_view serviceName, EndpointURI brokerAddress, RequestHandler &&handler, const Context &context, Settings settings = {})
        : _handler{ std::forward<RequestHandler>(handler) }, _settings{ std::move(settings) }, _brokerAddress{ std::move(brokerAddress) }, _serviceName{ std::move(serviceName) }, _context(context), _notifyListenerSocket(_notifyContext, ZMQ_PULL), _notifyAddress(makeNotifyAddress(serviceName)) {
        zmq_invoke(zmq_bind, _notifyListenerSocket, _notifyAddress.data()).assertSuccess();
    }

    explicit BasicMdpWorker(std::string_view serviceName, const Broker &broker, RequestHandler &&handler)
        : BasicMdpWorker(serviceName, INPROC_BROKER, std::forward<RequestHandler>(handler), broker.context, broker.settings) {
    }

    // Sets the service description
    void setServiceDescription(std::string description) {
        _serviceDescription = std::move(description);
    }

    void setRbacRole(std::string rbac) {
        _rbacRole = std::move(rbac);
    }

    void shutdown() {
        _shutdownRequested = true;
    }

    bool notify(MdpMessage &&message) {
        message.setProtocol(Protocol::Worker);
        message.setCommand(Command::Notify);
        message.setServiceName(_serviceName, MessageFrame::dynamic_bytes_tag{});
        message.setRbacToken(_rbacRole, MessageFrame::dynamic_bytes_tag{});
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
    }

private:
    std::string makeNotifyAddress(std::string_view serviceName) {
        const auto id = detail::nextWorkerId();
        return fmt::format("inproc://workers/{}-{}/notify", serviceName, std::to_string(id));
    }

    NotificationHandler &notificationHandlerForThisThread() {
        std::lock_guard lock{ _notificationHandlersLock };
        const auto      it = _notificationHandlers.try_emplace(std::this_thread::get_id(), _context, _notifyAddress);
        return it.first->second;
    }

    void cleanupNotificationHandlers() {
        // cleanup unused handlers every 30 seconds
        const auto      expiryThreshold = Clock::now() - std::chrono::seconds(30);
        std::lock_guard lock{ _notificationHandlersLock };

        for (auto it = _notificationHandlers.begin(); it != _notificationHandlers.end(); ++it) {
            if (it->second.lastUsed < expiryThreshold) {
                it = _notificationHandlers.erase(it);
            }
        }
    }

    MdpMessage createMessage(Command command) {
        auto message = MdpMessage::createWorkerMessage(command);
        message.setServiceName(_serviceName, MessageFrame::dynamic_bytes_tag{});
        message.setRbacToken(_rbacRole, MessageFrame::dynamic_bytes_tag{});
        return message;
    }

    MdpMessage replyFromRequest(const MdpMessage &request) {
        MdpMessage reply;
        reply.setProtocol(request.protocol());
        reply.setCommand(Command::Final);
        reply.setServiceName(request.serviceName(), MessageFrame::dynamic_bytes_tag{});
        reply.setClientSourceId(request.clientSourceId(), MessageFrame::dynamic_bytes_tag{});
        reply.setClientRequestId(request.clientRequestId(), MessageFrame::dynamic_bytes_tag{});
        reply.setTopic(request.topic(), MessageFrame::dynamic_bytes_tag{});
        reply.setRbacToken(_rbacRole, MessageFrame::dynamic_bytes_tag{});
        return reply;
    }

    // TODO duplicated from Broker. worth sharing?
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
        const auto topic       = parseTopicURI(topicString);
        assert(topic); // TODO currently relaxed URI parsing always succeeds, handle error once this changes

        // this assumes that the broker does the subscribe/unsubscribe counting
        // for multiple clients and sends us a single sub/unsub for each topic
        if (data[0] == '\x1') {
            _activeSubcriptions.insert(*topic);
        } else {
            _activeSubcriptions.erase(*topic);
        }

        return true;
    }

    bool receiveNotificationMessage() {
        if (auto message = MdpMessage::receive(_notifyListenerSocket)) {
            const auto topic = parseTopicURI(message->topic());
            assert(topic);
            const auto matchesNotificationTopic = [&topic](const TopicURI &subscription) {
                static const SubscriptionMatcher matcher;
                return matcher(*topic, subscription);
            };

            // TODO what to do here if worker is disconnected?
            if (_workerSocket && std::any_of(_activeSubcriptions.begin(), _activeSubcriptions.end(), matchesNotificationTopic)) {
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

    MdpMessage processRequest(MdpMessage &&request) {
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

        const auto routerEndpoint = EndpointURI::factory(_brokerAddress).path(opencmw::majordomo::SUFFIX_ROUTER).build();
        if (!zmq_invoke(zmq_connect, *_workerSocket, toZeroMQEndpoint(routerEndpoint).data()).isValid()) {
            return false;
        }

        _pubSocket.emplace(_context, ZMQ_XPUB);

        const auto subEndpoint = EndpointURI::factory(_brokerAddress).path(opencmw::majordomo::SUFFIX_SUBSCRIBE).build();
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

template<HandlesRequest RequestHandler>
BasicMdpWorker(std::string_view, const opencmw::URI<> &, RequestHandler &&, const Context &, Settings) -> BasicMdpWorker<RequestHandler>;

template<HandlesRequest RequestHandler>
BasicMdpWorker(std::string_view, const Broker &, RequestHandler &&) -> BasicMdpWorker<RequestHandler>;

} // namespace opencmw::majordomo

#endif
