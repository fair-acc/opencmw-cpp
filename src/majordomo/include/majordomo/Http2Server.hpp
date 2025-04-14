#ifndef OPENCMW_MAJORDOMO_HTTP2SERVER_HPP
#define OPENCMW_MAJORDOMO_HTTP2SERVER_HPP

#include "IoBuffer.hpp"
#include "LoadTest.hpp"
#include "MdpMessage.hpp"
#include "MIME.hpp"
#include "nghttp2/NgHttp2Utils.hpp"
#include "nghttp2/NgHttp3Utils.hpp"
#include "Rest.hpp"
#include "Topic.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <expected>
#include <filesystem>
#include <memory>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <nghttp2/nghttp2.h>
#include <optional>
#include <ranges>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <vector>

#include <openssl/bio.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/quic.h>
#include <openssl/ssl.h>

#include <zmq.h>

#include <fmt/format.h>

#include <nghttp3/nghttp3.h>
#include <ngtcp2/ngtcp2.h>
#include <ngtcp2/ngtcp2_crypto.h>

constexpr size_t NGTCP2_SV_SCIDLEN = 18;

namespace opencmw::majordomo::detail::nghttp2 {

using namespace opencmw::nghttp2;
using namespace opencmw::nghttp2::detail;

enum class ContextType {
    Tcp,
    Quic,
};

inline int alpn_select_proto_cb(SSL * /*ssl*/, const unsigned char **out,
        unsigned char *outlen, const unsigned char *in,
        unsigned int inlen,
        void * /*arg*/) {
    if (nghttp2_select_alpn(out, outlen, in, inlen) != -1) {
        return SSL_TLSEXT_ERR_NOACK;
    }

    return SSL_TLSEXT_ERR_OK;
}

template<ContextType TContext>
inline std::expected<SSL_CTX_Ptr, std::string> create_ssl_ctx(EVP_PKEY *key, X509 *cert) {
    constexpr auto Method  = TContext == ContextType::Tcp ? TLS_server_method : OSSL_QUIC_server_method;
    auto           ssl_ctx = SSL_CTX_Ptr(SSL_CTX_new(Method()), SSL_CTX_free);
    if (!ssl_ctx) {
        return std::unexpected(fmt::format("Could not create SSL/TLS context: {}", ERR_error_string(ERR_get_error(), nullptr)));
    }
    if constexpr (TContext == ContextType::Tcp) {
        SSL_CTX_set_options(ssl_ctx.get(), SSL_OP_ALL | SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_COMPRESSION | SSL_OP_NO_SESSION_RESUMPTION_ON_RENEGOTIATION);
    }
    if (SSL_CTX_set1_curves_list(ssl_ctx.get(), "P-256") != 1) {
        return std::unexpected(fmt::format("SSL_CTX_set1_curves_list failed: {}", ERR_error_string(ERR_get_error(), nullptr)));
    }

    if (SSL_CTX_use_PrivateKey(ssl_ctx.get(), key) <= 0) {
        return std::unexpected(fmt::format("Could not configure private key"));
    }
    if (SSL_CTX_use_certificate(ssl_ctx.get(), cert) != 1) {
        return std::unexpected(fmt::format("Could not configure certificate file"));
    }

    if (!SSL_CTX_check_private_key(ssl_ctx.get())) {
        return std::unexpected("Private key does not match the certificate");
    }

    if constexpr (TContext == ContextType::Tcp) {
        SSL_CTX_set_alpn_select_cb(ssl_ctx.get(), alpn_select_proto_cb, nullptr);
    }

    return ssl_ctx;
}

using Message = mdp::BasicMessage<mdp::MessageFormat::WithSourceId>;

enum class RestMethod {
    Get,
    LongPoll,
    Post,
    Invalid
};

inline RestMethod parseMethod(std::string_view methodString) {
    using enum RestMethod;
    return methodString == "POLL" ? LongPoll
         : methodString == "PUT"  ? Post
         : methodString == "POST" ? Post
         : methodString == "GET"  ? Get
                                  : Invalid;
}

struct Request {
    std::vector<std::pair<std::string, std::string>> rawHeaders;
    mdp::Topic                                       topic;
    RestMethod                                       method = RestMethod::Invalid;
    std::string                                      longPollIndex;
    std::string                                      contentType;
    std::string                                      accept;
    std::string      payload;
    bool             complete = false;

    std::string_view acceptedMime() const {
        static constexpr auto acceptableMimeTypes = std::array{
            opencmw::MIME::JSON.typeName(), MIME::HTML.typeName(), MIME::BINARY.typeName()
        };
        auto accepted = [](auto format) {
            const auto it = std::find(acceptableMimeTypes.begin(), acceptableMimeTypes.end(), format);
            return std::make_pair(
                    it != acceptableMimeTypes.cend(),
                    it);
        };

        if (!contentType.empty()) {
            if (const auto [found, where] = accepted(contentType); found) {
                return *where;
            }
        }
        if (auto it = topic.params().find("contentType"); it != topic.params().end()) {
            if (const auto [found, where] = accepted(it->second); found) {
                return *where;
            }
        }

        auto       isDelimiter = [](char c) { return c == ' ' || c == ','; };
        auto       from        = accept.cbegin();
        const auto end         = accept.cend();

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
};

struct ResponseData {
    explicit ResponseData(Message &&m)
        : message(std::move(m))
        , errorBuffer(message.error.data(), message.error.size()) {}

    explicit ResponseData(rest::Response &&r)
        : restResponse(std::move(r)) {}

    Message  message;
    IoBuffer errorBuffer;

    rest::Response restResponse;
};

struct IdGenerator {
    std::uint64_t _nextRequestId = 0;

    std::uint64_t generateId() {
        return _nextRequestId++;
    }
};

struct SubscriptionCacheEntry {
    constexpr static std::size_t kCapacity  = 100;
    std::uint64_t                firstIndex = 0;
    std::deque<Message>          messages;

    void                         add(Message &&message) {
        if (messages.size() == kCapacity) {
            messages.pop_front();
            firstIndex++;
        }
        messages.push_back(std::move(message));
    }
    std::uint64_t lastIndex() const noexcept {
        assert(!messages.empty());
        return firstIndex + messages.size() - 1;
    }
    std::uint64_t nextIndex() const noexcept {
        return firstIndex + messages.size();
    }
};

struct SharedData {
    std::string                                   _altSvcHeaderValue;
    nghttp2_nv                                    _altSvcHeader;
    std::map<std::string, SubscriptionCacheEntry> _subscriptionCache;
    std::vector<rest::Handler>                    _handlers;

    rest::Handler                                *findHandler(std::string_view method, std::string_view path) {
        auto bestMatch = _handlers.end();

        for (auto itHandler = _handlers.begin(); itHandler != _handlers.end(); ++itHandler) {
            if (itHandler->method != method) {
                continue;
            }

            std::string_view handlerPath = itHandler->path;

            if (handlerPath == path) {
                // exact match, use this handler
                return &*itHandler;
            }
            // if the handler path ends with '*', do a prefix check and use the most specific (longest) one
            if (handlerPath.ends_with("*")) {
                handlerPath.remove_suffix(1);
                if (path.starts_with(handlerPath) && (bestMatch == _handlers.end() || bestMatch->path.size() < itHandler->path.size())) {
                    bestMatch = itHandler;
                }
            }
        }

        return bestMatch != _handlers.end() ? &*bestMatch : nullptr;
    }
};

constexpr int kHttpOk       = 200;
constexpr int kHttpError    = 500;
constexpr int kFileNotFound = 404;

template<typename Derived, typename TStreamId>
struct SessionBase {
    using PendingRequest = std::tuple<std::uint64_t, TStreamId>;              // requestId, streamId
    using PendingPoll    = std::tuple<std::string, std::uint64_t, TStreamId>; // zmqTopic, PollingIndex, streamId
    std::map<TStreamId, Request>               _requestsByStreamId;
    std::map<TStreamId, ResponseData>          _responsesByStreamId;
    std::vector<PendingRequest>                _pendingRequests;
    std::vector<PendingPoll>                   _pendingPolls;
    std::shared_ptr<SharedData>                _sharedData;

    explicit SessionBase(std::shared_ptr<SharedData> sharedData)
        : _sharedData(std::move(sharedData)) {
    }

    SessionBase(const SessionBase &)                                   = delete;
    SessionBase &operator=(const SessionBase &)                        = delete;
    SessionBase(SessionBase &&other)                                   = delete;
    SessionBase                        &operator=(SessionBase &&other) = delete;

    [[nodiscard]] constexpr auto       &self() noexcept { return *static_cast<Derived *>(this); }
    [[nodiscard]] constexpr const auto &self() const noexcept { return *static_cast<const Derived *>(this); }

    std::optional<Message>              processGetSetRequest(TStreamId streamId, Request &request, IdGenerator &idGenerator) {
        Message result;
        request.topic.addParam("contentType", request.acceptedMime());
        result.command         = request.method == RestMethod::Get ? mdp::Command::Get : mdp::Command::Set;
        result.serviceName     = request.topic.service();
        result.topic           = request.topic.toMdpTopic();
        result.data            = IoBuffer(request.payload.data(), request.payload.size());
        auto id                = idGenerator.generateId();
        result.clientRequestID = IoBuffer(std::to_string(id).data(), std::to_string(id).size());
        _pendingRequests.emplace_back(id, streamId);
        return result;
    };

    void respondToLongPoll(TStreamId streamId, std::uint64_t index, Message &&msg) {
        auto timestamp = std::to_string(opencmw::load_test::timestamp().count());
        self().sendResponse(streamId, kHttpOk, std::move(msg), { nv(u8span("x-opencmw-long-polling-idx"), u8span(std::to_string(index))), nv(u8span("x-timestamp"), u8span(timestamp)) });
    }

    void respondToLongPollWithError(TStreamId streamId, std::string_view error, int code, std::uint64_t index) {
        Message response = {};
        response.error   = std::string(error);
        self().sendResponse(streamId, code, std::move(response), { nv(u8span("x-opencmw-long-polling-idx"), u8span(std::to_string(index))) });
    }

    void respondWithError(TStreamId streamId, std::string_view error, int code = kHttpError, std::vector<nghttp2_nv> extraHeaders = {}) {
        Message response = {};
        response.error   = std::string(error);
        self().sendResponse(streamId, code, std::move(response), std::move(extraHeaders));
    }

    void respondWithLongPollingRedirect(TStreamId streamId, const URI<> &topic, std::size_t longPollIdx) {
        auto location = URI<>::UriFactory(topic).addQueryParameter("LongPollingIdx", std::to_string(longPollIdx)).build();
        self().respondWithRedirect(streamId, location.str());
    }

    std::optional<Message> processLongPollRequest(TStreamId streamId, const Request &request) {
        std::optional<Message> result;
        const auto             zmqTopic = request.topic.toZmqTopic();
        auto                   entryIt  = _sharedData->_subscriptionCache.find(zmqTopic);
        if (entryIt == _sharedData->_subscriptionCache.end()) {
            entryIt         = _sharedData->_subscriptionCache.try_emplace(zmqTopic, SubscriptionCacheEntry{}).first;
            result          = Message{};
            result->command = mdp::Command::Subscribe;
            result->topic   = request.topic.toMdpTopic();
        }
        auto &entry = entryIt->second;
        if (request.longPollIndex == "Next") {
            respondWithLongPollingRedirect(streamId, request.topic.toMdpTopic(), entry.nextIndex());
            return result;
        } else if (request.longPollIndex == "Last") {
            const std::size_t last = entry.messages.empty() ? entry.nextIndex() : entry.lastIndex();
            respondWithLongPollingRedirect(streamId, request.topic.toMdpTopic(), last);
            return result;
        }

        std::uint64_t index = 0;
        if (auto [ptr, ec] = std::from_chars(request.longPollIndex.data(), request.longPollIndex.data() + request.longPollIndex.size(), index); ec != std::errc()) {
            respondWithError(streamId, fmt::format("Malformed LongPollingIdx '{}'", request.longPollIndex));
            return {};
        }

#ifdef OPENCMW_PROFILE_HTTP
        const std::size_t last = entry.messages.empty() ? entry.nextIndex() : entry.lastIndex();
        if (index < last) {
            fmt::println(std::cerr, "Server::LongPoll: index {} < last {}", index, last);
        }
#endif
        if (index < entry.firstIndex) {
            // index is too old, redirect to the next index
            HTTP_DBG("Server::LongPoll: index {} < firstIndex {}", index, entry.firstIndex);
            respondWithLongPollingRedirect(streamId, request.topic.toMdpTopic(), entry.nextIndex());
        } else if (entry.messages.empty() || index > entry.lastIndex()) {
            // future index, wait for new messages
            _pendingPolls.emplace_back(zmqTopic, index, streamId);
        } else {
            // we have a message for this index, send it
            respondToLongPoll(streamId, index - entry.firstIndex, Message(entry.messages[index - entry.firstIndex]));
        }
        return result;
    }

    void processCompletedRequest(TStreamId streamId) {
        auto it = _requestsByStreamId.find(streamId);
        assert(it != _requestsByStreamId.end());
        auto &[streamid, request] = *it;

        std::string      path;
        std::string_view method;
        std::string_view xOpencmwMethod;

        for (const auto &[name, value] : request.rawHeaders) {
            if (name == ":path") {
                path = value;
            } else if (name == ":method") {
                method = value;
            } else if (name == "content-type") {
                request.contentType = value;
            } else if (name == "accept") {
                request.accept = value;
            } else if (name == "x-opencmw-method") {
                xOpencmwMethod = value;
            }
        }

        // if we have an externally configured handler for this method/path, use it
        if (auto handler = _sharedData->findHandler(method, path); handler) {
            rest::Request req;
            req.method = method;
            req.path   = path;
            std::swap(req.headers, request.rawHeaders);
            auto response = handler->handler(req);
            self().sendResponse(streamId, std::move(response));
            _requestsByStreamId.erase(it);
            return;
        }

        // redirect "/" request to "/mmi.service"
        if (path == "/") {
            path = "/mmi.service";
        }

        // Everything else is a service request
        try {
            auto pathUri                 = URI<>(path);
            auto factory                 = URI<>::UriFactory(pathUri).setQuery({});
            bool haveSubscriptionContext = false;
            for (const auto &[qkey, qvalue] : pathUri.queryParamMap()) {
                if (qkey == "LongPollingIdx") {
                    request.method        = RestMethod::LongPoll;
                    request.longPollIndex = qvalue.value_or("");
                } else if (qkey == "SubscriptionContext") {
                    request.topic           = mdp::Topic::fromMdpTopic(URI<>(qvalue.value_or("")));
                    haveSubscriptionContext = true;
                } else if (qkey == "_bodyOverride") {
                    request.payload = qvalue.value_or("");
                } else {
                    if (qvalue) {
                        factory = std::move(factory).addQueryParameter(qkey, qvalue.value());
                    } else {
                        factory = std::move(factory).addQueryParameter(qkey);
                    }
                }
            }
            if (!haveSubscriptionContext) {
                request.topic = mdp::Topic::fromMdpTopic(factory.build());
            }
        } catch (const std::exception &e) {
            HTTP_DBG("Service::Header: Could not parse service URI '{}': {}", path, e.what());
            Message response;
            response.error = e.what();
            self().sendResponse(streamId, kFileNotFound, std::move(response));
            _requestsByStreamId.erase(it);
            return;
        }

        if (request.method == RestMethod::Invalid && !xOpencmwMethod.empty()) {
            request.method = parseMethod(xOpencmwMethod);
        }
        if (request.method == RestMethod::Invalid) {
            request.method = parseMethod(method);
        }

        // Set completed for getMessages() to collect
        request.complete = true;
    }

    std::vector<Message> getMessages(IdGenerator &idGenerator) {
        const auto           completeEnd = std::ranges::partition_point(_requestsByStreamId, [](const auto &pair) { return pair.second.complete; });

        std::vector<Message> result;
        result.reserve(static_cast<std::size_t>(std::distance(_requestsByStreamId.begin(), completeEnd)));

        for (auto it = _requestsByStreamId.begin(); it != completeEnd; ++it) {
            auto &[streamId, request] = *it;

            switch (request.method) {
            case RestMethod::Get:
            case RestMethod::Post:
                if (auto m = processGetSetRequest(streamId, request, idGenerator); m.has_value()) {
                    result.push_back(std::move(m.value()));
                }
                break;
            case RestMethod::LongPoll:
                if (auto m = processLongPollRequest(streamId, request); m.has_value()) {
                    result.push_back(std::move(m.value()));
                }
                break;
            case RestMethod::Invalid:
                respondWithError(it->first, "Invalid REST method", kHttpError);
                break;
            }
        }

        _requestsByStreamId.erase(_requestsByStreamId.begin(), completeEnd);
        return result;
    }

    void handleNotification(std::string_view zmqTopic, std::uint64_t index, const Message &msg) {
        auto pollIt = _pendingPolls.begin();
        while (pollIt != _pendingPolls.end()) {
            const auto &[pendingZmqTopic, pollIndex, streamId] = *pollIt;
            if (pendingZmqTopic == zmqTopic && index == pollIndex) {
                respondToLongPoll(streamId, pollIndex, Message(msg));
                pollIt = _pendingPolls.erase(pollIt);
            } else {
                ++pollIt;
            }
        }
    }
};

struct Http2Session : public SessionBase<Http2Session, std::int32_t> {
    using TStreamId = typename std::int32_t;
    TcpSocket         _socket;
    WriteBuffer<4096> _writeBuffer;
    nghttp2_session  *_session = nullptr;

    explicit Http2Session(TcpSocket &&socket, std::shared_ptr<SharedData> sharedData)
        : SessionBase<Http2Session, std::int32_t>(std::move(sharedData)), _socket{ std::move(socket) } {
        nghttp2_session_callbacks *callbacks;
        nghttp2_session_callbacks_new(&callbacks);
        nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks, [](nghttp2_session *, const nghttp2_frame *frame, void *user_data) {
            auto session = static_cast<Http2Session *>(user_data);
            return session->frame_recv_callback(frame);
        });
        nghttp2_session_callbacks_set_on_frame_send_callback(callbacks, [](nghttp2_session *, const nghttp2_frame *frame, void *user_data) {
            auto session = static_cast<Http2Session *>(user_data);
            return session->frame_send_callback(frame);
        });
        nghttp2_session_callbacks_set_on_frame_not_send_callback(callbacks, [](nghttp2_session *, const nghttp2_frame *frame, int lib_error_code, void *user_data) {
            auto session = static_cast<Http2Session *>(user_data);
            return session->frame_not_send_callback(frame, lib_error_code);
        });
        nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks, [](nghttp2_session *, uint8_t flags, int32_t stream_id, const uint8_t *data, size_t len, void *user_data) {
            auto session = static_cast<Http2Session *>(user_data);
            return session->data_chunk_recv_callback(flags, stream_id, { reinterpret_cast<const char *>(data), len });
        });
        nghttp2_session_callbacks_set_on_stream_close_callback(callbacks, [](nghttp2_session *, int32_t stream_id, uint32_t error_code, void *user_data) {
            auto session = static_cast<Http2Session *>(user_data);
            return session->stream_closed_callback(stream_id, error_code);
        });
        nghttp2_session_callbacks_set_on_header_callback2(callbacks, [](nghttp2_session *, const nghttp2_frame *frame, nghttp2_rcbuf *name,
                                                                             nghttp2_rcbuf *value, uint8_t flags, void *user_data) {
            auto session = static_cast<Http2Session *>(user_data);
            return session->header_callback(frame, as_view(name), as_view(value), flags);
        });
        nghttp2_session_callbacks_set_on_invalid_frame_recv_callback(callbacks, [](nghttp2_session *, const nghttp2_frame *frame, int lib_error_code, void *user_data) {
            auto session = static_cast<Http2Session *>(user_data);
            return session->invalid_frame_recv_callback(frame, lib_error_code);
        });
        nghttp2_session_callbacks_set_error_callback2(callbacks, [](nghttp2_session *, int lib_error_code, const char *msg, size_t len, void *user_data) {
            auto session = static_cast<Http2Session *>(user_data);
            return session->error_callback(lib_error_code, msg, len);
        });
        nghttp2_session_server_new(&_session, callbacks, this);
        nghttp2_session_callbacks_del(callbacks);
    }

    ~Http2Session() {
        nghttp2_session_del(_session);
    }

    Http2Session(const Http2Session &)            = delete;
    Http2Session &operator=(const Http2Session &) = delete;
    Http2Session(Http2Session &&other)            = delete;
    Http2Session &operator=(Http2Session &&other) = delete;

    bool          wantsToRead() const {
        return _socket._state == TcpSocket::Connected ? nghttp2_session_want_read(_session) : (_socket._state == TcpSocket::SSLAcceptWantsRead);
    }

    bool wantsToWrite() const {
        return _socket._state == TcpSocket::Connected ? _writeBuffer.wantsToWrite(_session) : (_socket._state == TcpSocket::SSLAcceptWantsWrite);
    }

    static auto ioBufferCallback() {
        return [](nghttp2_session *, int32_t /*stream_id*/, uint8_t *buf, size_t length, uint32_t *data_flags, nghttp2_data_source *source, void * /*user_data*/) {
            auto   ioBuffer = static_cast<IoBuffer *>(source->ptr);
            size_t copy_len = std::min(length, ioBuffer->size() - ioBuffer->position());
            std::copy(ioBuffer->data() + ioBuffer->position(), ioBuffer->data() + ioBuffer->position() + copy_len, buf);
            ioBuffer->skip(static_cast<int>(copy_len));
            if (ioBuffer->position() == ioBuffer->size()) {
                *data_flags |= NGHTTP2_DATA_FLAG_EOF;
            }
            return static_cast<ssize_t>(copy_len);
        };
    }

    void sendResponse(TStreamId streamId, rest::Response response) {
        // store message while sending so we don't need to copy the data
        auto                   &msg       = _responsesByStreamId.try_emplace(streamId, ResponseData{ std::move(response) }).first->second;

        constexpr auto          noCopy    = NGHTTP2_NV_FLAG_NO_COPY_NAME | NGHTTP2_NV_FLAG_NO_COPY_VALUE;
        const auto              statusStr = std::to_string(msg.restResponse.code);

        std::vector<nghttp2_nv> headers;
        headers.reserve(msg.restResponse.headers.size() + 2);
        // :status must go first, otherwise browsers and curl will not accept the response
        headers.push_back(nv(u8span(":status"), u8span(statusStr), NGHTTP2_NV_FLAG_NO_COPY_NAME));
        headers.push_back(nv(u8span("access-control-allow-origin"), u8span("*"), noCopy));
        headers.push_back(_sharedData->_altSvcHeader);

        for (const auto &[name, value] : msg.restResponse.headers) {
            headers.push_back(nv(u8span(name), u8span(value), noCopy));
        }

        nghttp2_data_provider2 data_prd;

        if (msg.restResponse.bodyReader) {
            data_prd.source.ptr    = &msg.restResponse;
            data_prd.read_callback = [](nghttp2_session *, int32_t stream_id, uint8_t *buf, size_t length, uint32_t *data_flags, nghttp2_data_source *source, void * /*user_data*/) -> ssize_t {
                std::ignore    = stream_id;
                auto       res = static_cast<rest::Response *>(source->ptr);
                const auto r   = res->bodyReader(std::span(buf, length));
                if (!r) {
                    HTTP_DBG("Server: stream_id={} Error reading body: {}", stream_id, r.error());
                    return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
                }
                const auto &[bytesRead, hasMore] = *r;
                if (!hasMore) {
                    *data_flags |= NGHTTP2_DATA_FLAG_EOF;
                }
                return static_cast<ssize_t>(bytesRead);
            };
        } else {
            data_prd.source.ptr    = &msg.restResponse.body;
            data_prd.read_callback = ioBufferCallback();
        }

#ifdef OPENCMW_DEBUG_HTTP
        auto formattedHeaders = headers | std::views::transform([](const auto &header) {
            return fmt::format("'{}'='{}'", std::string_view(reinterpret_cast<const char *>(header.name), header.namelen), std::string_view(reinterpret_cast<const char *>(header.value), header.valuelen));
        });
        HTTP_DBG("Sending response {} to streamId {}. Headers:\n{}\n Body: {}", msg.restResponse.code, streamId, fmt::join(formattedHeaders, "\n"), msg.restResponse.bodyReader ? "reader" : fmt::format("{} bytes", msg.restResponse.body.size()));
#endif
        if (auto rc = nghttp2_submit_response2(_session, streamId, headers.data(), headers.size(), &data_prd); rc != 0) {
            HTTP_DBG("Server: nghttp2_submit_response2 for stream ID {} failed: {}", streamId, nghttp2_strerror(rc));
            _responsesByStreamId.erase(streamId);
        }
    }

    void sendResponse(std::int32_t streamId, int responseCode, Message &&responseMessage, std::vector<nghttp2_nv> extraHeaders = {}) {
        // store message while sending so we don't need to copy the data
        auto         &msg           = _responsesByStreamId.try_emplace(streamId, ResponseData{ std::move(responseMessage) }).first->second;
        IoBuffer     *buf           = msg.errorBuffer.empty() ? &msg.message.data : &msg.errorBuffer;

        auto          codeStr       = std::to_string(responseCode);
        auto          contentLength = std::to_string(buf->size());
        constexpr int noCopy        = NGHTTP2_NV_FLAG_NO_COPY_NAME | NGHTTP2_NV_FLAG_NO_COPY_VALUE;
        // :status must go first
        auto headers = std::vector{ nv(u8span(":status"), u8span(codeStr)), nv(u8span("x-opencmw-topic"), u8span(msg.message.topic.str()), noCopy),
            nv(u8span("x-opencmw-service-name"), u8span(msg.message.serviceName), noCopy), nv(u8span("access-control-allow-origin"), u8span("*"), noCopy), nv(u8span("content-length"), u8span(contentLength)), _sharedData->_altSvcHeader };

        headers.insert(headers.end(), extraHeaders.begin(), extraHeaders.end());

        nghttp2_data_provider2 data_prd;
        data_prd.source.ptr    = buf;
        data_prd.read_callback = ioBufferCallback();

#ifdef OPENCMW_DEBUG_HTTP
        auto formattedHeaders = headers | std::views::transform([](const auto &header) {
            return fmt::format("'{}'='{}'", std::string_view(reinterpret_cast<const char *>(header.name), header.namelen), std::string_view(reinterpret_cast<const char *>(header.value), header.valuelen));
        });
        HTTP_DBG("Sending response {} to streamId {}. Headers:\n{}", responseCode, streamId, fmt::join(formattedHeaders, "\n"));
#endif
        if (auto rc = nghttp2_submit_response2(_session, streamId, headers.data(), headers.size(), &data_prd); rc != 0) {
            HTTP_DBG("Server: nghttp2_submit_response2 for stream ID {} failed: {}", streamId, nghttp2_strerror(rc));
            _responsesByStreamId.erase(streamId);
        }
    }

    void respondWithRedirect(TStreamId streamId, std::string_view location) {
        HTTP_DBG("Server::respondWithRedirect: streamId={} location={}", streamId, location);
        // :status must go first
        constexpr auto noCopy  = NGHTTP2_NV_FLAG_NO_COPY_NAME | NGHTTP2_NV_FLAG_NO_COPY_VALUE;
        const auto     headers = std::array{ nv(u8span(":status"), u8span("302"), noCopy), nv(u8span("location"), u8span(location)), _sharedData->_altSvcHeader };
        nghttp2_submit_response2(_session, streamId, headers.data(), headers.size(), nullptr);
    }

    void respondWithLongPollingRedirect(std::int32_t streamId, const URI<> &topic, std::size_t longPollIdx) {
        auto location = URI<>::UriFactory(topic).addQueryParameter("LongPollingIdx", std::to_string(longPollIdx)).build();
        respondWithRedirect(streamId, location.str());
    }

    std::optional<Message> processLongPollRequest(std::int32_t streamId, const Request &request) {
        std::optional<Message> result;
        const auto             zmqTopic = request.topic.toZmqTopic();
        auto                   entryIt  = _sharedData->_subscriptionCache.find(zmqTopic);
        if (entryIt == _sharedData->_subscriptionCache.end()) {
            entryIt         = _sharedData->_subscriptionCache.try_emplace(zmqTopic, SubscriptionCacheEntry{}).first;
            result          = Message{};
            result->command = mdp::Command::Subscribe;
            result->topic   = request.topic.toMdpTopic();
        }
        auto &entry = entryIt->second;
        if (request.longPollIndex == "Next") {
            respondWithLongPollingRedirect(streamId, request.topic.toMdpTopic(), entry.nextIndex());
            return result;
        } else if (request.longPollIndex == "Last") {
            const std::size_t last = entry.messages.empty() ? entry.nextIndex() : entry.lastIndex();
            respondWithLongPollingRedirect(streamId, request.topic.toMdpTopic(), last);
            return result;
        }

        std::uint64_t index = 0;
        if (auto [ptr, ec] = std::from_chars(request.longPollIndex.data(), request.longPollIndex.data() + request.longPollIndex.size(), index); ec != std::errc()) {
            respondWithError(streamId, fmt::format("Malformed LongPollingIdx '{}'", request.longPollIndex));
            return {};
        }

#ifdef OPENCMW_PROFILE_HTTP
        const std::size_t last = entry.messages.empty() ? entry.nextIndex() : entry.lastIndex();
        if (index + 5 < last) {
            fmt::println(std::cerr, "Server::LongPoll: index {} < last {} => {}", index, last, last - index);
        }
#endif
        if (index < entry.firstIndex) {
            // index is too old, redirect to the next index
            HTTP_DBG("Server::LongPoll: index {} < firstIndex {}", index, entry.firstIndex);
            respondWithLongPollingRedirect(streamId, request.topic.toMdpTopic(), entry.nextIndex());
        } else if (entry.messages.empty() || index > entry.lastIndex()) {
            // future index, wait for new messages
            _pendingPolls.emplace_back(zmqTopic, index, streamId);
        } else {
            // we have a message for this index, send it
            respondToLongPoll(streamId, index - entry.firstIndex, Message(entry.messages[index - entry.firstIndex]));
        }
        return result;
    }

    void processCompletedRequest(std::int32_t streamId) {
        auto it = _requestsByStreamId.find(streamId);
        assert(it != _requestsByStreamId.end());
        auto &[streamid, request] = *it;

        std::string      path;
        std::string_view method;
        std::string_view xOpencmwMethod;

        for (const auto &[name, value] : request.rawHeaders) {
            if (name == ":path") {
                path = value;
            } else if (name == ":method") {
                method = value;
            } else if (name == "content-type") {
                request.contentType = value;
            } else if (name == "accept") {
                request.accept = value;
            } else if (name == "x-opencmw-method") {
                xOpencmwMethod = value;
            }
        }

        // if we have an externally configured handler for this method/path, use it
        if (auto handler = _sharedData->findHandler(method, path); handler) {
            rest::Request req;
            req.method = method;
            req.path   = path;
            std::swap(req.headers, request.rawHeaders);
            auto response = handler->handler(req);
            sendResponse(streamId, std::move(response));
            _requestsByStreamId.erase(it);
            return;
        }

        // redirect "/" request to "/mmi.service"
        if (path == "/") {
            path = "/mmi.service";
        }

        // Everything else is a service request
        try {
            auto pathUri                 = URI<>(path);
            auto factory                 = URI<>::UriFactory(pathUri).setQuery({});
            bool haveSubscriptionContext = false;
            for (const auto &[qkey, qvalue] : pathUri.queryParamMap()) {
                if (qkey == "LongPollingIdx") {
                    request.method        = RestMethod::LongPoll;
                    request.longPollIndex = qvalue.value_or("");
                } else if (qkey == "SubscriptionContext") {
                    request.topic           = mdp::Topic::fromMdpTopic(URI<>(qvalue.value_or("")));
                    haveSubscriptionContext = true;
                } else if (qkey == "_bodyOverride") {
                    request.payload = qvalue.value_or("");
                } else {
                    if (qvalue) {
                        factory = std::move(factory).addQueryParameter(qkey, qvalue.value());
                    } else {
                        factory = std::move(factory).addQueryParameter(qkey);
                    }
                }
            }
            if (!haveSubscriptionContext) {
                request.topic = mdp::Topic::fromMdpTopic(factory.build());
            }
        } catch (const std::exception &e) {
            HTTP_DBG("Service::Header: Could not parse service URI '{}': {}", path, e.what());
            Message response;
            response.error = e.what();
            sendResponse(streamId, kFileNotFound, std::move(response));
            _requestsByStreamId.erase(it);
            return;
        }

        if (request.method == RestMethod::Invalid && !xOpencmwMethod.empty()) {
            request.method = parseMethod(xOpencmwMethod);
        }
        if (request.method == RestMethod::Invalid) {
            request.method = parseMethod(method);
        }

        // Set completed for getMessages() to collect
        request.complete = true;
    }

    std::vector<Message> getMessages(IdGenerator &idGenerator) {
        const auto           completeEnd = std::ranges::partition_point(_requestsByStreamId, [](const auto &pair) { return pair.second.complete; });

        std::vector<Message> result;
        result.reserve(static_cast<std::size_t>(std::distance(_requestsByStreamId.begin(), completeEnd)));

        for (auto it = _requestsByStreamId.begin(); it != completeEnd; ++it) {
            auto &[streamId, request] = *it;

            switch (request.method) {
            case RestMethod::Get:
            case RestMethod::Post:
                if (auto m = processGetSetRequest(streamId, request, idGenerator); m.has_value()) {
                    result.push_back(std::move(m.value()));
                }
                break;
            case RestMethod::LongPoll:
                if (auto m = processLongPollRequest(streamId, request); m.has_value()) {
                    result.push_back(std::move(m.value()));
                }
                break;
            case RestMethod::Invalid:
                respondWithError(it->first, "Invalid REST method", kHttpError);
                break;
            }
        }

        _requestsByStreamId.erase(_requestsByStreamId.begin(), completeEnd);
        return result;
    }

    int frame_recv_callback(const nghttp2_frame *frame) {
        HTTP_DBG("Server::Frame: id={} {} {} {}", frame->hd.stream_id, frame->hd.type, frame->hd.flags, (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) ? "END_STREAM" : "");
        switch (frame->hd.type) {
        case NGHTTP2_DATA:
        case NGHTTP2_HEADERS:
            if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
                processCompletedRequest(frame->hd.stream_id);
            }
            break;
        }
        return 0;
    }

    int frame_send_callback(const nghttp2_frame *frame) {
        HTTP_DBG("Server::Frame sent: id={} {} {} {}", frame->hd.stream_id, frame->hd.type, frame->hd.flags, (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) ? "END_STREAM" : "");
        if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
            _responsesByStreamId.erase(frame->hd.stream_id);
        }
        return 0;
    }

    int frame_not_send_callback(const nghttp2_frame *frame, int lib_error_code) {
        std::ignore = lib_error_code;
        HTTP_DBG("Server::Frame not sent: id={} {} {} {}", frame->hd.stream_id, frame->hd.type, frame->hd.flags, (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) ? "END_STREAM" : "");
        if (frame->hd.type == NGHTTP2_DATA) {
            _responsesByStreamId.erase(frame->hd.stream_id);
        }
        return 0;
    }

    int data_chunk_recv_callback(uint8_t /*flags*/, int32_t stream_id, std::string_view data) {
        HTTP_DBG("Server::Data id={} {} bytes", stream_id, data.size());
        _requestsByStreamId[stream_id].payload += data;
        return 0;
    }

    int stream_closed_callback(int32_t stream_id, uint32_t error_code) {
        std::ignore = error_code;
        HTTP_DBG("Server::Stream closed: {} ({})", stream_id, error_code);
        const std::size_t erased = _responsesByStreamId.erase(stream_id);
        // if this was canceled by the client, remove any pending requests/polls
        if (erased > 0) {
            std::erase_if(_pendingRequests, [stream_id](const auto &request) { return std::get<1>(request) == stream_id; });
            std::erase_if(_pendingPolls, [stream_id](const auto &poll) { return std::get<2>(poll) == stream_id; });
        }
        return 0;
    }

    int header_callback(const nghttp2_frame *frame, std::string_view name, std::string_view value, uint8_t /*flags*/) {
        HTTP_DBG("Server::Header id={} {} = {}", frame->hd.stream_id, name, value);
        const auto [it, inserted] = _requestsByStreamId.try_emplace(frame->hd.stream_id, Request{});
        auto &request             = it->second;
        request.rawHeaders.emplace_back(name, value);
#ifdef OPENCMW_PROFILE_HTTP
        if (name == "x-timestamp") {
            fmt::println(std::cerr, "Server::Header: x-timestamp: {} (latency {} ns)", value, opencmw::detail::nghttp2::latency(value).count());
        }
#endif
        return 0;
    }

    int invalid_frame_recv_callback(const nghttp2_frame *, int lib_error_code) {
        std::ignore = lib_error_code;
        HTTP_DBG("invalid_frame_recv_callback called error={}", lib_error_code);
        return 0;
    }

    int error_callback(int lib_error_code, const char *msg, size_t len) {
        std::ignore = lib_error_code;
        std::ignore = msg;
        std::ignore = len;
        HTTP_DBG("Server::ERROR: {} ({})", std::string_view(msg, len), lib_error_code);
        return 0;
    }
};

struct Http3Session : public SessionBase<Http3Session, std::int64_t> {
    nghttp3_conn *_httpconn = nullptr;
    TcpSocket _socket;

    explicit Http3Session(TcpSocket &&socket, std::shared_ptr<SharedData> sharedData)
        : SessionBase<Http3Session, std::int64_t>(std::move(sharedData)), _socket{ std::move(socket) } {
        nghttp3_callbacks callbacks;
        callbacks.acked_stream_data = [](nghttp3_conn *, std::int64_t stream_id, std::uint64_t datalen, void *conn_user_data, void *) {
            auto session = static_cast<Http3Session *>(conn_user_data);
            return session->acked_stream_data(stream_id, datalen);
        };
        callbacks.stream_close = [](nghttp3_conn *, std::int64_t stream_id, std::uint64_t error_code, void *conn_user_data, void *) {
            auto session = static_cast<Http3Session *>(conn_user_data);
            return session->stream_close(stream_id, error_code);
        };
        callbacks.recv_data = [](nghttp3_conn *, std::int64_t stream_id, const uint8_t *data, std::size_t datalen, void *conn_user_data, void * /*stream_user_data*/) {
            auto session   = static_cast<Http3Session *>(conn_user_data);
            auto data_view = std::string_view(reinterpret_cast<const char *>(data), datalen);
            return session->recv_data(stream_id, data_view);
        };
        callbacks.deferred_consume = [](nghttp3_conn *, std::int64_t stream_id, std::size_t datalen, void *conn_user_data, void *) {
            auto session = static_cast<Http3Session *>(conn_user_data);
            return session->deferred_consume(stream_id, datalen);
        };
        callbacks.begin_headers = [](nghttp3_conn *, std::int64_t stream_id, void *conn_user_data, void *) {
            auto session = static_cast<Http3Session *>(conn_user_data);
            return session->begin_headers(stream_id);
        };
        callbacks.recv_header = [](nghttp3_conn *, std::int64_t stream_id, std::int32_t token, nghttp3_rcbuf *name, nghttp3_rcbuf *value, uint8_t flags, void *conn_user_data, void *) {
            auto session = static_cast<Http3Session *>(conn_user_data);
            return session->recv_header(stream_id, token, name, value, flags);
        };
        callbacks.end_headers = [](nghttp3_conn *, std::int64_t stream_id, int fin, void *conn_user_data, void *) {
            auto session = static_cast<Http3Session *>(conn_user_data);
            return session->end_headers(stream_id, fin);
        };
        callbacks.stop_sending = [](nghttp3_conn *, std::int64_t stream_id, std::uint64_t app_error_code, void *conn_user_data, void *) {
            auto session = static_cast<Http3Session *>(conn_user_data);
            return session->stop_sending(stream_id, app_error_code);
        };
        callbacks.end_stream = [](nghttp3_conn *, std::int64_t stream_id, void *conn_user_data, void *) {
            auto session = static_cast<Http3Session *>(conn_user_data);
            return session->end_stream(stream_id);
        };
        callbacks.reset_stream = [](nghttp3_conn *, std::int64_t stream_id, std::uint64_t error_code, void *conn_user_data, void *) {
            auto session = static_cast<Http3Session *>(conn_user_data);
            return session->reset_stream(stream_id, error_code);
        };
        callbacks.shutdown = [](nghttp3_conn *, std::int64_t id, void *conn_user_data) {
            auto session = static_cast<Http3Session *>(conn_user_data);
            return session->shutdown(id);
        };
        callbacks.recv_settings = [](nghttp3_conn *, const nghttp3_settings *settings, void *conn_user_data) {
            auto session = static_cast<Http3Session *>(conn_user_data);
            return session->recv_settings(settings);
        };

        nghttp3_settings settings;
        nghttp3_settings_default(&settings);

        auto mem = nghttp3_mem_default();
        // TODO: set settings
        if (nghttp3_conn_server_new(&_httpconn, &callbacks, &settings, mem, this) != 0) {
            // TODO use std::expected
            throw std::runtime_error("Failed to create nghttp3 connection");
        }
    }

    ~Http3Session() {
        nghttp3_conn_del(_httpconn);
    }

    Http3Session(const Http3Session &)
            = delete;
    Http3Session &operator=(const Http3Session &) = delete;
    Http3Session(Http3Session &&other)            = delete;
    Http3Session &operator=(Http3Session &&other) = delete;

    int           acked_stream_data(int64_t stream_id, size_t datalen) {
        HTTP_DBG("Server::H3::acked_stream_data: stream_id={} datalen={}", stream_id, datalen);
        return 0;
    }

    int stream_close(std::int64_t stream_id, std::uint64_t error_code) {
        HTTP_DBG("Server::H3::stream_close: stream_id={} error_code={}", stream_id, error_code);
        return 0;
    }

    int recv_data(std::int64_t stream_id, std::string_view data) {
        HTTP_DBG("Server::H3::recv_data: stream_id={} datalen={}", stream_id, data.size());
        return 0;
    }

    int deferred_consume(std::int64_t stream_id, std::size_t datalen) {
        HTTP_DBG("Server::H3::deferred_consume: stream_id={} datalen={}", stream_id, datalen);
        return 0;
    }

    int begin_headers(std::int64_t stream_id) {
        HTTP_DBG("Server::H3::begin_headers: stream_id={}", stream_id);
        return 0;
    }

    int recv_header(std::int64_t stream_id, std::int32_t token, const nghttp3_rcbuf *name, const nghttp3_rcbuf *value, std::uint8_t flags) {
        HTTP_DBG("Server::H3::recv_header: stream_id={} token={} name={} value={} flags={}", stream_id, token, as_view(name), as_view(value), flags);
        return 0;
    }

    int end_headers(std::int64_t stream_id, int fin) {
        HTTP_DBG("Server::H3::end_headers: stream_id={} fin={}", stream_id, fin);
        return 0;
    }

    int stop_sending(std::int64_t stream_id, std::uint64_t error_code) {
        HTTP_DBG("Server::H3::stop_sending: stream_id={} error_code={}", stream_id, error_code);
        return 0;
    }

    int end_stream(std::int64_t stream_id) {
        HTTP_DBG("Server::H3::end_stream: stream_id={}", stream_id);
        return 0;
    }
    int reset_stream(std::int64_t stream_id, std::uint64_t error_code) {
        HTTP_DBG("Server::H3::reset_stream: stream_id={} error_code={}", stream_id, error_code);
        return 0;
    }

    int recv_settings(const nghttp3_settings *) {
        HTTP_DBG("Server::H3::recv_settings");
        return 0;
    }

    int shutdown(std::int64_t id) {
        HTTP_DBG("Server::H3::shutdown: id={}", id);
        return 0;
    }
};

struct Http3ServerSocket {
    int fd              = -1;
    opencmw::nghttp3::Address addr;

    Http3ServerSocket() = default;

    Http3ServerSocket(int fd_, opencmw::nghttp3::Address addr_)
        : fd(fd_), addr(std::move(addr_)) {
    }

    Http3ServerSocket(const Http3ServerSocket &)            = delete;
    Http3ServerSocket &operator=(const Http3ServerSocket &) = delete;

    Http3ServerSocket(Http3ServerSocket &&other) noexcept
        : fd(std::exchange(other.fd, -1)), addr(other.addr) {}

    Http3ServerSocket &operator=(Http3ServerSocket &&other) noexcept {
        if (this != &other) {
            close();
            std::swap(fd, other.fd);
            addr = other.addr;
        }
        return *this;
    }

    void close() {
        if (fd != -1) {
            ::close(fd);
            fd = -1;
        }
    }

    static std::expected<Http3ServerSocket, std::string> create(uint16_t port) {
        using namespace opencmw::nghttp3;
        Address address;
        // TODO(Frank) refactor create_sock so we get error details
        auto              fd = opencmw::nghttp3::create_sock(address, "*", std::to_string(port).c_str(), AF_INET);
        Http3ServerSocket socket{ fd, address };
        return socket;
    }

    ~Http3ServerSocket() {
        close();
    }
};

inline std::expected<TcpSocket, std::string> createTcpServerSocket(SSL_CTX *ssl_ctx, uint16_t port) {
    auto ssl = SSL_Ptr(nullptr, SSL_free);
    if (ssl_ctx) {
        auto maybeSsl = create_ssl(ssl_ctx);
        if (!maybeSsl) {
            return std::unexpected(fmt::format("Failed to set up TCP server socket: {}", maybeSsl.error()));
        }
        ssl = std::move(maybeSsl.value());
    }

    auto serverSocket = TcpSocket::create(std::move(ssl), socket(AF_INET, SOCK_STREAM, 0));
    if (!serverSocket) {
        return std::unexpected(serverSocket.error());
    }

    int reuseFlag = 1;
    if (setsockopt(serverSocket->fd, SOL_SOCKET, SO_REUSEADDR, &reuseFlag, sizeof(reuseFlag)) < 0) {
        return std::unexpected(fmt::format("setsockopt(SO_REUSEADDR) failed: {}", strerror(errno)));
    }

    sockaddr_in address;
    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port        = htons(port);
    memset(address.sin_zero, 0, sizeof(address.sin_zero));
    if (::bind(serverSocket->fd, reinterpret_cast<struct sockaddr *>(&address), sizeof(address)) < 0) {
        return std::unexpected(fmt::format("Bind failed: {}", strerror(errno)));
    }

    if (listen(serverSocket->fd, 32) < 0) {
        return std::unexpected(fmt::format("Listen failed: {}", strerror(errno)));
    }

    return serverSocket;
}

struct Http2Server {
    TcpSocket                                    _tcpServerSocket;
    Http3ServerSocket                            _quicServerSocket;
    SSL_CTX_Ptr                                  _sslCtxTcp  = SSL_CTX_Ptr(nullptr, SSL_CTX_free);
    EVP_PKEY_Ptr                                 _key        = EVP_PKEY_Ptr(nullptr, EVP_PKEY_free);
    X509_Ptr                                     _cert       = X509_Ptr(nullptr, X509_free);
    std::shared_ptr<SharedData>                  _sharedData = std::make_shared<SharedData>();
    std::map<int, std::unique_ptr<Http2Session>> _sessions;
    IdGenerator                                  _requestIdGenerator;

    Http2Server()                               = default;
    Http2Server(const Http2Server &)            = delete;
    Http2Server &operator=(const Http2Server &) = delete;
    Http2Server(Http2Server &&)                 = default;
    Http2Server &operator=(Http2Server &&)      = default;

    Http2Server(SSL_CTX_Ptr sslCtxTcp, EVP_PKEY_Ptr key, X509_Ptr cert)
        : _sslCtxTcp(std::move(sslCtxTcp)), _key(std::move(key)), _cert(std::move(cert)) {
        if (_sslCtxTcp) {
            SSL_library_init();
            SSL_load_error_strings();
            OpenSSL_add_all_algorithms();
        }
    }

    static std::expected<Http2Server, std::string> unencrypted() {
        return Http2Server(SSL_CTX_Ptr(nullptr, SSL_CTX_free), EVP_PKEY_Ptr(nullptr, EVP_PKEY_free), X509_Ptr(nullptr, X509_free));
    }

    static std::expected<Http2Server, std::string> sslWithBuffers(std::string_view certBuffer, std::string_view keyBuffer) {
        auto maybeCert = nghttp2::readServerCertificateFromBuffer(certBuffer);
        if (!maybeCert) {
            return std::unexpected(maybeCert.error());
        }
        auto maybeKey = nghttp2::readServerPrivateKeyFromBuffer(keyBuffer);
        if (!maybeKey) {
            return std::unexpected(maybeKey.error());
        }
        auto maybeSslCtxTcp = create_ssl_ctx<ContextType::Tcp>(maybeKey->get(), maybeCert->get());
        if (!maybeSslCtxTcp) {
            return std::unexpected(maybeSslCtxTcp.error());
        }
        return Http2Server(std::move(maybeSslCtxTcp.value()), std::move(maybeKey.value()), std::move(maybeCert.value()));
    }

    static std::expected<Http2Server, std::string> sslWithPaths(std::filesystem::path certPath, std::filesystem::path keyPath) {
        auto maybeCert = nghttp2::readServerCertificateFromFile(certPath);
        if (!maybeCert) {
            return std::unexpected(maybeCert.error());
        }
        auto maybeKey = nghttp2::readServerPrivateKeyFromFile(keyPath);
        if (!maybeKey) {
            return std::unexpected(maybeKey.error());
        }
        auto maybeSslCtxTcp = create_ssl_ctx<ContextType::Tcp>(maybeKey->get(), maybeCert->get());
        if (!maybeSslCtxTcp) {
            return std::unexpected(maybeSslCtxTcp.error());
        }

        return Http2Server(std::move(maybeSslCtxTcp.value()), std::move(maybeKey.value()), std::move(maybeCert.value()));
    }

    void setHandlers(std::vector<opencmw::majordomo::rest::Handler> handlers) {
        _sharedData->_handlers = std::move(handlers);
    }

    void handleResponse(Message &&message) {
        auto          view = message.clientRequestID.asString();
        std::uint64_t id;
        const auto    ec = std::from_chars(view.begin(), view.end(), id);
        if (ec.ec != std::errc{}) {
            HTTP_DBG("Failed to parse request ID: '{}'", view);
            return;
        }
        auto matchesId = [id](const auto &pendingRequest) { return std::get<0>(pendingRequest) == id; };

        auto it        = std::ranges::find_if(_sessions, [matchesId](const auto &session) {
            return std::ranges::find_if(session.second->_pendingRequests, matchesId) != session.second->_pendingRequests.end();
        });

        if (it != _sessions.end()) {
            auto &session                 = it->second;
            auto  pendingIt               = std::ranges::find_if(session->_pendingRequests, matchesId);
            const auto &[reqId, streamId] = *pendingIt;
            const auto code               = message.error.empty() ? kHttpOk : kHttpError;
            session->sendResponse(streamId, code, std::move(message));
            session->_pendingRequests.erase(pendingIt);
        };
    }

    void handleNotification(const mdp::Topic &topic, Message &&message) {
        const auto zmqTopic = topic.toZmqTopic();
        auto       entryIt  = _sharedData->_subscriptionCache.find(zmqTopic);
        if (entryIt == _sharedData->_subscriptionCache.end()) {
            HTTP_DBG("Server::handleNotification: No subscription for topic '{}'", zmqTopic);
            return;
        }
        auto &entry = entryIt->second;
        entry.add(std::move(message));
        const auto index = entry.lastIndex() - entry.firstIndex;

        for (auto &session : _sessions | std::views::values) {
            session->handleNotification(zmqTopic, index, entry.messages.back());
        }
    }

    void populatePollerItems(std::vector<zmq_pollitem_t> &items) {
        items.push_back(zmq_pollitem_t{ nullptr, _tcpServerSocket.fd, ZMQ_POLLIN, 0 });
        items.push_back(zmq_pollitem_t{ nullptr, _quicServerSocket.fd, ZMQ_POLLIN, 0 });
        for (const auto &[_, session] : _sessions) {
            const auto wantsRead  = session->wantsToRead();
            const auto wantsWrite = session->wantsToWrite();
            if (wantsRead || wantsWrite) {
                items.emplace_back(nullptr, session->_socket.fd, static_cast<short>((wantsRead ? ZMQ_POLLIN : 0) | (wantsWrite ? ZMQ_POLLOUT : 0)), 0);
            }
        }
    }

    std::vector<Message> processReadWrite(int fd, bool read, bool write) {
        if (fd == _tcpServerSocket.fd) {
            auto maybeSocket = _tcpServerSocket.accept(_sslCtxTcp.get(), TcpSocket::None);
            if (!maybeSocket) {
                HTTP_DBG("Failed to accept client: {}", maybeSocket.error());
                return {};
            }

            auto clientSocket = std::move(maybeSocket.value());
            if (!clientSocket) {
                return {};
            }

            auto newFd                    = clientSocket->fd;

            auto [newSessionIt, inserted] = _sessions.try_emplace(newFd, std::make_unique<Http2Session>(std::move(clientSocket.value()), _sharedData));
            assert(inserted);
            auto                  &newSession = newSessionIt->second;
            nghttp2_settings_entry iv[1]      = { { NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 1000 } };
            if (nghttp2_submit_settings(newSession->_session, NGHTTP2_FLAG_NONE, iv, 1) != 0) {
                HTTP_DBG("nghttp2_submit_settings failed");
            }
            return {};
        }

        if (fd == _quicServerSocket.fd) {
            fmt::println(std::cerr, "Server::UDP: fd={} read={}", fd, read);
            on_read();
        }

        auto sessionIt = _sessions.find(fd);
        assert(sessionIt != _sessions.end());
        assert(sessionIt->second->_socket.fd == fd);
        auto &session = sessionIt->second;

        if (session->_socket._state != TcpSocket::Connected) {
            if (auto r = session->_socket.continueHandshake(); !r) {
                HTTP_DBG("Handshake failed: {}", r.error());
                _sessions.erase(sessionIt);
                return {};
            }
            return {};
        }

        if (write) {
            if (!session->_writeBuffer.write(session->_session, session->_socket)) {
                HTTP_DBG("Failed to write to peer");
                _sessions.erase(sessionIt);
                return {};
            }
        }

        if (!read) {
            return {};
        }

        bool mightHaveMore = true;

        while (mightHaveMore) {
            std::array<uint8_t, 1024> buffer;
            ssize_t                   bytes_read = session->_socket.read(buffer.data(), sizeof(buffer));
            if (bytes_read <= 0 && errno != EAGAIN) {
                if (bytes_read < 0) {
                    HTTP_DBG("Server::read failed: {} {}", bytes_read, session->_socket.lastError());
                }
                _sessions.erase(sessionIt);
                return {};
            }
            if (nghttp2_session_mem_recv2(session->_session, buffer.data(), static_cast<size_t>(bytes_read)) < 0) {
                HTTP_DBG("Server: nghttp2_session_mem_recv2 failed");
                _sessions.erase(sessionIt);
                return {};
            }
            mightHaveMore = bytes_read == static_cast<ssize_t>(buffer.size());
        }

        return session->getMessages(_requestIdGenerator);
    }

    std::expected<void, std::string>
    bind(std::uint16_t port) {
        if (_tcpServerSocket.fd != -1) {
            return std::unexpected("Server already bound");
        }

        _sharedData->_altSvcHeaderValue = fmt::format("h3=\"{}\"; ma=2592000", port + 1);
        _sharedData->_altSvcHeader      = nv(u8span("alt-svc"), u8span(_sharedData->_altSvcHeaderValue), NGHTTP2_NV_FLAG_NO_COPY_NAME | NGHTTP2_NV_FLAG_NO_COPY_VALUE);

        auto tcpSocket                  = createTcpServerSocket(_sslCtxTcp.get(), port);
        if (!tcpSocket) {
            return std::unexpected(tcpSocket.error());
        }
        _tcpServerSocket = std::move(tcpSocket.value());
        auto quicSocket  = Http3ServerSocket::create(port + 1);
        if (!quicSocket) {
            return std::unexpected(quicSocket.error());
        }
        _quicServerSocket = std::move(quicSocket.value());
        return {};
    }

    int on_read() {
        fmt::println(std::cerr, "Server::on_read");
        using namespace opencmw::nghttp3;
        sockaddr_union                 su;
        std::array<uint8_t, 64 * 1024> buf;
        size_t                         pktcnt = 0;
        ngtcp2_pkt_info                pi;

        iovec                          msg_iov{
                                     .iov_base = buf.data(),
                                     .iov_len  = buf.size(),
        };

        uint8_t msg_ctrl[CMSG_SPACE(sizeof(int)) + CMSG_SPACE(sizeof(in6_pktinfo)) + CMSG_SPACE(sizeof(int))];

        msghdr  msg{
             .msg_name    = &su,
             .msg_iov     = &msg_iov,
             .msg_iovlen  = 1,
             .msg_control = msg_ctrl,
        };

        for (; pktcnt < 10;) {
            msg.msg_namelen    = sizeof(su);
            msg.msg_controllen = sizeof(msg_ctrl);

            auto nread         = ::recvmsg(_quicServerSocket.fd, &msg, 0);
            if (nread == -1) {
                if (!(errno == EAGAIN || errno == ENOTCONN)) {
                    std::cerr << "recvmsg: " << strerror(errno) << std::endl;
                }
                return 0;
            }

            // Packets less than 21 bytes never be a valid QUIC packet.
            if (nread < 21) {
                ++pktcnt;

                continue;
            }
#if 0 // TODO(Frank) check if we need this
            if (util::prohibited_port(util::port(&su))) {
                ++pktcnt;

                continue;
            }
#endif
            pi.ecn          = msghdr_get_ecn(&msg, su.storage.ss_family);
            auto local_addr = msghdr_get_local_addr(&msg, su.storage.ss_family);
            if (!local_addr) {
                ++pktcnt;
                std::cerr << "Unable to obtain local address" << std::endl;
                continue;
            }

            auto gso_size = msghdr_get_udp_gro(&msg);
            if (gso_size == 0) {
                gso_size = static_cast<size_t>(nread);
            }

            set_port(*local_addr, _quicServerSocket.addr);

            auto data = std::span{ buf.data(), static_cast<size_t>(nread) };

            for (; !data.empty();) {
                auto datalen = std::min(data.size(), gso_size);

                ++pktcnt;

#if 0
#ifdef OPENCMW_DEBUG_HTTP
                std::array<char, IF_NAMESIZE> ifname;
                std::cerr << "Received packet: local="
                          << util::straddr(&local_addr->su.sa, local_addr->len)
                          << " remote=" << util::straddr(&su.sa, msg.msg_namelen)
                          << " if="
                          << if_indextoname(local_addr->ifindex, ifname.data())
                          << " ecn=0x" << std::hex << static_cast<uint32_t>(pi.ecn)
                          << std::dec << " " << datalen << " bytes" << std::endl;
#endif
#endif

                // Packets less than 21 bytes never be a valid QUIC packet.
                if (datalen < 21) {
                    break;
                }

                read_pkt(&su.sa, msg.msg_namelen, &pi,
                        { data.data(), datalen });

                data = data.subspan(datalen);
            }
        }

        return 0;
    }

    void read_pkt(const sockaddr *sa, socklen_t salen,
            const ngtcp2_pkt_info   *pi,
            std::span<const uint8_t> data) {
#if 0
        ngtcp2_version_cid vc;

        switch (auto rv = ngtcp2_pkt_decode_version_cid(&vc, data.data(), data.size(),
                        NGTCP2_SV_SCIDLEN);
                rv) {
        case 0:
            break;
        case NGTCP2_ERR_VERSION_NEGOTIATION:
            send_version_negotiation(vc.version, { vc.scid, vc.scidlen },
                    { vc.dcid, vc.dcidlen }, ep, local_addr, sa, salen);
            return;
        default:
            std::cerr << "Could not decode version and CID from QUIC packet header: "
                      << ngtcp2_strerror(rv) << std::endl;
            return;
        }

        auto dcid_key   = util::make_cid_key({ vc.dcid, vc.dcidlen });

        auto handler_it = handlers_.find(dcid_key);
        if (handler_it == std::end(handlers_)) {
            ngtcp2_pkt_hd hd;

            if (auto rv = ngtcp2_accept(&hd, data.data(), data.size()); rv != 0) {
                if (!config.quiet) {
                    std::cerr << "Unexpected packet received: length=" << data.size()
                              << std::endl;
                }

                if (!(data[0] & 0x80) && data.size() >= NGTCP2_SV_SCIDLEN + 21) {
                    send_stateless_reset(data.size(), { vc.dcid, vc.dcidlen }, ep, local_addr,
                            sa, salen);
                }

                return;
            }

            ngtcp2_cid        ocid;
            ngtcp2_cid       *pocid      = nullptr;
            ngtcp2_token_type token_type = NGTCP2_TOKEN_TYPE_UNKNOWN;

            assert(hd.type == NGTCP2_PKT_INITIAL);

            if (config.validate_addr || hd.tokenlen) {
                std::cerr << "Perform stateless address validation" << std::endl;
                if (hd.tokenlen == 0) {
                    send_retry(&hd, ep, local_addr, sa, salen, data.size() * 3);
                    return;
                }

                if (hd.token[0] != NGTCP2_CRYPTO_TOKEN_MAGIC_RETRY2 && hd.dcid.datalen < NGTCP2_MIN_INITIAL_DCIDLEN) {
                    send_stateless_connection_close(&hd, ep, local_addr, sa, salen);
                    return;
                }

                switch (hd.token[0]) {
                case NGTCP2_CRYPTO_TOKEN_MAGIC_RETRY2:
                    switch (verify_retry_token(&ocid, &hd, sa, salen)) {
                    case 0:
                        pocid      = &ocid;
                        token_type = NGTCP2_TOKEN_TYPE_RETRY;
                        break;
                    case -1:
                        send_stateless_connection_close(&hd, ep, local_addr, sa, salen);
                        return;
                    case 1:
                        hd.token    = nullptr;
                        hd.tokenlen = 0;
                        break;
                    }

                    break;
                case NGTCP2_CRYPTO_TOKEN_MAGIC_REGULAR:
                    if (verify_token(&hd, sa, salen) != 0) {
                        if (config.validate_addr) {
                            send_retry(&hd, ep, local_addr, sa, salen, data.size() * 3);
                            return;
                        }

                        hd.token    = nullptr;
                        hd.tokenlen = 0;
                    } else {
                        token_type = NGTCP2_TOKEN_TYPE_NEW_TOKEN;
                    }
                    break;
                default:
                    if (!config.quiet) {
                        std::cerr << "Ignore unrecognized token" << std::endl;
                    }
                    if (config.validate_addr) {
                        send_retry(&hd, ep, local_addr, sa, salen, data.size() * 3);
                        return;
                    }
                    hd.token    = nullptr;
                    hd.tokenlen = 0;
                    break;
                }
            }

            auto h = std::make_unique<Handler>(loop_, this);
            if (h->init(ep, local_addr, sa, salen, &hd.scid, &hd.dcid, pocid,
                        { hd.token, hd.tokenlen }, token_type, hd.version,
                        tls_ctx_)
                    != 0) {
                return;
            }

            switch (h->on_read(ep, local_addr, sa, salen, pi, data)) {
            case 0:
                break;
            case NETWORK_ERR_RETRY:
                send_retry(&hd, ep, local_addr, sa, salen, data.size() * 3);
                return;
            default:
                return;
            }

            if (h->on_write() != 0) {
                return;
            }

            std::array<ngtcp2_cid, 8> scids;
            auto                      conn     = h->conn();

            auto                      num_scid = ngtcp2_conn_get_scid(conn, nullptr);

            assert(num_scid <= scids.size());

            ngtcp2_conn_get_scid(conn, scids.data());

            for (size_t i = 0; i < num_scid; ++i) {
                associate_cid(&scids[i], h.get());
            }

            handlers_.emplace(dcid_key, h.release());

            return;
        }

        auto h    = (*handler_it).second;
        auto conn = h->conn();
        if (ngtcp2_conn_in_closing_period(conn)) {
            if (h->send_conn_close(ep, local_addr, sa, salen, pi, data) != 0) {
                remove(h);
            }
            return;
        }
        if (ngtcp2_conn_in_draining_period(conn)) {
            return;
        }

        if (auto rv = h->on_read(ep, local_addr, sa, salen, pi, data); rv != 0) {
            if (rv != NETWORK_ERR_CLOSE_WAIT) {
                remove(h);
            }
            return;
        }

        h->signal_write();
#endif
    }
};
} // namespace opencmw::majordomo::detail::nghttp2

#endif // OPENCMW_MAJORDOMO_HTTP2SERVER_HPP
