#ifndef OPENCMW_CLIENT_RESTCLIENTNATIVE_HPP
#define OPENCMW_CLIENT_RESTCLIENTNATIVE_HPP

#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <mutex>
#include <netinet/tcp.h>
#include <poll.h>
#include <stop_token>
#include <string>
#include <sys/poll.h>
#include <thread>
#include <unistd.h>

#include <fmt/format.h>
#include <fmt/std.h>

#include <nghttp2/nghttp2.h>

#include "ClientCommon.hpp"
#include "ClientContext.hpp"
#include "MdpMessage.hpp"
#include "MIME.hpp"
#include "nghttp2/NgHttp2Utils.hpp"
#include "Topic.hpp"

namespace opencmw::client {
enum class SubscriptionMode {
    Next,
    FirstAvailable,
    Last,
    None
};

namespace detail {

using namespace opencmw::nghttp2;
using namespace opencmw::nghttp2::detail;

template<typename T>
struct SharedQueue {
    // TODO(Frank) use a lock-free queue? This is only used client-side, so not that critical
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
    client::Command request;
    std::string     normalizedTopic;

    // response data
    std::string                  responseStatus;
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

struct Subscription {
    client::Command                                   request;
    SubscriptionMode                                  mode;
    std::optional<std::uint64_t>                      longPollingIdx;
    std::vector<std::function<void(mdp::Message &&)>> callbacks;
};

struct Endpoint {
    std::string scheme;
    std::string host;
    uint16_t    port;

    auto        operator<=>(const Endpoint &) const = default;
};

struct ClientSession {
    struct PendingRequest {
        client::Command              command;
        SubscriptionMode             mode;
        std::string                  preferredMimeType;
        std::optional<std::uint64_t> longPollIdx;
    };

    TcpSocket                           _socket;
    nghttp2_session                    *_session = nullptr;
    WriteBuffer<1024>                   _writeBuffer;
    std::map<std::string, Subscription> _subscriptions;
    std::map<int32_t, RequestResponse>  _requestsByStreamId;

    explicit ClientSession(TcpSocket socket_)
        : _socket(std::move(socket_)) {
        nghttp2_session_callbacks *callbacks;
        nghttp2_session_callbacks_new(&callbacks);

        nghttp2_session_callbacks_set_send_callback2(callbacks, [](nghttp2_session *, const uint8_t *data, size_t length, int flags, void *user_data) {
            auto client = static_cast<ClientSession *>(user_data);
            HTTP_DBG("Client::send {}", length);
            const auto r = client->_socket.write(data, length, flags);
            if (r < 0) {
                HTTP_DBG("Client::send failed: {}", client->_socket.lastError());
            }
            return r;
        });

        nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks, [](nghttp2_session *, uint8_t /*flags*/, int32_t stream_id, const uint8_t *data, size_t len, void *user_data) {
            auto client = static_cast<ClientSession *>(user_data);
            client->_requestsByStreamId[stream_id].payload.append(reinterpret_cast<const char *>(data), len);
            return 0;
        });

        nghttp2_session_callbacks_set_on_header_callback(callbacks, [](nghttp2_session *, const nghttp2_frame *frame, const uint8_t *name, size_t namelen, const uint8_t *value, size_t valuelen, uint8_t /*flags*/, void *user_data) {
            auto       client    = static_cast<ClientSession *>(user_data);
            const auto nameView  = std::string_view(reinterpret_cast<const char *>(name), namelen);
            const auto valueView = std::string_view(reinterpret_cast<const char *>(value), valuelen);
            HTTP_DBG("Client::Header: id={} {} = {}", frame->hd.stream_id, nameView, valueView);
            if (nameView == ":status") {
                client->_requestsByStreamId[frame->hd.stream_id].responseStatus = std::string(valueView);
            } else if (nameView == "x-opencmw-topic") {
                try {
                    client->_requestsByStreamId[frame->hd.stream_id].response.topic = URI<>(std::string(valueView));
                } catch (const std::exception &e) {
                    HTTP_DBG("Client::Header: Could not parse URI '{}': {}", valueView, e.what());
                    return static_cast<int>(NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE);
                }
            } else if (nameView == "x-opencmw-service-name") {
                client->_requestsByStreamId[frame->hd.stream_id].response.serviceName = std::string(valueView);
            } else if (nameView == "x-opencmw-long-polling-idx") {
                std::uint64_t longPollingIdx;
                if (auto ec = std::from_chars(valueView.data(), valueView.data() + valueView.size(), longPollingIdx); ec.ec != std::errc{}) {
                    HTTP_DBG("Client::Header: Could not parse x-opencmw-long-polling-idx '{}'", valueView);
                    return static_cast<int>(NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE);
                }
                client->_requestsByStreamId[frame->hd.stream_id].longPollingIdx = longPollingIdx;
#ifdef OPENCMW_PROFILE_HTTP
            } else if (nameView == "x-timestamp") {
                fmt::println(std::cerr, "Client::Header: x-timestamp: {} (latency {} ns)", valueView, latency(valueView).count());
#endif
            }

            return 0;
        });
        nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks, [](nghttp2_session *, const nghttp2_frame *frame, void *user_data) {
            HTTP_DBG("Client::Frame: id={} {} {}", frame->hd.stream_id, frame->hd.type, (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) ? "END_STREAM" : "");
            auto client = static_cast<ClientSession *>(user_data);
            switch (frame->hd.type) {
            case NGHTTP2_HEADERS:
            case NGHTTP2_DATA:
                if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
                    auto it = client->_requestsByStreamId.find(frame->hd.stream_id);
                    assert(it != client->_requestsByStreamId.end());
                    if (it != client->_requestsByStreamId.end()) {
                        it->second.fillResponse();
                        const auto &request  = it->second.request;
                        auto        response = std::move(it->second.response);
                        if (it->second.longPollingIdx && it->second.responseStatus == "504") {
                            // Server timeout on long-poll, resend same request
                            client->resendLongPollRequest(it->second.normalizedTopic, it->second.longPollingIdx.value());
                        } else {
                            const auto hasError = !it->second.responseStatus.starts_with("2") && !it->second.responseStatus.starts_with("3");
                            if (hasError) {
                                response.error = std::move(it->second.payload);
                            } else {
                                response.data = IoBuffer(it->second.payload.data(), it->second.payload.size());
                            }
                            if (it->second.longPollingIdx) {
                                // Subscription
                                client->handleSubscriptionResponse(it->second.normalizedTopic, it->second.longPollingIdx.value(), std::move(response));
                            } else {
                                // GET/SET
                                if (request.callback) {
                                    request.callback(std::move(response));
                                }
                            }
                        }
                        client->_requestsByStreamId.erase(it);
                    } else {
                        HTTP_DBG("Client::Frame: Could not find request for stream id {}", frame->hd.stream_id);
                    }
                }
                break;
            }
            return 0;
        });
        nghttp2_session_callbacks_set_on_stream_close_callback(callbacks, [](nghttp2_session *, int32_t stream_id, uint32_t /*error_code*/, void *user_data) {
            auto client = static_cast<ClientSession *>(user_data);
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

    ClientSession(const ClientSession &)                     = delete;
    ClientSession &operator=(const ClientSession &)          = delete;
    ClientSession(ClientSession &&other) noexcept            = delete;
    ClientSession &operator=(ClientSession &&other) noexcept = delete;

    ~ClientSession() {
        nghttp2_session_del(_session);
    }

    bool isReady() const {
        return _socket._state == TcpSocket::Connected;
    }

    std::expected<void, std::string> continueToMakeReady() {
        auto makeError = [](std::string_view msg) {
            return std::unexpected(fmt::format("Could not connect to endpoint: {}", msg));
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

    void submitRequest(client::Command &&cmd, SubscriptionMode mode, std::string preferredMimeType, std::optional<std::uint64_t> longPollIdx) {
        auto topic = cmd.topic;
        if (cmd.command == mdp::Command::Set) {
            topic = URI<>::UriFactory(topic).addQueryParameter("_bodyOverride", std::string{ cmd.data.asString() }).build();
        }

        std::string longPollIdxParam;
        if (longPollIdx) {
            longPollIdxParam = std::to_string(*longPollIdx);
        } else {
            switch (mode) {
            case SubscriptionMode::Next:
                longPollIdxParam = "Next";
                break;
            case SubscriptionMode::FirstAvailable:
                longPollIdxParam = "FirstAvailable";
                break;
            case SubscriptionMode::Last:
                longPollIdxParam = "Last";
                break;
            case SubscriptionMode::None:
                break;
            }
        }
        if (!longPollIdxParam.empty()) {
            topic = URI<>::UriFactory(topic).addQueryParameter("LongPollingIdx", longPollIdxParam).build();
        }

        const auto host = cmd.topic.hostName().value_or("");
        const auto path = topic.relativeRefNoFragment().value_or("/");
#ifdef OPENCMW_PROFILE_HTTP
        const auto ts = std::to_string(timestamp().count());
#endif
        constexpr uint8_t noCopy  = NGHTTP2_NV_FLAG_NO_COPY_NAME | NGHTTP2_NV_FLAG_NO_COPY_VALUE;
        auto              headers = std::vector{
            nv(u8span(":method"), u8span("GET"), noCopy),  //
            nv(u8span(":path"), u8span(path)),             //
            nv(u8span(":scheme"), u8span("http"), noCopy), //
            nv(u8span(":authority"), u8span(host)),
#ifdef OPENCMW_PROFILE_HTTP
            nv(u8span("x-timestamp"), u8span(ts))
#endif
        };
        if (!preferredMimeType.empty()) {
            headers.push_back(nv(u8span("accept"), u8span(preferredMimeType)));
            headers.push_back(nv(u8span("content-type"), u8span(preferredMimeType)));
        }
        if (cmd.command == mdp::Command::Set) {
            headers.push_back(nv(u8span("x-opencmw-method"), u8span("PUT"), noCopy));
        }

        RequestResponse rr;
        rr.request = std::move(cmd);
        try {
            rr.normalizedTopic = mdp::Topic::fromMdpTopic(rr.request.topic).toZmqTopic();
        } catch (...) {
            rr.normalizedTopic = rr.request.topic.str();
        }

        const std::int32_t streamId = nghttp2_submit_request2(_session, nullptr, headers.data(), headers.size(), nullptr, nullptr);
        if (streamId < 0) {
            rr.reportError(fmt::format("Could not submit request: {}", nghttp2_strerror(streamId)));
            return;
        }

        _requestsByStreamId.emplace(streamId, std::move(rr));
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
        auto &sub          = subIt->second;
        sub.longPollingIdx = longPollingIdx;

        // Start next poll
        // TODO(Frank) we could have multiple long-polling requests open in parallel
        auto request = sub.request;

        submitRequest(std::move(request), sub.mode, {}, longPollingIdx + 1);

        for (std::size_t i = 0; i < sub.callbacks.size(); ++i) {
            if (i == sub.callbacks.size() - 1) {
                sub.callbacks[i](std::move(response));
            } else {
                auto responseCopy = response;
                sub.callbacks[i](std::move(responseCopy));
            }
        }
    }

    void resendLongPollRequest(std::string zmqTopic, std::uint64_t longPollingIdx) {
        auto subIt = _subscriptions.find(zmqTopic);
        if (subIt == _subscriptions.end()) {
            HTTP_DBG("Client::handleSubscriptionResponse: Could not find subscription for topic '{}'", zmqTopic);
            return;
        }
        auto &sub          = subIt->second;
        sub.longPollingIdx = longPollingIdx;
        auto request       = sub.request;
        submitRequest(std::move(request), sub.mode, {}, longPollingIdx);
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
        subIt->second.callbacks.push_back(std::move(command.callback));
        if (inserted) {
            submitRequest(std::move(command), mode, {}, {});
        }
    }
};

} // namespace detail

struct ClientCertificates {
    std::string _certificates;

    ClientCertificates() = default;
    ClientCertificates(const char *X509_ca_bundle) noexcept
        : _certificates(X509_ca_bundle){};
    ClientCertificates(const std::string &X509_ca_bundle) noexcept
        : _certificates(X509_ca_bundle){};
    constexpr operator std::string() const noexcept { return _certificates; };
};

struct RestClient : public ClientBase {
    struct SslSettings {
        std::string caCertificate;
        bool        verifyPeers = true;
    };

    std::jthread                                                               _worker;
    MIME::MimeType                                                             _mimeType = opencmw::MIME::JSON;
    SslSettings                                                                _sslSettings;
    std::shared_ptr<detail::SharedQueue<std::pair<Command, SubscriptionMode>>> _requestQueue = std::make_shared<detail::SharedQueue<std::pair<Command, SubscriptionMode>>>();

    static std::expected<detail::ClientSession *, std::string>
    ensureSession(detail::SSL_CTX_Ptr &ssl_ctx, std::map<detail::Endpoint, std::unique_ptr<detail::ClientSession>> &sessions, const SslSettings &sslSettings, URI<> topic) {
        if (topic.scheme() != "http" && topic.scheme() != "https") {
            return std::unexpected(fmt::format("Unsupported protocol '{}' for endpoint '{}'", topic.scheme().value_or(""), topic.str()));
        }
        if (topic.hostName().value_or("").empty()) {
            return std::unexpected(fmt::format("No host provided for endpoint '{}'", topic.str()));
        }
        const auto port     = topic.port().value_or(topic.scheme() == "https" ? 443 : 80);
        const auto endpoint = detail::Endpoint{ topic.scheme().value(), topic.hostName().value(), port };

        auto       sessionIt = sessions.find(endpoint);
        if (sessionIt != sessions.end()) {
            return sessionIt->second.get();
        }

        int socketFlags = detail::TcpSocket::None;
        if (topic.scheme() == "https") {
            if (!ssl_ctx) {
                ssl_ctx = detail::SSL_CTX_Ptr(SSL_CTX_new(SSLv23_client_method()), SSL_CTX_free);
                if (!ssl_ctx) {
                    return std::unexpected(fmt::format("Could not create SSL/TLS context: {}", ERR_error_string(ERR_get_error(), nullptr)));
                }
                SSL_CTX_set_options(ssl_ctx.get(),
                        SSL_OP_ALL | SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_COMPRESSION | SSL_OP_NO_SESSION_RESUMPTION_ON_RENEGOTIATION);

                SSL_CTX_set_alpn_protos(ssl_ctx.get(), reinterpret_cast<const unsigned char *>("\x02h2"), 3);

                if (sslSettings.verifyPeers) {
                    SSL_CTX_set_verify(ssl_ctx.get(), SSL_VERIFY_PEER, nullptr);

                    if (!sslSettings.caCertificate.empty()) {
                        auto maybeStore = detail::createCertificateStore(sslSettings.caCertificate);
                        if (!maybeStore) {
                            return std::unexpected(fmt::format("Could not create certificate store: {}", maybeStore.error()));
                        }
                        SSL_CTX_set_cert_store(ssl_ctx.get(), maybeStore->release());
                    }
                }
            }

            auto ssl = detail::create_ssl(ssl_ctx.get());
            if (!ssl) {
                return std::unexpected(fmt::format("Failed to create SSL object: {}", ssl.error()));
            }
            if (sslSettings.verifyPeers) {
                socketFlags |= detail::TcpSocket::VerifyPeer;
            }
            auto session = std::make_unique<detail::ClientSession>(detail::TcpSocket(std::move(ssl.value()), socket(AF_INET, SOCK_STREAM, 0), socketFlags));
            if (auto rc = session->_socket.prepareConnect(endpoint.host, endpoint.port); !rc) {
                return std::unexpected(rc.error());
            }
            sessionIt = sessions.emplace(endpoint, std::move(session)).first;
            return sessionIt->second.get();
        }

        // HTTP
        auto session = std::make_unique<detail::ClientSession>(detail::TcpSocket({ nullptr, SSL_free }, socket(AF_INET, SOCK_STREAM, 0), socketFlags));
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

            detail::SSL_CTX_Ptr                                                ssl_ctx{ nullptr, SSL_CTX_free };

            std::map<detail::Endpoint, std::unique_ptr<detail::ClientSession>> sessions;

            auto                                                               reportError = [](Command &cmd, std::string error) {
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
                            reportError(cmd, fmt::format("Unsupported endpoint '{}': {}", cmd.topic.str(), session.error()));
                            continue;
                        }
                        session.value()->startSubscription(std::move(cmd), mode);
                    } break;
                    case mdp::Command::Unsubscribe:
                        // TODO
                        break;
                    case mdp::Command::Final:
                    case mdp::Command::Partial:
                    case mdp::Command::Heartbeat:
                    case mdp::Command::Notify:
                    case mdp::Command::Invalid:
                    case mdp::Command::Ready:
                    case mdp::Command::Disconnect:
                        // TODO report error (unexpected command)
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
                        std::array<uint8_t, 1024> buffer;
                        const auto bytes_read = session->_socket.read(buffer.data(), buffer.size());
                        if (bytes_read <= 0 && errno != EAGAIN) {
                            if (bytes_read < 0) {
                                HTTP_DBG("Client::read failed: {}", session->_socket.lastError());
                            }
                            sessionIt = sessions.erase(sessionIt);
                            continue;
                        }

                        if (bytes_read > 0 && nghttp2_session_mem_recv2(session->_session, buffer.data(), static_cast<size_t>(bytes_read)) < 0) {
                            HTTP_DBG("Client: nghttp2_session_mem_recv2 failed");
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
            assert(cmd.callback); // TODO(Frank) allow SET without callback?
            _requestQueue->push({ std::move(cmd), SubscriptionMode::None });
            break;
        case mdp::Command::Subscribe:
            assert(cmd.callback);
            // TODO change ClientCommon API to allow different modes?
            _requestQueue->push({ std::move(cmd), SubscriptionMode::Next });
            break;
        case mdp::Command::Unsubscribe:
            // TODO
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
