#ifndef OPENCMW_MAJORDOMO_RESTBACKEND_P_H
#define OPENCMW_MAJORDOMO_RESTBACKEND_P_H

// STD
#include <charconv>
#include <filesystem>
#include <iostream>
#include <optional>
#include <regex>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wuninitialized"
#pragma GCC diagnostic ignored "-Wuseless-cast"
#define CPPHTTPLIB_THREAD_POOL_COUNT 8
#include <httplib.h>
#pragma GCC diagnostic pop

// Core
#include <MIME.hpp>
#include <URI.hpp>

#include <IoSerialiserJson.hpp>
#include <MustacheSerialiser.hpp>
#include <ThreadAffinity.hpp>

// Majordomo
#include <majordomo/Broker.hpp>
#include <majordomo/Message.hpp>

#include <refl.hpp>

#include <cmrc/cmrc.hpp>
#include <utility>
CMRC_DECLARE(assets);

struct FormData {
    std::unordered_map<std::string, std::string> fields;
};
ENABLE_REFLECTION_FOR(FormData, fields)

struct Service {
    std::string name;
    std::string description;

    Service(std::string name_, std::string description_)
        : name{ std::move(name_) }, description{ std::move(description_) } {}
};
ENABLE_REFLECTION_FOR(Service, name, description)

struct ServicesList {
    std::vector<Service> services;
};
ENABLE_REFLECTION_FOR(ServicesList, services)

namespace opencmw::majordomo {

using namespace std::chrono_literals;

constexpr auto        HTTP_OK                             = 200;
constexpr auto        HTTP_ERROR                          = 500;
constexpr auto        DEFAULT_REST_PORT                   = 8080;
constexpr auto        REST_POLLING_TIME                   = 10s;
constexpr auto        UPDATER_POLLING_TIME                = 1s;
constexpr auto        UNUSED_SUBSCRIPTION_EXPIRATION_TIME = 30s;
constexpr std::size_t MAX_CACHED_REPLIES                  = 32;

namespace detail {
// Provides a safe alternative to getenv
const char *getEnvFilenameOr(const char *field, const char *defaultValue) {
    const char *result = ::getenv(field);
    if (result == nullptr) {
        result = defaultValue;
    }

    if (!std::filesystem::exists(result)) {
        throw opencmw::startup_error(fmt::format("File {} not found. Current path is {}", result, std::filesystem::current_path().string()));
    }
    return result;
}
} // namespace detail

struct HTTPS {
    constexpr static std::string_view DEFAULT_REST_SCHEME = "https";
    httplib::SSLServer                _svr;

    HTTPS()
        : _svr(detail::getEnvFilenameOr("OPENCMW_REST_CERT_FILE", "demo_public.crt"), detail::getEnvFilenameOr("OPENCMW_REST_PRIVATE_KEY_FILE", "demo_private.key")) {}
};

struct PLAIN_HTTP {
    constexpr static std::string_view DEFAULT_REST_SCHEME = "https";
    httplib::Server                   _svr;
};

namespace detail {
using PollingIndex = std::uint64_t;

using ReadLock     = std::shared_lock<std::shared_mutex>;
using WriteLock    = std::unique_lock<std::shared_mutex>;

enum class RestMethod {
    Get,
    Subscribe,
    LongPoll,
    Post,
    Invalid
};

std::string_view acceptedMimeForRequest(const auto &request) {
    static constexpr std::array<std::string_view, 3> acceptableMimeTypes = {
        MIME::JSON.typeName(), MIME::HTML.typeName(), "application/x-opencmw-test-format"
    };
    auto accepted = [](auto format) {
        const auto it = std::find(acceptableMimeTypes.cbegin(), acceptableMimeTypes.cend(), format);
        return std::make_pair(
                it != acceptableMimeTypes.cend(),
                it);
    };

    if (request.has_param("contentType")) {
        std::string format = request.get_param_value("contentType");
        if (const auto [found, where] = accepted(format); found) {
            return *where;
        }
    }

    auto        isDelimiter  = [](char c) { return c == ' ' || c == ','; };
    const auto &acceptHeader = request.get_header_value("Accept");
    auto        from         = acceptHeader.cbegin();
    const auto  end          = acceptHeader.cend();

    while (from != end) {
        from    = std::find_if_not(from, end, isDelimiter);
        auto to = std::find_if(from, end, isDelimiter);
        if (from != end) {
            std::string_view format(from, to);
            if (const auto [found, where] = accepted(format); found) {
                return *where;
            }
        }

        from = to;
    }

    return acceptableMimeTypes[0];
}

bool respondWithError(auto &response, std::string_view message) {
    response.status = HTTP_ERROR;
    response.set_content(message.data(), MIME::TEXT.typeName().data());
    return true;
};

bool respondWithServicesList(auto &broker, const httplib::Request &request, httplib::Response &response) {
    // Mmi is not a MajordomoWorker, so it doesn't know JSON (TODO)
    const auto acceptedFormat = acceptedMimeForRequest(request);

    if (acceptedFormat == MIME::JSON.typeName()) {
        std::vector<std::string> serviceNames;
        // TODO: Should this be synchronized?
        broker.forEachService([&](std::string_view name, std::string_view) {
            serviceNames.emplace_back(std::string(name));
        });
        response.status = HTTP_OK;

        opencmw::IoBuffer buffer;
        // opencmw::serialise<opencmw::Json>(buffer, serviceNames);
        IoSerialiser<opencmw::Json, decltype(serviceNames)>::serialise(buffer, FieldDescriptionShort{}, serviceNames);
        response.set_content(buffer.asString().data(), MIME::JSON.typeName().data());
        return true;

    } else if (acceptedFormat == MIME::HTML.typeName()) {
        response.set_chunked_content_provider(
                MIME::HTML.typeName().data(),
                [&broker](std::size_t /*offset*/, httplib::DataSink &sink) {
                    ServicesList servicesList;
                    broker.forEachService([&](std::string_view name, std::string_view description) {
                        servicesList.services.emplace_back(std::string(name), std::string(description));
                    });

                    // sort services, move mmi. services to the end
                    auto serviceLessThan = [](const auto &lhs, const auto &rhs) {
                        const auto lhsIsMmi = lhs.name.starts_with("mmi.");
                        const auto rhsIsMmi = rhs.name.starts_with("mmi.");
                        if (lhsIsMmi != rhsIsMmi) {
                            return rhsIsMmi;
                        }
                        return lhs.name < rhs.name;
                    };
                    std::sort(servicesList.services.begin(), servicesList.services.end(), serviceLessThan);

                    using namespace std::string_literals;
                    mustache::serialise(cmrc::assets::get_filesystem(), "ServicesList", sink.os,
                            std::pair<std::string, const ServicesList &>{ "servicesList"s, servicesList });
                    sink.done();
                    return true;
                });
        return true;

    } else {
        return respondWithError(response, "Requested an unsupported response type");
    }
}

struct SubscriptionInfo {
    std::string serviceName;
    std::string topicName;
    auto        operator<=>(const SubscriptionInfo &other) const = default;
    bool        operator==(const SubscriptionInfo &other) const  = default;
};

struct Connection {
    zmq::Socket      notificationSubscriptionSocket;
    zmq::Socket      requestResponseSocket;
    SubscriptionInfo subscriptionInfo;

    using Timestamp    = std::chrono::time_point<std::chrono::system_clock>;
    Timestamp lastUsed = std::chrono::system_clock::now();

private:
    mutable std::shared_mutex   _cachedRepliesMutex;
    std::deque<std::string>     _cachedReplies; // Ring buffer?
    PollingIndex                _nextPollingIndex = 0;
    std::condition_variable_any _pollingIndexCV;

    std::atomic_int             _refCount = 1;

    // Here be dragons! This is not to be used after
    // the connection was involved in any threading code
    Connection(Connection &&other) noexcept
        : notificationSubscriptionSocket(std::move(other.notificationSubscriptionSocket))
        , requestResponseSocket(std::move(other.requestResponseSocket))
        , subscriptionInfo(std::move(other.subscriptionInfo))
        , lastUsed(std::move(other.lastUsed))
        , _cachedReplies(std::move(other._cachedReplies))
        , _nextPollingIndex(other._nextPollingIndex) {
    }

public:
    Connection(const zmq::Context &context, SubscriptionInfo _subscriptionInfo)
        : notificationSubscriptionSocket(context, ZMQ_DEALER)
        , requestResponseSocket(context, ZMQ_SUB)
        , subscriptionInfo(std::move(_subscriptionInfo)) {}

    Connection(const Connection &other) = delete;
    Connection &operator=(const Connection &) = delete;
    Connection &operator=(Connection &&) = delete;

    // Here be dragons! This is not to be used after
    // the connection was involved in any threading code
    Connection unsafeMove() && {
        return std::move(*this);
    }

    const auto &referenceCount() const { return _refCount; }

    // Functions to block connection deletion
    void increaseReferenceCount() { ++_refCount; }
    void decreaseReferenceCount() { --_refCount; }

    struct KeepAlive {
        Connection *_connection;
        KeepAlive(Connection *connection)
            : _connection(connection) {
            _connection->increaseReferenceCount();
        }

        ~KeepAlive() {
            _connection->decreaseReferenceCount();
        }
    };

    auto writeLock() {
        return WriteLock(_cachedRepliesMutex);
    }

    auto readLock() const {
        return ReadLock(_cachedRepliesMutex);
    }

    void waitForUpdate() {
        auto temporaryLock = writeLock();
        _pollingIndexCV.wait(temporaryLock);
    }

    std::size_t cachedRepliesSize(ReadLock & /*lock*/) const {
        return _cachedReplies.size();
    }

    std::string cachedReply(ReadLock & /*lock*/, PollingIndex index) const {
        const auto firstCachedIndex = _nextPollingIndex - _cachedReplies.size();
        return (index >= firstCachedIndex && index < _nextPollingIndex) ? _cachedReplies[index - firstCachedIndex] : std::string{};
    }

    PollingIndex nextPollingIndex(ReadLock & /*lock*/) const {
        return _nextPollingIndex;
    }

    void addCachedReply(std::unique_lock<std::shared_mutex> & /*lock*/, std::string reply) {
        _cachedReplies.push_back(std::move(reply));
        if (_cachedReplies.size() > MAX_CACHED_REPLIES) {
            _cachedReplies.erase(_cachedReplies.begin(), _cachedReplies.begin() + long(_cachedReplies.size() - MAX_CACHED_REPLIES));
        }

        _nextPollingIndex++;
        _pollingIndexCV.notify_all();
    }
};

} // namespace detail

template<typename Mode, typename VirtualFS, role... Roles>
class RestBackend : public Mode {
protected:
    Broker<Roles...> &_broker;
    const VirtualFS  &_vfs;
    URI<>             _restAddress;

private:
    std::jthread                                                            _connectionUpdaterThread;
    std::shared_mutex                                                       _connectionsMutex;

    std::map<detail::SubscriptionInfo, std::unique_ptr<detail::Connection>> _connectionForService;

public:
    using BrokerType = Broker<Roles...>;
    // returns a connection with refcount 1. Make sure you lower it to
    // zero at some point
    detail::Connection *notificationSubscriptionConnectionFor(const detail::SubscriptionInfo &subscriptionInfo) {
        detail::WriteLock lock(_connectionsMutex);
        // TODO: No need to find + emplace as separate steps
        if (auto it = _connectionForService.find(subscriptionInfo); it != _connectionForService.end()) {
            auto *connection = it->second.get();
            connection->increaseReferenceCount();
            return connection;
        }

        auto [it, inserted] = _connectionForService.emplace(std::piecewise_construct,
                std::forward_as_tuple(subscriptionInfo),
                std::forward_as_tuple(std::make_unique<detail::Connection>(_broker.context, subscriptionInfo)));

        if (!inserted) {
            assert(inserted);
            std::terminate();
        }

        auto *connection = it->second.get();

        zmq::invoke(zmq_connect, connection->notificationSubscriptionSocket, INTERNAL_ADDRESS_BROKER.str()).template onFailure<opencmw::startup_error>("Can not connect REST worker to Majordomo broker");

        auto subscribeMessage = MdpMessage::createClientMessage(Command::Subscribe);
        subscribeMessage.setServiceName(subscriptionInfo.serviceName, MessageFrame::dynamic_bytes_tag{});
        subscribeMessage.setTopic(subscriptionInfo.topicName, MessageFrame::dynamic_bytes_tag{});

        if (!subscribeMessage.send(connection->notificationSubscriptionSocket)) {
            std::terminate();
            return nullptr;
        }

        return connection;
    }

    // Starts the thread to keep the unused subscriptions alive
    void startUpdaterThread() {
        _connectionUpdaterThread = std::jthread([this](std::stop_token stop_token) {
            thread::setThreadName("RestBackend updater thread");
            while (!stop_token.stop_requested()) {
                std::this_thread::sleep_for(100ms);
                {
                    // This is a long lock, alternatively, message reading
                    // could have separate locks per connection
                    detail::WriteLock lock(_connectionsMutex);

                    // Expired subscriptions cleanup
                    std::vector<const detail::SubscriptionInfo *> expiredSubscriptions;
                    for (auto &[info, connection] : _connectionForService) {
                        // fmt::print("Reference count is {}\n", connection->referenceCount());
                        if (connection->referenceCount() == 0) {
                            auto connectionLock = connection->writeLock();
                            if (connection->referenceCount() != 0) {
                                continue;
                            }
                            if (std::chrono::system_clock::now() - connection->lastUsed > UNUSED_SUBSCRIPTION_EXPIRATION_TIME) {
                                expiredSubscriptions.push_back(&info);
                            }
                        }
                    }
                    for (auto *subscriptionInfo : expiredSubscriptions) {
                        _connectionForService.erase(*subscriptionInfo);
                    }

                    // Reading the missed messages
                    const auto connectionCount = _connectionForService.size();

                    if (connectionCount != 0) {
                        std::vector<detail::Connection *> connections;
                        std::vector<zmq_pollitem_t>       pollItems;
                        connections.resize(connectionCount);
                        pollItems.resize(connectionCount);

                        std::list<detail::Connection::KeepAlive> keep;

                        for (std::size_t i = 0; auto &kvp : _connectionForService) {
                            auto &[key, connection] = kvp;

                            connections[i]          = connection.get();
                            keep.emplace_back(connection.get());

                            pollItems[i].events = ZMQ_POLLIN;
                            pollItems[i].socket = connection->notificationSubscriptionSocket.zmq_ptr;
                            ++i;
                        }

                        auto pollCount = zmq::invoke(zmq_poll, pollItems.data(), static_cast<int>(pollItems.size()),
                                std::chrono::duration_cast<std::chrono::milliseconds>(UPDATER_POLLING_TIME).count());
                        if (!pollCount) {
                            std::terminate();
                        }
                        if (pollCount.value() == 0) {
                            continue;
                        }

                        for (std::size_t i = 0; i < connectionCount; ++i) {
                            const auto events = pollItems[i].revents;
                            if (events & ZMQ_POLLIN) {
                                auto *currentConnection = connections[i];
                                auto  connectionLock    = currentConnection->writeLock();
                                if (auto responseMessage = MdpMessage::receive(currentConnection->notificationSubscriptionSocket)) {
                                    currentConnection->addCachedReply(connectionLock, std::string(responseMessage->body()));
                                }
                            }
                        }
                    }
                }
            }
        });
    }

    struct RestWorker;

    RestWorker &workerForCurrentThread() {
        thread_local static RestWorker worker(*this);
        return worker;
    }

    using Mode::_svr;
    using Mode::DEFAULT_REST_SCHEME;

public:
    explicit RestBackend(Broker<Roles...> &broker, const VirtualFS &vfs, URI<> restAddress = URI<>::factory().scheme(DEFAULT_REST_SCHEME).hostName("0.0.0.0").port(DEFAULT_REST_PORT).build())
        : _broker(broker), _vfs(vfs), _restAddress(restAddress) {
        _broker.registerDnsAddress(restAddress);
    }

    virtual ~RestBackend() {
        _svr.stop();
    }

    auto handleServiceRequest(const httplib::Request &request, httplib::Response &response, const httplib::ContentReader *content_reader_ = nullptr) {
        using detail::RestMethod;
        auto service = [&] {
            if (!request.path.empty() && request.path[0] == '/') {
                return std::string_view(request.path.begin() + 1, request.path.end());
            } else {
                return std::string_view(request.path);
            }
        }();

        if (service.empty()) {
            return detail::respondWithError(response, "Error: Service not specified\n");
        }

        auto restMethod = [&] {
            // clang-format off
            auto methodString =
                request.has_header("X-OPENCMW-METHOD") ?
                    request.get_header_value("X-OPENCMW-METHOD") :
                    request.method;

            return methodString == "SUB"  ? RestMethod::Subscribe :
                   methodString == "POLL" ? RestMethod::LongPoll :
                   methodString == "PUT"  ? RestMethod::Post :
                   methodString == "POST" ? RestMethod::Post :
                   methodString == "GET"  ? RestMethod::Get :
                                            RestMethod::Invalid;
            // clang-format on
        }();

        std::string subscriptionContext;

        for (const auto &[key, value] : request.params) {
            if (key == "LongPollingIdx") {
                // This parameter is not passed on, it just means we
                // want to use long polling
                restMethod = value == "Subscription" ? RestMethod::Subscribe : RestMethod::LongPoll;

            } else if (key == "SubscriptionContext") {
                subscriptionContext = value;
            }
        }

        if (restMethod == RestMethod::Invalid) {
            return detail::respondWithError(response, "Error: Requested method is not supported\n");
        }

        auto &worker = workerForCurrentThread();

        switch (restMethod) {
        case RestMethod::Get:
        case RestMethod::Post:
            return worker.respondWithPubSub(request, response, service, restMethod, content_reader_);

        case RestMethod::LongPoll:
            return worker.respondWithLongPoll(request, response, service, subscriptionContext);

        case RestMethod::Subscribe:
            return worker.respondWithSubscription(response, service, subscriptionContext);

        default:
            // std::unreachable() is C++23
            assert(!"We have already checked that restMethod is not Invalid");
            return false;
        }
    }

    virtual void registerHandlers() {
        _svr.Get("/", [this](const httplib::Request &request, httplib::Response &response) {
            return detail::respondWithServicesList(_broker, request, response);
        });

        _svr.Options(".*",
                [](const httplib::Request & /*req*/, httplib::Response &res) {
                    res.set_header("Allow", "GET, POST, PUT, OPTIONS");
                    res.set_header("Access-Control-Allow-Origin", "*");
                    res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, OPTIONS");
                    res.set_header("Access-Control-Allow-Headers", "X-OPENCMW-METHOD,Content-Type");
                    res.set_header("Access-Control-Max-Age", "86400");
                });

        static const char *nonEmptyPath = "..*";
        _svr.Get(nonEmptyPath, [this](const httplib::Request &request, httplib::Response &response) {
            return handleServiceRequest(request, response, nullptr);
        });
        _svr.Post(nonEmptyPath, [this](const httplib::Request &request, httplib::Response &response, const httplib::ContentReader &content_reader) {
            return handleServiceRequest(request, response, &content_reader);
        });
        _svr.Put(nonEmptyPath, [this](const httplib::Request &request, httplib::Response &response, const httplib::ContentReader &content_reader) {
            return handleServiceRequest(request, response, &content_reader);
        });
    }

    void run() {
        thread::setThreadName("RestBackend thread");

        startUpdaterThread();

        registerHandlers();

        if (!_restAddress.hostName() || !_restAddress.port()) {
            throw opencmw::startup_error(fmt::format("REST server URI is not valid {}", _restAddress.str()));
        }

        bool listening = _svr.listen(_restAddress.hostName().value().data(), _restAddress.port().value());
        if (!listening) {
            throw opencmw::startup_error(fmt::format("Can not start REST server on {}:{}", _restAddress.hostName().value().data(), _restAddress.port().value()));
        }
    }

    void requestStop() {
        _svr.stop();
    }

    void shutdown() {
        requestStop();
    }
};

template<typename Mode, typename VirtualFS, role... Roles>
struct RestBackend<Mode, VirtualFS, Roles...>::RestWorker {
    RestBackend   &restBackend;

    zmq_pollitem_t pollItem{};

    RestWorker(RestBackend &rest)
        : restBackend(rest) {
    }

    RestWorker(RestWorker &&other) = default;

    detail::Connection connect() {
        detail::Connection connection(restBackend._broker.context, {});
        pollItem.events = ZMQ_POLLIN;

        zmq::invoke(zmq_connect, connection.notificationSubscriptionSocket, INTERNAL_ADDRESS_BROKER.str()).template onFailure<opencmw::startup_error>("Can not connect REST worker to Majordomo broker");
        zmq::invoke(zmq_connect, connection.requestResponseSocket, INTERNAL_ADDRESS_PUBLISHER.str()).template onFailure<opencmw::startup_error>("Can not connect REST worker to Majordomo broker");

        return std::move(connection).unsafeMove();
    }

    bool respondWithPubSub(const httplib::Request &request, httplib::Response &response, const std::string_view &service, detail::RestMethod restMethod, const httplib::ContentReader *content_reader_ = nullptr) {
        // clang-format off
        const Command mdpMessageCommand =
                 restMethod == detail::RestMethod::Post      ? Command::Set :
                                                 /* default */ Command::Get;
        // clang-format on

        auto        uri = URI<>::factory();
        std::string bodyOverride;
        for (const auto &[key, value] : request.params) {
            if (key == "_bodyOverride") {
                bodyOverride = value;

            } else if (key == "LongPollingIdx") {
                // This parameter is not passed on, it just means we
                // want to use long polling -- already handled

            } else {
                uri = std::move(uri).addQueryParameter(key, value);
            }
        }

        auto message = MdpMessage::createClientMessage(mdpMessageCommand);
        message.setServiceName(service, MessageFrame::dynamic_bytes_tag{});

        const auto acceptedFormat = detail::acceptedMimeForRequest(request);
        uri                       = std::move(uri).addQueryParameter("contentType", std::string(acceptedFormat));

        auto topic                = std::string(service) + uri.toString();

        message.setTopic(topic, MessageFrame::dynamic_bytes_tag{});

        if (request.is_multipart_form_data()) {
            if (content_reader_ != nullptr) {
                const auto &content_reader = *content_reader_;

                FormData    formData;
                auto        it = formData.fields.begin();

                content_reader(
                        [&](const httplib::MultipartFormData &file) {
                            it = formData.fields.emplace(file.name, "").first;
                            return true;
                        },
                        [&](const char *data, std::size_t data_length) {
                            // TODO: This should append to content
                            it->second.append(std::string_view(data, data_length));

                            return true;
                        });

                opencmw::IoBuffer buffer;
                // opencmw::serialise<opencmw::Json>(buffer, formData);
                IoSerialiser<opencmw::Json, decltype(formData.fields)>::serialise(buffer, FieldDescriptionShort{}, formData.fields);

                std::string requestData(buffer.asString());
                // Json serialiser (rightfully) does not like bool values in string
                static auto replacerRegex = std::regex(R"regex("opencmw_unquoted_value[(](.*)[)]")regex");
                requestData               = std::regex_replace(requestData, replacerRegex, "$1");
                auto requestDataBegin     = std::find(requestData.cbegin(), requestData.cend(), '{');

                message.setBody(std::string_view(requestDataBegin, requestData.cend()), MessageFrame::dynamic_bytes_tag{});
            }
        } else if (!bodyOverride.empty()) {
            message.setBody(bodyOverride, MessageFrame::dynamic_bytes_tag{});

        } else if (!request.body.empty()) {
            message.setBody(request.body, MessageFrame::dynamic_bytes_tag{});
        }

        auto connection = connect();

        if (!message.send(connection.notificationSubscriptionSocket)) {
            return detail::respondWithError(response, "Error: Failed to send a message to the broker\n");
        }

        pollItem.socket = connection.notificationSubscriptionSocket.zmq_ptr;
        auto pollResult = zmq::invoke(zmq_poll, &pollItem, 1, std::chrono::duration_cast<std::chrono::milliseconds>(REST_POLLING_TIME).count());
        if (!pollResult || pollResult.value() == 0) {
            detail::respondWithError(response, "Error: No response from broker\n");
        } else if (auto responseMessage = MdpMessage::receive(connection.notificationSubscriptionSocket); !responseMessage) {
            detail::respondWithError(response, "Error: Empty response from broker\n");
        } else if (!responseMessage->error().empty()) {
            detail::respondWithError(response, responseMessage->error());
        } else {
            response.status = HTTP_OK;

            response.set_header("X-OPENCMW-TOPIC", responseMessage->topic().data());
            response.set_header("X-OPENCMW-SERVICE-NAME", responseMessage->serviceName().data());
            response.set_header("Access-Control-Allow-Origin", "*");

            if (request.method != "GET") {
                response.set_content(responseMessage->body().data(), MIME::TEXT.typeName().data());
            } else {
                response.set_content(responseMessage->body().data(), acceptedFormat.data());
            }
        }
        return true;
    }

    bool respondWithSubscription(httplib::Response &response, const std::string_view &service, const std::string_view &topic) {
        detail::SubscriptionInfo subscriptionInfo{ std::string(service), std::string(topic) };

        auto                    *connection = restBackend.notificationSubscriptionConnectionFor(subscriptionInfo);
        assert(connection);

        response.set_header("Access-Control-Allow-Origin", "*");
        response.set_chunked_content_provider(
                "application/json",
                [connection](std::size_t /*offset*/, httplib::DataSink &sink) mutable {
                    std::cerr << "Chunked reply...\n";

                    connection->waitForUpdate();

                    auto        connectionCacheLock = connection->readLock();
                    auto        lastIndex           = connection->nextPollingIndex(connectionCacheLock) - 1;
                    const auto &lastReply           = connection->cachedReply(connectionCacheLock, lastIndex);
                    std::cerr << "Chunk: " << lastIndex << "'" << lastReply << "'\n";

                    sink.os << lastReply << "\n\n";

                    return true;
                },
                [connection](bool) {
                    connection->decreaseReferenceCount();
                });

        return true;
    }

    bool respondWithLongPollRedirect(const httplib::Request &request, httplib::Response &response, const std::string_view &subscriptionContext, detail::PollingIndex redirectLongPollingIdx) {
        auto uri = URI<>::factory()
                           .path(request.path)
                           .addQueryParameter("LongPollingIdx", std::to_string(redirectLongPollingIdx))
                           .addQueryParameter("SubscriptionContext", std::string(subscriptionContext));

        // copy over the original query parameters
        addParameters(request, uri);

        const auto redirect = uri.toString();
        response.set_redirect(redirect);
        return true;
    }

    bool respondWithLongPoll(const httplib::Request &request, httplib::Response &response, const std::string_view &service, const std::string_view &topic) {
        // TODO: After the URIs are formalized, rethink service and topic
        auto uri = URI<>::factory();
        addParameters(request, uri);

        detail::SubscriptionInfo subscriptionInfo{ std::string(service), std::string(topic) };

        const auto               longPollingIdxIt = request.params.find("LongPollingIdx");
        if (longPollingIdxIt == request.params.end()) {
            return detail::respondWithError(response, "Error: LongPollingIdx parameter not specified");
        }

        const auto &longPollingIdxParam = longPollingIdxIt->second;

        struct CacheInfo {
            detail::PollingIndex firstCachedIndex = 0;
            detail::PollingIndex nextPollingIndex = 0;
            detail::Connection  *connection       = nullptr;
        };
        auto fetchCache = [this, &subscriptionInfo] {
            std::shared_lock lock(restBackend._connectionsMutex);
            auto            &recycledConnectionForService = restBackend._connectionForService;
            if (auto it = recycledConnectionForService.find(subscriptionInfo); it != recycledConnectionForService.cend()) {
                auto                         *connectionCache = it->second.get();
                detail::Connection::KeepAlive keep(connectionCache);
                auto                          connectionCacheLock = connectionCache->readLock();
                return CacheInfo{
                    .firstCachedIndex = connectionCache->nextPollingIndex(connectionCacheLock) - connectionCache->cachedRepliesSize(connectionCacheLock),
                    .nextPollingIndex = connectionCache->nextPollingIndex(connectionCacheLock),
                    .connection       = connectionCache
                };
            } else {
                // We didn't have this before, means 0 is the next index
                return CacheInfo{
                    .firstCachedIndex = 0,
                    .nextPollingIndex = 0,
                    .connection       = nullptr
                };
            }
        };

        detail::PollingIndex requestedLongPollingIdx = 0;

        // Hoping we already have the requested value in the cache
        {
            const auto cache = fetchCache();
            response.set_header("Access-Control-Allow-Origin", "*");

            if (longPollingIdxParam == "Next") {
                return respondWithLongPollRedirect(request, response, topic, cache.nextPollingIndex);
            }

            if (longPollingIdxParam == "Last") {
                if (cache.connection != nullptr) {
                    return respondWithLongPollRedirect(request, response, topic, cache.nextPollingIndex - 1);
                } else {
                    return respondWithLongPollRedirect(request, response, topic, cache.nextPollingIndex);
                }
            }

            if (longPollingIdxParam == "FirstAvailable") {
                return respondWithLongPollRedirect(request, response, topic, cache.firstCachedIndex);
            }

            if (std::from_chars(longPollingIdxParam.data(), longPollingIdxParam.data() + longPollingIdxParam.size(), requestedLongPollingIdx).ec != std::errc{}) {
                return detail::respondWithError(response, "Error: Invalid LongPollingIdx value");
            }

            if (requestedLongPollingIdx > cache.nextPollingIndex) {
                return detail::respondWithError(response, "Error: LongPollingIdx tries to read the future");
            }

            if (requestedLongPollingIdx < cache.firstCachedIndex) {
                return detail::respondWithError(response, "Error: Requested LongPollingIdx is no longer cached");
            }

            if (cache.connection && requestedLongPollingIdx < cache.nextPollingIndex) {
                auto connectionCacheLock = cache.connection->readLock();
                // The result is already ready
                response.set_content(cache.connection->cachedReply(connectionCacheLock, requestedLongPollingIdx), MIME::JSON.typeName().data());
                return true;
            }
        }

        // Fallback to creating a connection and waiting
        auto *connection = restBackend.notificationSubscriptionConnectionFor(subscriptionInfo);
        assert(connection);
        detail::Connection::KeepAlive keep(connection);

        // Since we use KeepAlive object, the inital refCount can go away
        connection->decreaseReferenceCount();

        connection->waitForUpdate();

        const auto newCache = fetchCache();

        // This time it needs to exist
        assert(newCache.connection != nullptr);

        if (requestedLongPollingIdx >= newCache.firstCachedIndex && requestedLongPollingIdx < newCache.nextPollingIndex) {
            auto connectionCacheLock = newCache.connection->readLock();
            response.set_content(newCache.connection->cachedReply(connectionCacheLock, requestedLongPollingIdx), MIME::JSON.typeName().data());
            return true;

        } else {
            return detail::respondWithError(response, "Error: We waited for the new value, but it was not found");
        }
    }

private:
    void addParameters(const httplib::Request &request, URI<>::UriFactory &uri) {
        for (const auto &[key, value] : request.params) {
            if (key == "LongPollingIdx") {
                // This parameter is not passed on, it just means we
                // want to use long polling -- already handled
            } else {
                uri = std::move(uri).addQueryParameter(key, value);
            }
        }
    }
};

} // namespace opencmw::majordomo

#endif // include guard
