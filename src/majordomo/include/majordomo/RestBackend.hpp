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
#include <MdpMessage.hpp>
#include <MIME.hpp>
#include <URI.hpp>

#include <IoSerialiserJson.hpp>
#include <MustacheSerialiser.hpp>
#include <ThreadAffinity.hpp>
#include <Topic.hpp>

// Majordomo
#include <majordomo/Broker.hpp>
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
constexpr auto        HTTP_GATEWAY_TIMEOUT                = 504;
constexpr auto        DEFAULT_REST_PORT                   = 8080;
constexpr auto        UPDATER_POLLING_TIME                = 1s;
constexpr auto        LONG_POLL_SERVER_TIMEOUT            = 30s;
constexpr auto        UNUSED_SUBSCRIPTION_EXPIRATION_TIME = 30s;
constexpr std::size_t MAX_CACHED_REPLIES                  = 32;

namespace detail {
// Provides a safe alternative to getenv
inline const char *getEnvFilenameOr(const char *field, const char *defaultValue) {
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

inline std::string_view acceptedMimeForRequest(const auto &request) {
    static constexpr std::array<std::string_view, 3> acceptableMimeTypes = {
        MIME::JSON.typeName(), MIME::HTML.typeName(), MIME::BINARY.typeName()
    };
    auto accepted = [](auto format) {
        const auto it = std::find(acceptableMimeTypes.cbegin(), acceptableMimeTypes.cend(), format);
        return std::make_pair(
                it != acceptableMimeTypes.cend(),
                it);
    };

    if (request.has_header("Content-Type")) {
        std::string format = request.get_header_value("Content-Type");
        if (const auto [found, where] = accepted(format); found) {
            return *where;
        }
    }
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

bool respondWithError(auto &response, std::string_view message, int status = HTTP_ERROR) {
    response.status = status;
    response.set_content(message.data(), MIME::TEXT.typeName().data());
    return true;
};

inline bool respondWithServicesList(auto &broker, const httplib::Request &request, httplib::Response &response) {
    // Mmi is not a MajordomoWorker, so it doesn't know JSON (TODO)
    const auto acceptedFormat = acceptedMimeForRequest(request);

    if (acceptedFormat == MIME::JSON.typeName()) {
        std::vector<std::string> serviceNames;
        // TODO: Should this be synchronized?
        broker.forEachService([&](std::string_view name, std::string_view) {
            serviceNames.emplace_back(name);
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
                        const auto lhsIsMmi = lhs.name.starts_with("/mmi.");
                        const auto rhsIsMmi = rhs.name.starts_with("/mmi.");
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

struct Connection {
    zmq::Socket notificationSubscriptionSocket;
    zmq::Socket requestResponseSocket;
    std::string subscriptionKey;

    using Timestamp                 = std::chrono::time_point<std::chrono::system_clock>;
    std::atomic<Timestamp> lastUsed = std::chrono::system_clock::now();

private:
    mutable std::shared_mutex   _cachedRepliesMutex;
    std::deque<std::string>     _cachedReplies; // Ring buffer?
    PollingIndex                _nextPollingIndex = 0;
    std::condition_variable_any _pollingIndexCV;

    std::atomic_int             _refCount = 1;

    // Here be dragons! This is not to be used after the connection was involved in any threading code
    Connection(Connection &&other) noexcept
        : notificationSubscriptionSocket(std::move(other.notificationSubscriptionSocket))
        , requestResponseSocket(std::move(other.requestResponseSocket))
        , subscriptionKey(std::move(other.subscriptionKey))
        , lastUsed(other.lastUsed.load())
        , _cachedReplies(std::move(other._cachedReplies))
        , _nextPollingIndex(other._nextPollingIndex) {
    }

public:
    Connection(const zmq::Context &context, std::string _subscriptionKey)
        : notificationSubscriptionSocket(context, ZMQ_SUB)
        , requestResponseSocket(context, ZMQ_DEALER)
        , subscriptionKey(std::move(_subscriptionKey)) {}

    Connection(const Connection &other)       = delete;
    Connection &operator=(const Connection &) = delete;
    Connection &operator=(Connection &&)      = delete;

    // Here be dragons! This is not to be used after the connection was involved in any threading code
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

    bool waitForUpdate(std::chrono::milliseconds timeout) {
        // This could also periodically check for the client connection being dropped (e.g. due to client-side timeout) if cpp-httplib had API for that.
        auto       temporaryLock = writeLock();
        const auto next          = _nextPollingIndex;
        while (_nextPollingIndex == next) {
            if (_pollingIndexCV.wait_for(temporaryLock, timeout) == std::cv_status::timeout) {
                return false;
            }
        }

        return true;
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
    Broker<Roles...>                      &_broker;
    const VirtualFS                       &_vfs;
    URI<>                                  _restAddress;
    std::atomic<std::chrono::milliseconds> _majordomoTimeout = 30000ms;

private:
    std::jthread                                               _mdpConnectionUpdaterThread;
    std::shared_mutex                                          _mdpConnectionsMutex;
    std::map<std::string, std::unique_ptr<detail::Connection>> _mdpConnectionForService;

public:
    /**
     * Timeout used for interaction with majordomo workers, i.e. the time to wait
     * for notifications on subscriptions (long-polling) and for responses to Get/Set
     * requests.
     */
    void setMajordomoTimeout(std::chrono::milliseconds timeout) {
        _majordomoTimeout = timeout;
    }

    std::chrono::milliseconds majordomoTimeout() const {
        return _majordomoTimeout;
    }

    using BrokerType = Broker<Roles...>;
    // returns a connection with refcount 1. Make sure you lower it to zero at some point
    detail::Connection *notificationSubscriptionConnectionFor(const std::string &zmqTopic) {
        detail::WriteLock lock(_mdpConnectionsMutex);
        // TODO: No need to find + emplace as separate steps
        if (auto it = _mdpConnectionForService.find(zmqTopic); it != _mdpConnectionForService.end()) {
            auto *connection = it->second.get();
            connection->increaseReferenceCount();
            return connection;
        }

        auto [it, inserted] = _mdpConnectionForService.emplace(std::piecewise_construct,
                std::forward_as_tuple(zmqTopic),
                std::forward_as_tuple(std::make_unique<detail::Connection>(_broker.context, zmqTopic)));

        if (!inserted) {
            assert(inserted);
            std::terminate();
        }

        auto *connection = it->second.get();

        zmq::invoke(zmq_connect, connection->notificationSubscriptionSocket, INTERNAL_ADDRESS_PUBLISHER.str()).template onFailure<opencmw::startup_error>("Can not connect REST worker to Majordomo broker");
        zmq::invoke(zmq_setsockopt, connection->notificationSubscriptionSocket, ZMQ_SUBSCRIBE, zmqTopic.data(), zmqTopic.size()).assertSuccess();

        return connection;
    }

    // Starts the thread to keep the unused subscriptions alive
    void startUpdaterThread() {
        _mdpConnectionUpdaterThread = std::jthread([this](const std::stop_token &stopToken) {
            thread::setThreadName("RestBackend updater thread");

            std::vector<detail::Connection *> connections;
            std::vector<zmq_pollitem_t>       pollItems;
            while (!stopToken.stop_requested()) {
                std::list<detail::Connection::KeepAlive> keep;
                {
                    // This is a long lock, alternatively, message reading could have separate locks per connection
                    detail::WriteLock lock(_mdpConnectionsMutex);

                    // Expired subscriptions cleanup
                    std::vector<std::string> expiredSubscriptions;
                    for (auto &[subscriptionKey, connection] : _mdpConnectionForService) {
                        // fmt::print("Reference count is {}\n", connection->referenceCount());
                        if (connection->referenceCount() == 0) {
                            auto connectionLock = connection->writeLock();
                            if (connection->referenceCount() != 0) {
                                continue;
                            }
                            if (std::chrono::system_clock::now() - connection->lastUsed.load() > UNUSED_SUBSCRIPTION_EXPIRATION_TIME) {
                                expiredSubscriptions.push_back(subscriptionKey);
                            }
                        }
                    }
                    for (const auto &subscriptionKey : expiredSubscriptions) {
                        _mdpConnectionForService.erase(subscriptionKey);
                    }

                    // setup poller and socket data structures for all connections
                    const std::size_t connectionCount = _mdpConnectionForService.size();
                    connections.resize(connectionCount);
                    pollItems.resize(connectionCount);
                    for (std::size_t i = 0UZ; auto &[key, connection] : _mdpConnectionForService) {
                        connections[i] = connection.get();
                        keep.emplace_back(connection.get());
                        pollItems[i].events = ZMQ_POLLIN;
                        pollItems[i].socket = connection->notificationSubscriptionSocket.zmq_ptr;
                        ++i;
                    }
                } // finished copying local state, keep ensures that connections are kept alive, end of lock on _mdpConnectionsForService

                if (pollItems.empty()) {
                    std::this_thread::sleep_for(100ms); // prevent spinning on connection cleanup if there are no connections to poll on
                    continue;
                }

                auto pollCount = zmq::invoke(zmq_poll, pollItems.data(), static_cast<int>(pollItems.size()), std::chrono::duration_cast<std::chrono::milliseconds>(UPDATER_POLLING_TIME).count());
                if (!pollCount) {
                    fmt::print("Error while polling for updates from the broker\n");
                    std::terminate();
                }
                if (pollCount.value() == 0) {
                    continue;
                }

                // Reading messages
                for (std::size_t i = 0; i < connections.size(); ++i) {
                    if (pollItems[i].revents & ZMQ_POLLIN) {
                        detail::Connection                 *currentConnection = connections[i];
                        std::unique_lock<std::shared_mutex> connectionLock    = currentConnection->writeLock();
                        while (auto responseMessage = zmq::receive<mdp::MessageFormat::WithSourceId>(currentConnection->notificationSubscriptionSocket)) {
                            currentConnection->addCachedReply(connectionLock, std::string(responseMessage->data.asString()));
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
        // shutdown thread before _connectionForService is destroyed
        _mdpConnectionUpdaterThread.request_stop();
        _mdpConnectionUpdaterThread.join();
    }

    auto handleServiceRequest(const httplib::Request &request, httplib::Response &response, const httplib::ContentReader *content_reader_ = nullptr) {
        using detail::RestMethod;

        auto convertParams = [](const httplib::Params &params) {
            mdp::Topic::Params r;
            for (const auto &[key, value] : params) {
                if (key == "LongPollingIdx" || key == "SubscriptionContext") {
                    continue;
                }
                if (value.empty()) {
                    r[key] = std::nullopt;
                } else {
                    r[key] = value;
                }
            }
            return r;
        };

        std::optional<mdp::Topic> maybeTopic;

        try {
            maybeTopic = mdp::Topic::fromString(request.path, convertParams(request.params));
        } catch (const std::exception &e) {
            return detail::respondWithError(response, fmt::format("Error: {}\n", e.what()));
        }
        auto topic      = std::move(*maybeTopic);

        auto restMethod = [&] {
            auto methodString = request.has_header("X-OPENCMW-METHOD") ? request.get_header_value("X-OPENCMW-METHOD") : request.method;
            // clang-format off
            return methodString == "SUB"  ? RestMethod::Subscribe :
                   methodString == "POLL" ? RestMethod::LongPoll :
                   methodString == "PUT"  ? RestMethod::Post :
                   methodString == "POST" ? RestMethod::Post :
                   methodString == "GET"  ? RestMethod::Get :
                                            RestMethod::Invalid;
            // clang-format on
        }();

        for (const auto &[key, value] : request.params) {
            if (key == "LongPollingIdx") {
                // This parameter is not passed on, it just means we want to use long polling
                restMethod = value == "Subscription" ? RestMethod::Subscribe : RestMethod::LongPoll;
            } else if (key == "SubscriptionContext") {
                topic = mdp::Topic::fromString(value, {}); // params are parsed from value
            }
        }

        if (restMethod == RestMethod::Invalid) {
            return detail::respondWithError(response, "Error: Requested method is not supported\n");
        }

        auto &worker = workerForCurrentThread();

        switch (restMethod) {
        case RestMethod::Get:
        case RestMethod::Post:
            return worker.respondWithGetSet(request, response, topic, restMethod, content_reader_);
        case RestMethod::LongPoll:
            return worker.respondWithLongPoll(request, response, topic);
        case RestMethod::Subscribe:
            return worker.respondWithSubscription(response, topic);
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

        _svr.set_tcp_nodelay(true);
        _svr.set_keep_alive_max_count(1000);
        _svr.set_read_timeout(1, 0);
        _svr.set_write_timeout(1, 0);
        _svr.new_task_queue = []() { return new httplib::ThreadPool(/*num_threads=*/32, /*max_queued_requests=*/20); };

        bool listening      = _svr.listen(_restAddress.hostName().value().data(), _restAddress.port().value());
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

    explicit RestWorker(RestBackend &rest)
        : restBackend(rest) {
    }

    RestWorker(RestWorker &&other) noexcept = default;

    detail::Connection connect() {
        detail::Connection connection(restBackend._broker.context, {});
        pollItem.events = ZMQ_POLLIN;

        zmq::invoke(zmq_connect, connection.notificationSubscriptionSocket, INTERNAL_ADDRESS_PUBLISHER.str()).template onFailure<opencmw::startup_error>("Can not connect REST worker to Majordomo broker");
        zmq::invoke(zmq_connect, connection.requestResponseSocket, INTERNAL_ADDRESS_BROKER.str()).template onFailure<opencmw::startup_error>("Can not connect REST worker to Majordomo broker");

        return std::move(connection).unsafeMove();
    }

    bool respondWithGetSet(const httplib::Request &request, httplib::Response &response, mdp::Topic topic, detail::RestMethod restMethod, const httplib::ContentReader *content_reader_ = nullptr) {
        const mdp::Command mdpMessageCommand = restMethod == detail::RestMethod::Post ? mdp::Command::Set : mdp::Command::Get;

        auto               uri               = URI<>::factory();
        std::string        bodyOverride;
        std::string        contentType;
        int                contentLength{ 0 };
        for (const auto &[key, value] : request.params) {
            if (key == "_bodyOverride") {
                bodyOverride = value;
            } else if (key == "LongPollingIdx") {
                // This parameter is not passed on, it just means we want to use long polling -- already handled
            } else {
                uri = std::move(uri).addQueryParameter(key, value);
            }
        }

        for (const auto &[key, value] : request.headers) {
            if (httplib::detail::case_ignore::equal(key, "Content-Length")) {
                contentLength = std::stoi(value);
            } else if (httplib::detail::case_ignore::equal(key, "Content-Type")) {
                contentType = value;
            }
        }

        mdp::Message message;
        message.protocolName      = mdp::clientProtocol;
        message.command           = mdpMessageCommand;

        const auto acceptedFormat = detail::acceptedMimeForRequest(request);
        topic.addParam("contentType", acceptedFormat);
        message.serviceName = std::string(topic.service());
        message.topic       = topic.toMdpTopic();

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
                static auto replacerRegex   = std::regex(R"regex("opencmw_unquoted_value[(](.*)[)]")regex");
                requestData                 = std::regex_replace(requestData, replacerRegex, "$1");
                auto       requestDataBegin = std::find(requestData.cbegin(), requestData.cend(), '{');

                const auto req              = std::string_view(requestDataBegin, requestData.cend());
                message.data                = IoBuffer(req.data(), req.size());
            }
        } else if (!bodyOverride.empty()) {
            message.data = IoBuffer(bodyOverride.data(), bodyOverride.size());
        } else if (!request.body.empty()) {
            message.data = IoBuffer(request.body.data(), request.body.size());
        } else if (contentType != "" && contentLength > 0 && content_reader_ != nullptr) {
            std::string body;
            (*content_reader_)([&body](const char *data, size_t datalength) { body = std::string{data, datalength}; return true; });
            message.data = IoBuffer(body.data(), body.size());
        }

        auto connection = connect();

        if (!zmq::send(std::move(message), connection.requestResponseSocket)) {
            return detail::respondWithError(response, "Error: Failed to send a message to the broker\n");
        }

        // blocks waiting for the response
        pollItem.socket = connection.requestResponseSocket.zmq_ptr;
        auto pollResult = zmq::invoke(zmq_poll, &pollItem, 1, std::chrono::duration_cast<std::chrono::milliseconds>(restBackend.majordomoTimeout()).count());
        if (!pollResult || pollResult.value() == 0) {
            detail::respondWithError(response, "Error: No response from broker\n", HTTP_GATEWAY_TIMEOUT);
        } else if (auto responseMessage = zmq::receive<mdp::MessageFormat::WithoutSourceId>(connection.requestResponseSocket); !responseMessage) {
            detail::respondWithError(response, "Error: Empty response from broker\n");
        } else if (!responseMessage->error.empty()) {
            detail::respondWithError(response, responseMessage->error);
        } else {
            response.status = HTTP_OK;

            response.set_header("X-OPENCMW-TOPIC", responseMessage->topic.str().data());
            response.set_header("X-OPENCMW-SERVICE-NAME", responseMessage->serviceName.data());
            response.set_header("Access-Control-Allow-Origin", "*");
            response.set_header("X-TIMESTAMP", fmt::format("{}", std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count()));
            const auto data = responseMessage->data.asString();

            if (request.method != "GET") {
                response.set_content(data.data(), data.size(), MIME::TEXT.typeName().data());
            } else {
                response.set_content(data.data(), data.size(), acceptedFormat.data());
            }
        }
        return true;
    }

    bool respondWithSubscription(httplib::Response &response, const mdp::Topic &subscription) {
        const auto subscriptionKey = subscription.toZmqTopic();
        auto      *connection      = restBackend.notificationSubscriptionConnectionFor(subscriptionKey);
        assert(connection);
        const auto majordomoTimeout = restBackend.majordomoTimeout();
        response.set_header("Access-Control-Allow-Origin", "*");
        response.set_header("X-TIMESTAMP", fmt::format("{}", std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count()));

        response.set_chunked_content_provider(
                "application/json",
                [connection, majordomoTimeout](std::size_t /*offset*/, httplib::DataSink &sink) mutable {
                    std::cerr << "Chunked reply...\n";

                    if (!connection->waitForUpdate(majordomoTimeout)) {
                        return false;
                    }

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

    bool respondWithLongPollRedirect(const httplib::Request &request, httplib::Response &response, const mdp::Topic &subscription, detail::PollingIndex redirectLongPollingIdx) {
        auto uri = URI<>::factory()
                           .path(request.path)
                           .addQueryParameter("LongPollingIdx", std::to_string(redirectLongPollingIdx));

        // copy over the original query parameters
        addParameters(request, uri);

        const auto redirect = uri.toString();
        response.set_redirect(redirect);
        return true;
    }

    bool respondWithLongPoll(const httplib::Request &request, httplib::Response &response, const mdp::Topic &subscription) {
        // TODO: After the URIs are formalized, rethink service and topic
        auto uri = URI<>::factory();
        addParameters(request, uri);

        const auto subscriptionKey  = subscription.toZmqTopic();

        const auto longPollingIdxIt = request.params.find("LongPollingIdx");
        if (longPollingIdxIt == request.params.end()) {
            return detail::respondWithError(response, "Error: LongPollingIdx parameter not specified");
        }

        const auto &longPollingIdxParam = longPollingIdxIt->second;

        struct CacheInfo {
            detail::PollingIndex firstCachedIndex = 0;
            detail::PollingIndex nextPollingIndex = 0;
            detail::Connection  *connection       = nullptr;
        };
        auto fetchCache = [this, &subscriptionKey] {
            std::shared_lock lock(restBackend._mdpConnectionsMutex);
            auto            &recycledConnectionForService = restBackend._mdpConnectionForService;
            if (auto it = recycledConnectionForService.find(subscriptionKey); it != recycledConnectionForService.cend()) {
                auto                         *connectionCache = it->second.get();
                detail::Connection::KeepAlive keep(connectionCache);
                connectionCache->lastUsed = std::chrono::system_clock::now();
                auto connectionCacheLock  = connectionCache->readLock();
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

        // Hoping we already have the requested value in the cache. Holding this caches blocks all cache entries, so no further updates can be received or other connections initiated.
        {
            const auto cache = fetchCache();
            response.set_header("Access-Control-Allow-Origin", "*");

            if (longPollingIdxParam == "Next") {
                return respondWithLongPollRedirect(request, response, subscription, cache.nextPollingIndex);
            }

            if (longPollingIdxParam == "Last") {
                if (cache.connection != nullptr) {
                    return respondWithLongPollRedirect(request, response, subscription, cache.nextPollingIndex - 1);
                } else {
                    return respondWithLongPollRedirect(request, response, subscription, cache.nextPollingIndex);
                }
            }

            if (longPollingIdxParam == "FirstAvailable") {
                return respondWithLongPollRedirect(request, response, subscription, cache.firstCachedIndex);
            }

            if (std::from_chars(longPollingIdxParam.data(), longPollingIdxParam.data() + longPollingIdxParam.size(), requestedLongPollingIdx).ec != std::errc{}) {
                return detail::respondWithError(response, "Error: Invalid LongPollingIdx value");
            }

            if (requestedLongPollingIdx > cache.nextPollingIndex) {
                return detail::respondWithError(response, "Error: LongPollingIdx tries to read the future");
            }

            if (requestedLongPollingIdx < cache.firstCachedIndex || requestedLongPollingIdx + 15 < cache.nextPollingIndex) {
                return respondWithLongPollRedirect(request, response, subscription, cache.nextPollingIndex);
            }

            if (cache.connection && requestedLongPollingIdx < cache.nextPollingIndex) {
                auto connectionCacheLock = cache.connection->readLock();
                // The result is already ready
                response.set_content(cache.connection->cachedReply(connectionCacheLock, requestedLongPollingIdx), MIME::JSON.typeName().data());
                return true;
            }
        }

        // Fallback to creating a connection and waiting
        auto *connection = restBackend.notificationSubscriptionConnectionFor(subscriptionKey);
        assert(connection);
        detail::Connection::KeepAlive keep(connection);

        // Since we use KeepAlive object, the initial refCount can go away
        connection->decreaseReferenceCount();

        if (!connection->waitForUpdate(restBackend.majordomoTimeout())) {
            return detail::respondWithError(response, "Timeout waiting for update", HTTP_GATEWAY_TIMEOUT);
        }

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
                // This parameter is not passed on, it just means we want to use long polling -- already handled
            } else {
                uri = std::move(uri).addQueryParameter(key, value);
            }
        }
    }
};

} // namespace opencmw::majordomo

#endif // include guard
