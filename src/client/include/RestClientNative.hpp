#ifndef OPENCMW_CLIENT_RESTCLIENTNATIVE_HPP
#define OPENCMW_CLIENT_RESTCLIENTNATIVE_HPP

#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <format>
#include <mutex>
#include <netinet/tcp.h>
#include <poll.h>
#include <stop_token>
#include <string>
#include <string_view>
#include <sys/poll.h>
#include <thread>
#include <unistd.h>

#include <nghttp2/nghttp2.h>

#include "ClientCommon.hpp"
#include "ClientContext.hpp"
#include "MdpMessage.hpp"
#include "MIME.hpp"
#include "rest/RestUtils.hpp"
#include "Topic.hpp"
#ifdef OPENCMW_PROFILE_HTTP
#include "LoadTest.hpp"
#endif
namespace opencmw::client {
enum class SubscriptionMode {
    Next,
    Last,
    None
};

namespace detail {

using namespace opencmw::rest::detail;

template<typename T>
struct SharedQueue {
    // TODO use a lock-free queue? This is only used client-side, so not that critical
    std::deque<T> deque;
    std::mutex    mutex;

    void          push(T v) {
        std::lock_guard lock(mutex);
        deque.push_back(std::move(v));
    }

    std::optional<T> try_get() {
        std::lock_guard lock(mutex);
        if (deque.empty()) {
            return {};
        }
        auto result = std::move(deque.front());
        deque.pop_front();
        return result;
    }
};

struct RequestResponse {
    // request data
    client::Command           request;
    std::unique_ptr<IoBuffer> body;
    std::string               normalizedTopic;

    // response data
    std::string                  responseStatus;
    std::string                  location; // for redirects
    std::optional<std::uint64_t> longPollingIdx;
    std::string                  payload;
    mdp::Message                 response;

    void                         fillResponse() {
        response.id           = 0;
        response.arrivalTime  = std::chrono::system_clock::now();
        response.protocolName = request.topic.scheme().value_or("");
        response.rbac.clear();

        switch (request.command) {
        case mdp::Command::Get:
        case mdp::Command::Set:
            response.command         = mdp::Command::Final;
            response.clientRequestID = request.clientRequestID;
            break;
        case mdp::Command::Subscribe:
            response.command = mdp::Command::Notify;
            break;
        default:
            break;
        }
    }

    void reportError(std::string error) {
        if (!request.callback) {
            HTTP_DBG("Client::reportError: {}", error);
            return;
        }
        fillResponse();
        response.topic = request.topic;
        response.error = std::move(error);
        response.data.clear();
        request.callback(std::move(response));
    }
};

constexpr std::size_t kParallelLongPollingRequests = 3;

struct Subscription {
    client::Command                                   request;
    SubscriptionMode                                  mode;
    std::optional<std::uint64_t>                      lastReceivedLongPollingIdx;
    std::vector<std::function<void(mdp::Message &&)>> callbacks;
};

struct Endpoint {
    std::string scheme;
    std::string host;
    uint16_t    port;

    auto        operator<=>(const Endpoint &) const = default;
};

template<typename Derived, typename TStreamId>
struct ClientSessionBase {
    struct PendingRequest {
        client::Command              command;
        SubscriptionMode             mode;
        std::string                  preferredMimeType;
        std::optional<std::uint64_t> longPollIdx;
    };
    std::map<std::string, Subscription>  _subscriptions;
    std::map<TStreamId, RequestResponse> _requestsByStreamId;

    [[nodiscard]] constexpr auto        &self() noexcept { return *static_cast<Derived *>(this); }
    [[nodiscard]] constexpr const auto  &self() const noexcept { return *static_cast<const Derived *>(this); }

    bool                                 addHeader(TStreamId streamId, std::string_view nameView, std::string_view valueView) {
        HTTP_DBG("Client::Header: id={} {} = {}", streamId, nameView, valueView);
        if (nameView == ":status") {
            _requestsByStreamId[streamId].responseStatus = std::string(valueView);
        } else if (nameView == "location") {
            _requestsByStreamId[streamId].location = std::string(valueView);
        } else if (nameView == "x-opencmw-topic") {
            try {
                _requestsByStreamId[streamId].response.topic = URI<>(std::string(valueView));
            } catch (const std::exception &e) {
                HTTP_DBG("Client::Header: Could not parse URI '{}': {}", valueView, e.what());
                return false;
            }
        } else if (nameView == "x-opencmw-service-name") {
            _requestsByStreamId[streamId].response.serviceName = std::string(valueView);
        } else if (nameView == "x-opencmw-long-polling-idx") {
            std::uint64_t longPollingIdx;
            if (auto ec = std::from_chars(valueView.data(), valueView.data() + valueView.size(), longPollingIdx); ec.ec != std::errc{}) {
                HTTP_DBG("Client::Header: Could not parse x-opencmw-long-polling-idx '{}'", valueView);
                return false;
            }
            _requestsByStreamId[streamId].longPollingIdx = longPollingIdx;
#ifdef OPENCMW_PROFILE_HTTP
        } else if (nameView == "x-timestamp") {
            std::println(std::cerr, "Client::Header: x-timestamp: {} (latency {} ns)", valueView, latency(valueView).count());
#endif
        }
        return true;
    }

    void submitRequest(client::Command &&cmd, SubscriptionMode mode, std::string preferredMimeType, std::optional<std::uint64_t> longPollIdx) {
        std::string longPollIdxParam;
        if (longPollIdx) {
            longPollIdxParam = std::to_string(*longPollIdx);
        } else {
            switch (mode) {
            case SubscriptionMode::Next:
                longPollIdxParam = "Next";
                break;
            case SubscriptionMode::Last:
                longPollIdxParam = "Last";
                break;
            case SubscriptionMode::None:
                break;
            }
        }

        auto topic = cmd.topic;
        if (!longPollIdxParam.empty()) {
            topic = URI<>::UriFactory(topic).addQueryParameter("LongPollingIdx", longPollIdxParam).build();
        }

        const auto host   = cmd.topic.hostName().value_or("");
        const auto scheme = cmd.topic.scheme().value_or("");
        const auto path   = topic.relativeRefNoFragment().value_or("/");
#ifdef OPENCMW_PROFILE_HTTP
        const auto ts = std::to_string(opencmw::load_test::timestamp().count());
#endif
        constexpr uint8_t noCopy  = NGHTTP2_NV_FLAG_NO_COPY_NAME | NGHTTP2_NV_FLAG_NO_COPY_VALUE;

        const auto        method  = (cmd.command == mdp::Command::Set) ? u8span("POST") : u8span("GET");

        auto              headers = std::vector{
            nv(u8span(":method"), method, noCopy), //
            nv(u8span(":path"), u8span(path)),     //
            nv(u8span(":scheme"), u8span(scheme)), //
            nv(u8span(":authority"), u8span(host)),
#ifdef OPENCMW_PROFILE_HTTP
            nv(u8span("x-timestamp"), u8span(ts))
#endif
        };
        if (!preferredMimeType.empty()) {
            headers.push_back(nv(u8span("accept"), u8span(preferredMimeType)));
            headers.push_back(nv(u8span("content-type"), u8span(preferredMimeType)));
        }

        RequestResponse rr;
        rr.request = std::move(cmd);
        try {
            rr.normalizedTopic = mdp::Topic::fromMdpTopic(rr.request.topic).toZmqTopic();
        } catch (...) {
            rr.normalizedTopic = rr.request.topic.str();
        }

        if (!rr.request.data.empty()) {
            // we need a pointer that survives rr being moved
            rr.body = std::make_unique<IoBuffer>(std::move(rr.request.data));
        }

        const TStreamId streamId = self().submitRequestImpl(headers, rr.body.get());
        if (streamId < 0) {
            rr.reportError(std::format("Could not submit request: {}", nghttp2_strerror(streamId)));
            return;
        }

        _requestsByStreamId.emplace(streamId, std::move(rr));
    }

    int processResponse(TStreamId streamId) {
        auto it = _requestsByStreamId.find(streamId);
        assert(it != _requestsByStreamId.end());
        if (it != _requestsByStreamId.end()) {
            const auto &request = it->second.request;
            if (it->second.responseStatus == "302") {
                std::optional<URI<>> location;
                try {
                    location = URI<>(it->second.location);
                } catch (const std::exception &e) {
                    HTTP_DBG("Client::Header: Could not parse URI '{}': {}", it->second.location, e.what());
                    it->second.reportError(std::format("Could not parse redirect URI '{}': {}", it->second.location, e.what()));
                    _requestsByStreamId.erase(it);
                    return static_cast<int>(NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE);
                }
                const auto &queryParams   = location->queryParamMap();
                auto        longPollIdxIt = queryParams.find("LongPollingIdx");
                if (longPollIdxIt == queryParams.end() || !longPollIdxIt->second.has_value()) {
                    HTTP_DBG("Client::Header: Could not find LongPollingIdx in URI '{}'", it->second.location);
                    it->second.reportError(std::format("Could not find LongPollingIdx in URI '{}'", it->second.location));
                    _requestsByStreamId.erase(it);
                    return static_cast<int>(NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE);
                }
                std::uint64_t longPollingIdx;
                if (auto ec = std::from_chars(longPollIdxIt->second->data(), longPollIdxIt->second->data() + longPollIdxIt->second->size(), longPollingIdx); ec.ec != std::errc{}) {
                    HTTP_DBG("Client::Header: Could not parse numerical LongPollingIdx from '{}'", longPollIdxIt->second);
                    it->second.reportError(std::format("Could not parse numerical LongPollingIdx from '{}'", longPollIdxIt->second));
                    _requestsByStreamId.erase(it);
                    return static_cast<int>(NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE);
                }
                // Redirect to long-polling URL
                sendLongPollRequests(it->second.normalizedTopic, longPollingIdx, longPollingIdx + kParallelLongPollingRequests - 1);
            } else if (it->second.longPollingIdx && it->second.responseStatus == "504") {
                // Server timeout on long-poll, resend same request
                sendLongPollRequests(it->second.normalizedTopic, it->second.longPollingIdx.value(), it->second.longPollingIdx.value());
            } else {
                it->second.fillResponse();
                auto       response = std::move(it->second.response);
                const auto hasError = !it->second.responseStatus.starts_with("2") && !it->second.responseStatus.starts_with("3");
                if (hasError) {
                    response.error = std::move(it->second.payload);
                } else {
                    response.data = IoBuffer(it->second.payload.data(), it->second.payload.size());
                }
                if (it->second.longPollingIdx) {
                    // Subscription
                    handleSubscriptionResponse(it->second.normalizedTopic, it->second.longPollingIdx.value(), std::move(response));
                } else {
                    // GET/SET
                    if (request.callback) {
                        request.callback(std::move(response));
                    }
                }
            }
            _requestsByStreamId.erase(it);
        } else {
            HTTP_DBG("Client::Frame: Could not find request for stream id {}", streamId);
        }
        return 0;
    }

    void reportErrorToAllPendingRequests(std::string_view error) {
        for (auto &[streamId, request] : _requestsByStreamId) {
            request.reportError(std::string{ error });
        }
        _requestsByStreamId.clear();
    }

    void handleSubscriptionResponse(std::string zmqTopic, std::uint64_t longPollingIdx, mdp::Message &&response) {
        auto subIt = _subscriptions.find(zmqTopic);
        if (subIt == _subscriptions.end()) {
            HTTP_DBG("Client::handleSubscriptionResponse: Could not find subscription for topic '{}'", zmqTopic);
            return;
        }
        auto &sub                      = subIt->second;
        sub.lastReceivedLongPollingIdx = longPollingIdx;
        auto request                   = sub.request;

        submitRequest(std::move(request), sub.mode, {}, longPollingIdx + kParallelLongPollingRequests);

        for (std::size_t i = 0; i < sub.callbacks.size(); ++i) {
            if (i < sub.callbacks.size() - 1) {
                auto copy = response;
                sub.callbacks[i](std::move(copy));
            } else {
                sub.callbacks[i](std::move(response));
            }
        }
    }

    void sendLongPollRequests(std::string zmqTopic, std::uint64_t fromLongPollingIdx, std::uint64_t toLongPollingIdx) {
        auto subIt = _subscriptions.find(zmqTopic);
        if (subIt == _subscriptions.end()) {
            HTTP_DBG("Client::sendLongPollingRequests: Could not find subscription for topic '{}'", zmqTopic);
            return;
        }
        auto &sub = subIt->second;
        for (std::uint64_t longPollingIdx = fromLongPollingIdx; longPollingIdx <= toLongPollingIdx; ++longPollingIdx) {
            auto request = sub.request;
            submitRequest(std::move(request), sub.mode, {}, longPollingIdx);
        }
    }

    void startSubscription(client::Command &&command, SubscriptionMode mode = SubscriptionMode::Next) {
        mdp::Topic topic;
        try {
            topic = mdp::Topic::fromMdpTopic(command.topic);
        } catch (const std::exception &e) {
            HTTP_DBG("Client::startSubscription: Could not parse topic '{}': {}", command.topic.str(), e.what());
            return;
        }
        const auto [subIt, inserted] = _subscriptions.try_emplace(topic.toZmqTopic(), Subscription{});
        subIt->second.request        = command;
        subIt->second.callbacks.emplace_back(command.callback);
        if (inserted) {
            submitRequest(std::move(command), mode, {}, {});
        }
    }

    void stopSubscription(client::Command &&command) {
        // TODO a single unsubscribe cancels this also in case of multiple subscriptions when the client is shared
        // inside an application. Would be great if we could selectively unsubscribe certain callbacks and finally
        // stop the subscription when all callbacks are removed.
        mdp::Topic topic;
        try {
            topic = mdp::Topic::fromMdpTopic(command.topic);
        } catch (const std::exception &e) {
            HTTP_DBG("Client::stopSubscription: Could not parse topic '{}': {}", command.topic.str(), e.what());
            return;
        };
        if (auto subIt = _subscriptions.find(topic.toZmqTopic()); subIt != _subscriptions.end()) {
            // Cancel all requests for this topic
            auto reqIt = _requestsByStreamId.begin();
            while (reqIt != _requestsByStreamId.end()) {
                if (reqIt->second.request.topic == command.topic) {
                    self().cancelStream(reqIt->first);
                    reqIt = _requestsByStreamId.erase(reqIt);
                } else {
                    ++reqIt;
                }
            }
        }
    }
};

struct Http2ClientSession : public ClientSessionBase<Http2ClientSession, int32_t> {
    TcpSocket         _socket;
    nghttp2_session  *_session = nullptr;
    WriteBuffer<1024> _writeBuffer;

    explicit Http2ClientSession(TcpSocket socket_)
        : _socket(std::move(socket_)) {
        nghttp2_session_callbacks *callbacks;
        nghttp2_session_callbacks_new(&callbacks);

        nghttp2_session_callbacks_set_send_callback2(callbacks, [](nghttp2_session *, const uint8_t *data, size_t length, int flags, void *user_data) {
            auto client = static_cast<Http2ClientSession *>(user_data);
            HTTP_DBG("Client::send {}", length);
            const auto r = client->_socket.write(data, length, flags);
            if (r < 0) {
                HTTP_DBG("Client::send failed: {}", client->_socket.lastError());
            }
            return r;
        });

        nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks, [](nghttp2_session *, uint8_t /*flags*/, int32_t stream_id, const uint8_t *data, size_t len, void *user_data) {
            auto client = static_cast<Http2ClientSession *>(user_data);
            client->_requestsByStreamId[stream_id].payload.append(reinterpret_cast<const char *>(data), len);
            return 0;
        });

        nghttp2_session_callbacks_set_on_header_callback(callbacks, [](nghttp2_session *, const nghttp2_frame *frame, const uint8_t *name, size_t namelen, const uint8_t *value, size_t valuelen, uint8_t /*flags*/, void *user_data) {
            auto       client    = static_cast<Http2ClientSession *>(user_data);
            const auto nameView  = std::string_view(reinterpret_cast<const char *>(name), namelen);
            const auto valueView = std::string_view(reinterpret_cast<const char *>(value), valuelen);
            if (!client->addHeader(frame->hd.stream_id, nameView, valueView)) {
                return static_cast<int>(NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE);
            }
            return 0;
        });
        nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks, [](nghttp2_session *, const nghttp2_frame *frame, void *user_data) {
            HTTP_DBG("Client::Frame: id={} {} {}", frame->hd.stream_id, frame->hd.type, (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) ? "END_STREAM" : "");
            switch (frame->hd.type) {
            case NGHTTP2_HEADERS:
            case NGHTTP2_DATA:
                if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
                    auto client = static_cast<Http2ClientSession *>(user_data);
                    return client->processResponse(frame->hd.stream_id);
                }
                break;
            }
            return 0;
        });
        nghttp2_session_callbacks_set_on_stream_close_callback(callbacks, [](nghttp2_session *, int32_t stream_id, uint32_t /*error_code*/, void *user_data) {
            auto client = static_cast<Http2ClientSession *>(user_data);
            client->_requestsByStreamId.erase(stream_id);
            HTTP_DBG("Client::Stream closed: {}", stream_id);
            return 0;
        });

        nghttp2_session_client_new(&_session, callbacks, this);
        nghttp2_session_callbacks_del(callbacks);

        nghttp2_settings_entry iv[1] = {
            { NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 1000 }
        };

        if (nghttp2_submit_settings(_session, NGHTTP2_FLAG_NONE, iv, 1) != 0) {
            HTTP_DBG("Client::ClientSession: nghttp2_submit_settings failed");
        }
    }

    Http2ClientSession(const Http2ClientSession &)                     = delete;
    Http2ClientSession &operator=(const Http2ClientSession &)          = delete;
    Http2ClientSession(Http2ClientSession &&other) noexcept            = delete;
    Http2ClientSession &operator=(Http2ClientSession &&other) noexcept = delete;

    ~Http2ClientSession() {
        nghttp2_session_del(_session);
    }

    bool isReady() const {
        return _socket._state == TcpSocket::Connected;
    }

    std::expected<void, std::string> continueToMakeReady() {
        auto makeError = [](std::string_view msg) {
            return std::unexpected(std::format("Could not connect to endpoint: {}", msg));
        };
        assert(!isReady());
        if (_socket._state == detail::TcpSocket::Connecting) {
            if (auto rc = _socket.connect(); !rc) {
                return makeError(rc.error());
            }
        }

        if (_socket._state == detail::TcpSocket::SSLConnectWantsRead || _socket._state == detail::TcpSocket::SSLConnectWantsWrite) {
            if (auto rc = _socket.continueHandshake(); !rc) {
                return makeError(rc.error());
            }
        }

        return {};
    }

    bool wantsToRead() const {
        return _socket._state == TcpSocket::Connected ? nghttp2_session_want_read(_session) : (_socket._state == TcpSocket::Connecting || _socket._state == TcpSocket::SSLConnectWantsRead);
    }

    bool wantsToWrite() const {
        return _socket._state == TcpSocket::Connected ? _writeBuffer.wantsToWrite(_session) : (_socket._state == TcpSocket::Connecting || _socket._state == TcpSocket::SSLConnectWantsWrite);
    }

    int32_t submitRequestImpl(const std::vector<nghttp2_nv> &headers, IoBuffer *body) {
        nghttp2_data_provider2 data_prd;
        data_prd.read_callback = nullptr;

        if (body && !body->empty()) {
            data_prd.source.ptr    = body;
            data_prd.read_callback = [](nghttp2_session *, int32_t /*stream_id*/, uint8_t *buf, size_t length, uint32_t *data_flags, nghttp2_data_source *source, void * /*user_data*/) {
                auto              ioBuffer = static_cast<IoBuffer *>(source->ptr);
                const std::size_t copy_len = std::min(length, ioBuffer->size() - ioBuffer->position());
                std::copy(ioBuffer->data() + ioBuffer->position(), ioBuffer->data() + ioBuffer->position() + copy_len, buf);
                ioBuffer->skip(static_cast<int>(copy_len));
                if (ioBuffer->position() == ioBuffer->size()) {
                    *data_flags |= NGHTTP2_DATA_FLAG_EOF;
                }
                return static_cast<ssize_t>(copy_len);
            };
        }

        auto streamId = nghttp2_submit_request2(_session, nullptr, headers.data(), headers.size(), &data_prd, nullptr);
        if (streamId < 0) {
            HTTP_DBG("Client::submitRequest: nghttp2_submit_request failed: {}", nghttp2_strerror(streamId));
        }
        return streamId;
    }

    void cancelStream(int32_t streamId) {
        HTTP_DBG("Client::cancelStream: id={}", streamId);
        if (nghttp2_submit_rst_stream(_session, NGHTTP2_FLAG_NONE, streamId, NGHTTP2_CANCEL) != 0) {
            HTTP_DBG("Client::cancelStream: nghttp2_submit_rst_stream failed");
        }
    }
};

} // namespace detail

struct ClientCertificates {
    std::string _certificates;

    ClientCertificates() = default;
    ClientCertificates(std::string X509_ca_bundle) noexcept
        : _certificates(std::move(X509_ca_bundle)) {}
    constexpr operator std::string() const noexcept { return _certificates; };
};

struct RestClient : public ClientBase {
    struct SslSettings {
        std::string caCertificate;
        bool        verifyPeers = true;
    };
    bool                                                                       _forceHttp2 = false; // Force HTTP/2
    std::jthread                                                               _worker;
    MIME::MimeType                                                             _mimeType = opencmw::MIME::JSON;
    SslSettings                                                                _sslSettings;
    std::shared_ptr<detail::SharedQueue<std::pair<Command, SubscriptionMode>>> _requestQueue = std::make_shared<detail::SharedQueue<std::pair<Command, SubscriptionMode>>>();

    static std::expected<detail::Http2ClientSession *, std::string>
    ensureSession(detail::SSL_CTX_Ptr &ssl_ctx, std::map<detail::Endpoint, std::unique_ptr<detail::Http2ClientSession>> &sessions, const SslSettings &sslSettings, URI<> topic) {
        if (topic.scheme() != "http" && topic.scheme() != "https") {
            return std::unexpected(std::format("Unsupported protocol '{}' for endpoint '{}'", topic.scheme().value_or(""), topic.str()));
        }
        if (topic.hostName().value_or("").empty()) {
            return std::unexpected(std::format("No host provided for endpoint '{}'", topic.str()));
        }
        const auto port      = topic.port().value_or(topic.scheme() == "https" ? 443 : 80);
        const auto endpoint  = detail::Endpoint{ topic.scheme().value(), topic.hostName().value(), port };

        auto       sessionIt = sessions.find(endpoint);
        if (sessionIt != sessions.end()) {
            return sessionIt->second.get();
        }

        int socketFlags = detail::TcpSocket::None;
        if (topic.scheme() == "https") {
            if (!ssl_ctx) {
                ssl_ctx = detail::SSL_CTX_Ptr(SSL_CTX_new(SSLv23_client_method()), SSL_CTX_free);
                if (!ssl_ctx) {
                    return std::unexpected(std::format("Could not create SSL/TLS context: {}", ERR_error_string(ERR_get_error(), nullptr)));
                }
                SSL_CTX_set_options(ssl_ctx.get(),
                        SSL_OP_ALL | SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_COMPRESSION | SSL_OP_NO_SESSION_RESUMPTION_ON_RENEGOTIATION);

                SSL_CTX_set_alpn_protos(ssl_ctx.get(), reinterpret_cast<const unsigned char *>("\x02h2"), 3);

                if (sslSettings.verifyPeers) {
                    SSL_CTX_set_verify(ssl_ctx.get(), SSL_VERIFY_PEER, nullptr);

                    if (!sslSettings.caCertificate.empty()) {
                        auto maybeStore = detail::createCertificateStore(sslSettings.caCertificate);
                        if (!maybeStore) {
                            return std::unexpected(std::format("Could not create certificate store: {}", maybeStore.error()));
                        }
                        SSL_CTX_set_cert_store(ssl_ctx.get(), maybeStore->release());
                    }
                }
            }

            auto ssl = detail::create_ssl(ssl_ctx.get());
            if (!ssl) {
                return std::unexpected(std::format("Failed to create SSL object: {}", ssl.error()));
            }
            if (sslSettings.verifyPeers) {
                socketFlags |= detail::TcpSocket::VerifyPeer;
            }
            auto maybeSocket = detail::TcpSocket::create(std::move(ssl.value()), socket(AF_INET, SOCK_STREAM, 0), socketFlags);
            if (!maybeSocket) {
                return std::unexpected(std::format("Failed to create socket: {}", maybeSocket.error()));
            }
            auto session = std::make_unique<detail::Http2ClientSession>(std::move(maybeSocket.value()));
            if (auto rc = session->_socket.prepareConnect(endpoint.host, endpoint.port); !rc) {
                return std::unexpected(rc.error());
            }
            sessionIt = sessions.emplace(endpoint, std::move(session)).first;
            return sessionIt->second.get();
        }

        // HTTP
        auto maybeSocket = detail::TcpSocket::create({ nullptr, SSL_free }, socket(AF_INET, SOCK_STREAM, 0), socketFlags);
        if (!maybeSocket) {
            return std::unexpected(std::format("Failed to create socket: {}", maybeSocket.error()));
        }
        auto session = std::make_unique<detail::Http2ClientSession>(std::move(maybeSocket.value()));
        if (auto rc = session->_socket.prepareConnect(endpoint.host, endpoint.port); !rc) {
            return std::unexpected(rc.error());
        }
        sessionIt = sessions.emplace(endpoint, std::move(session)).first;
        return sessionIt->second.get();
    }

public:
    template<typename... Args>
    explicit(false) RestClient(Args... initArgs)
        : _mimeType(detail::find_argument_value<true, DefaultContentTypeHeader>([] { return MIME::JSON; }, initArgs...))
        , _sslSettings{
            .caCertificate = detail::find_argument_value<true, ClientCertificates>([] { return ClientCertificates{}; }, initArgs...),
            .verifyPeers = detail::find_argument_value<true, VerifyServerCertificates>([] { return true; }, initArgs...)
        } {
        _worker = std::jthread([queue = _requestQueue, sslSettings = _sslSettings, mimeType = _mimeType](std::stop_token stopToken) {
            auto preferredMimeType = [&mimeType](const URI<> &topic) {
                if (const auto contentTypeHeader = topic.queryParamMap().find("contentType"); contentTypeHeader != topic.queryParamMap().end() && contentTypeHeader->second) {
                    return contentTypeHeader->second.value();
                }
                return std::string{ mimeType.typeName() };
            };

            detail::SSL_CTX_Ptr                                                     ssl_ctx{ nullptr, SSL_CTX_free };

            std::map<detail::Endpoint, std::unique_ptr<detail::Http2ClientSession>> sessions;

            auto                                                                    reportError = [](Command &cmd, std::string error) {
                if (!cmd.callback) {
                    return;
                }
                mdp::Message msg;
                msg.protocolName    = cmd.topic.scheme().value_or("");
                msg.arrivalTime     = std::chrono::system_clock::now();
                msg.command         = mdp::Command::Final;
                msg.clientRequestID = cmd.clientRequestID;
                msg.topic           = cmd.topic;
                msg.error           = std::move(error);
                cmd.callback(msg);
            };

            std::vector<struct pollfd> pollFds;

            while (!stopToken.stop_requested()) {
                while (auto entry = queue->try_get()) {
                    auto &[cmd, mode] = entry.value();
                    switch (cmd.command) {
                    case mdp::Command::Get:
                    case mdp::Command::Set: {
                        auto session   = ensureSession(ssl_ctx, sessions, sslSettings, cmd.topic);
                        auto preferred = preferredMimeType(cmd.topic);
                        session.value()->submitRequest(std::move(cmd), mode, std::move(preferred), {});
                    } break;
                    case mdp::Command::Subscribe: {
                        auto session = ensureSession(ssl_ctx, sessions, sslSettings, cmd.topic);
                        if (!session) {
                            reportError(cmd, std::format("Unsupported endpoint '{}': {}", cmd.topic.str(), session.error()));
                            continue;
                        }
                        session.value()->startSubscription(std::move(cmd), mode);
                    } break;
                    case mdp::Command::Unsubscribe: {
                        auto session = ensureSession(ssl_ctx, sessions, sslSettings, cmd.topic);
                        if (!session) {
                            reportError(cmd, std::format("Unsupported endpoint '{}': {}", cmd.topic.str(), session.error()));
                            continue;
                        }
                        session.value()->stopSubscription(std::move(cmd));
                    } break;
                    case mdp::Command::Final:
                    case mdp::Command::Partial:
                    case mdp::Command::Heartbeat:
                    case mdp::Command::Notify:
                    case mdp::Command::Invalid:
                    case mdp::Command::Ready:
                    case mdp::Command::Disconnect:
                        assert(false); // unexpected command
                        break;
                    }
                }

                pollFds.clear();
                pollFds.reserve(sessions.size());
                for (auto &sessionPair : sessions) {
                    auto         &session = sessionPair.second;
                    struct pollfd pfd     = {};
                    pfd.fd                = session->_socket.fd;
                    pfd.events            = 0;
                    if (session->wantsToRead()) {
                        pfd.events |= POLLIN;
                    }
                    if (session->wantsToWrite()) {
                        pfd.events |= POLLOUT;
                    }
                    pollFds.push_back(pfd);
                }

                if (pollFds.empty()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    continue;
                }

                const auto n = ::poll(pollFds.data(), pollFds.size(), 100);

                if (n < 0) {
                    HTTP_DBG("poll failed: {}", strerror(errno));
                    continue;
                }
                if (n == 0) {
                    continue;
                }

                auto sessionIt = sessions.begin();
                while (sessionIt != sessions.end()) {
                    auto &session = sessionIt->second;

                    auto  pollIt  = std::ranges::find_if(pollFds, [&](const struct pollfd &pfd) {
                        return pfd.fd == session->_socket.fd;
                    });

                    if (pollIt == pollFds.end()) {
                        ++sessionIt;
                        continue;
                    }

                    if (pollIt->revents & POLLERR) {
                        int       error  = 0;
                        socklen_t errlen = sizeof(error);
                        getsockopt(pollIt->fd, SOL_SOCKET, SO_ERROR, &error, &errlen);
                        session->reportErrorToAllPendingRequests(strerror(error));
                        sessionIt = sessions.erase(sessionIt);
                        continue;
                    }

                    if (((pollIt->revents & POLLIN) || (pollIt->revents & POLLOUT)) && !session->isReady()) {
                        if (auto r = session->continueToMakeReady(); !r) {
                            sessionIt->second->reportErrorToAllPendingRequests(r.error());
                            sessionIt = sessions.erase(sessionIt);
                            continue;
                        }
                    }

                    if (!session->isReady()) {
                        ++sessionIt;
                        continue;
                    }

                    if (pollIt->revents & POLLOUT) {
                        if (!session->_writeBuffer.write(session->_session, session->_socket)) {
                            HTTP_DBG("Client: Failed to write to peer (fd={}): {}", session->_socket.fd, session->_socket.lastError());
                            sessionIt = sessions.erase(sessionIt);
                            continue;
                        }
                    }

                    if (pollIt->revents & POLLIN) {
                        bool mightHaveMore = true;
                        bool hasError      = false;

                        while (mightHaveMore && !hasError) {
                            std::array<uint8_t, 2048> buffer;
                            const auto                bytes_read = session->_socket.read(buffer.data(), buffer.size());
                            if (bytes_read <= 0 && errno != EAGAIN) {
                                if (bytes_read < 0) {
                                    HTTP_DBG("Client::read failed: {}", session->_socket.lastError());
                                }
                                hasError = true;
                                continue;
                            }

                            if (bytes_read > 0 && nghttp2_session_mem_recv2(session->_session, buffer.data(), static_cast<size_t>(bytes_read)) < 0) {
                                HTTP_DBG("Client: nghttp2_session_mem_recv2 failed");
                                hasError = true;
                                continue;
                            }
                            mightHaveMore = bytes_read == static_cast<ssize_t>(buffer.size());
                        }
                        if (hasError) {
                            sessionIt = sessions.erase(sessionIt);
                            continue;
                        }
                    }

                    ++sessionIt;
                }
            }
        });
    }

    ~RestClient() {
        stop();
    }

    [[nodiscard]]
    MIME::MimeType defaultMimeType() const {
        return _mimeType;
    }

    [[nodiscard]]
    bool verifySslPeers() const {
        return _sslSettings.verifyPeers;
    }

    [[nodiscard]]
    std::vector<std::string> protocols() override {
        return { "http", "https" };
    }

    void stop() override {
        _worker.request_stop();
        if (_worker.joinable()) {
            _worker.join();
        }
    }

    void request(Command cmd) override {
        switch (cmd.command) {
        case mdp::Command::Get:
        case mdp::Command::Set:
            assert(cmd.callback);
            _requestQueue->push({ std::move(cmd), SubscriptionMode::None });
            break;
        case mdp::Command::Subscribe:
            assert(cmd.callback);
            _requestQueue->push({ std::move(cmd), SubscriptionMode::Next });
            break;
        case mdp::Command::Unsubscribe:
            _requestQueue->push({ std::move(cmd), SubscriptionMode::None });
            break;
        default:
            assert(false); // unexpected command
        }
    }

    mdp::Message blockingRequest(Command cmd) {
        std::promise<mdp::Message> promise;
        auto                       future = promise.get_future();
        cmd.callback                      = [&promise](const mdp::Message &msg) {
            promise.set_value(std::move(msg));
        };
        request(std::move(cmd));
        return future.get();
    }
};

} // namespace opencmw::client

#endif // OPENCMW_CLIENT_RESTCLIENTNATIVE_HPP
