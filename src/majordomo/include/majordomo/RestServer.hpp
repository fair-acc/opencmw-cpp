#ifndef OPENCMW_MAJORDOMO_RESTSERVER_HPP
#define OPENCMW_MAJORDOMO_RESTSERVER_HPP

#include "IoBuffer.hpp"
#include "LoadTest.hpp"
#include "MdpMessage.hpp"
#include "MIME.hpp"
#include "NgTcp2Util.hpp"
#include "Rest.hpp"
#include "rest/RestUtils.hpp"
#include "TlsServerSession_Ossl.hpp"
#include "Topic.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <expected>
#include <filesystem>
#include <iterator>
#include <memory>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <optional>
#include <random>
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

#include <nghttp2/nghttp2.h>
#include <nghttp3/nghttp3.h>
#include <ngtcp2/ngtcp2.h>
#include <ngtcp2/ngtcp2_crypto.h>

namespace opencmw::majordomo::detail::rest {

#ifdef OPENCMW_DEBUG_HTTP
inline void print_debug2(const char *format, va_list args) {
    vfprintf(stderr, format, args);
}

inline void print_debug(void * /*user_data*/, const char *fmt, ...) {
    va_list                ap;
    std::array<char, 4096> buf;

    va_start(ap, fmt);
    auto n = vsnprintf(buf.data(), buf.size(), fmt, ap);
    va_end(ap);

    if (static_cast<size_t>(n) >= buf.size()) {
        n = buf.size() - 1;
    }

    buf[n++] = '\n';

    while (write(fileno(stderr), buf.data(), n) == -1 && errno == EINTR);
}
#endif

constexpr size_t NGTCP2_SV_SCIDLEN            = 18;

constexpr size_t max_preferred_versionslen    = 4;

constexpr size_t NGTCP2_STATELESS_RESET_BURST = 100;

// Endpoint is a local endpoint.
struct Endpoint {
    Address addr;
    int     fd;
};

struct Buffer {
    Buffer(const std::uint8_t *data, std::size_t datalen)
        : buf{ data, data + datalen }, begin(buf.data()), tail(begin + datalen) {}
    explicit Buffer(std::size_t datalen)
        : buf(datalen), begin(buf.data()), tail(begin) {}

    std::size_t                   size() const { return static_cast<std::size_t>(tail - begin); }
    std::size_t                   left() const { return static_cast<std::size_t>(buf.data() + buf.size() - tail); }
    std::uint8_t                 *wpos() { return tail; }
    std::span<const std::uint8_t> data() const { return { begin, size() }; }
    void                          push(std::size_t len) { tail += len; }
    void                          reset() { tail = begin; }

    std::vector<std::uint8_t>     buf;
    // begin points to the beginning of the buffer.  This might point to
    // buf.data() if a buffer space is allocated by this object.  It is
    // also allowed to point to the external shared buffer.
    std::uint8_t *begin;
    // tail points to the position of the buffer where write should
    // occur.
    std::uint8_t *tail;
};

inline std::expected<int, std::string> create_sock(Address &local_addr, std::string_view addr, std::string_view port, int family) {
    addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_flags    = AI_PASSIVE;
    hints.ai_family   = family;
    hints.ai_socktype = SOCK_DGRAM;

    addrinfo *res, *rp;
    int       val = 1;

    auto      paddr = addr == "*" ? nullptr : addr.data();

    if (auto rv = getaddrinfo(paddr, port.data(), &hints, &res); rv != 0) {
        return std::unexpected(std::format("getaddrinfo: {}", gai_strerror(rv)));
    }

    auto res_d = defer(freeaddrinfo, res);

    int  fd    = -1;

    for (rp = res; rp; rp = rp->ai_next) {
        fd = create_nonblock_socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd == -1) {
            continue;
        }

        if (rp->ai_family == AF_INET6) {
            if (setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &val, static_cast<socklen_t>(sizeof(val))) == -1) {
                ::close(fd);
                continue;
            }

            if (setsockopt(fd, IPPROTO_IPV6, IPV6_RECVPKTINFO, &val, static_cast<socklen_t>(sizeof(val))) == -1) {
                ::close(fd);
                continue;
            }
        } else if (setsockopt(fd, IPPROTO_IP, IP_PKTINFO, &val, static_cast<socklen_t>(sizeof(val))) == -1) {
            ::close(fd);
            continue;
        }

        if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, static_cast<socklen_t>(sizeof(val))) == -1) {
            ::close(fd);
            continue;
        }

        fd_set_recv_ecn(fd, rp->ai_family);
        fd_set_ip_mtu_discover(fd, rp->ai_family);
        fd_set_ip_dontfrag(fd, family);

        if (bind(fd, rp->ai_addr, rp->ai_addrlen) != -1) {
            break;
        }

        ::close(fd);
    }

    if (!rp) {
        return std::unexpected(std::format("Could not bind to address {}:{}", addr, port));
    }

    socklen_t len = sizeof(local_addr.su.storage);
    if (getsockname(fd, &local_addr.su.sa, &len) == -1) {
        ::close(fd);
        return std::unexpected(std::format("getsockname: {}", strerror(errno)));
    }
    local_addr.len     = len;
    local_addr.ifindex = 0;

    return fd;
}

inline uint32_t generate_reserved_version(const sockaddr *sa, socklen_t salen,
        uint32_t version) {
    uint32_t       h  = 0x811C9DC5u;
    const uint8_t *p  = reinterpret_cast<const uint8_t *>(sa);
    const uint8_t *ep = p + salen;
    for (; p != ep; ++p) {
        h ^= *p;
        h *= 0x01000193u;
    }
    version = htonl(version);
    p       = reinterpret_cast<const uint8_t *>(&version);
    ep      = p + sizeof(version);
    for (; p != ep; ++p) {
        h ^= *p;
        h *= 0x01000193u;
    }
    h &= 0xf0f0f0f0u;
    h |= 0x0a0a0a0au;
    return h;
}

using namespace opencmw::rest::detail;

inline int alpn_select_proto_cb(SSL * /*ssl*/, const unsigned char **out, unsigned char *outlen, const unsigned char *in, unsigned int inlen, void * /*arg*/) {
    static const unsigned char supported_alpns[] = { 2, 'h', '2', 2, 'h', '3' };

    HTTP_DBG("Client-supported ALPN protocols:");
    for (unsigned int i = 0; i < inlen;) {
        unsigned char    proto_len = in[i];
        std::string_view proto(reinterpret_cast<const char *>(&in[i + 1]), proto_len);
        HTTP_DBG("  - {}", proto);
        i += 1 + proto_len;
    }

    int ret = SSL_select_next_proto(const_cast<unsigned char **>(out), outlen, supported_alpns, sizeof(supported_alpns), in, inlen);

    HTTP_DBG("alpn_select_proto_cb: Selected ALPN: {}", ret == OPENSSL_NPN_NEGOTIATED ? std::string_view(reinterpret_cast<const char *>(*out), *outlen) : "none");
    if (ret != OPENSSL_NPN_NEGOTIATED) {
        return SSL_TLSEXT_ERR_NOACK;
    }

    return SSL_TLSEXT_ERR_OK;
}

inline std::expected<SSL_CTX_Ptr, std::string> create_ssl_ctx(EVP_PKEY *key, X509 *cert) {
    auto ssl_ctx = SSL_CTX_Ptr(SSL_CTX_new(TLS_server_method()), SSL_CTX_free);
    if (!ssl_ctx) {
        return std::unexpected(std::format("Could not create SSL/TLS context: {}", ERR_error_string(ERR_get_error(), nullptr)));
    }
    SSL_CTX_set_options(ssl_ctx.get(), SSL_OP_ALL | SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_COMPRESSION | SSL_OP_NO_SESSION_RESUMPTION_ON_RENEGOTIATION);
    if (SSL_CTX_set1_curves_list(ssl_ctx.get(), "P-256") != 1) {
        return std::unexpected(std::format("SSL_CTX_set1_curves_list failed: {}", ERR_error_string(ERR_get_error(), nullptr)));
    }

    if (SSL_CTX_use_PrivateKey(ssl_ctx.get(), key) <= 0) {
        return std::unexpected(std::format("Could not configure private key"));
    }
    if (SSL_CTX_use_certificate(ssl_ctx.get(), cert) != 1) {
        return std::unexpected(std::format("Could not configure certificate file"));
    }

    if (!SSL_CTX_check_private_key(ssl_ctx.get())) {
        return std::unexpected("Private key does not match the certificate");
    }

    SSL_CTX_set_alpn_select_cb(ssl_ctx.get(), alpn_select_proto_cb, nullptr);

    return ssl_ctx;
}

using Message = mdp::BasicMessage<mdp::MessageFormat::WithSourceId>;

enum class RestMethod {
    Options,
    Get,
    LongPoll,
    Post,
    Invalid
};

inline RestMethod parseMethod(std::string_view methodString) {
    using enum RestMethod;
    return methodString == "OPTIONS" ? Options
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
    std::string                                      payload;
    bool                                             complete = false;

    std::string_view                                 acceptedMime() const {
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

struct BodyReaderChunk {
    std::array<std::uint8_t, 16384> buffer;
    std::size_t                     unacked = 0;
    std::size_t                     offset  = 0;

    std::span<std::uint8_t>         data() {
        return { buffer.data() + offset, buffer.size() - offset };
    }

    void bytesAdded(std::size_t len) {
        unacked += len;
        offset += len;
    }

    void clear() {
        unacked = 0;
        offset  = 0;
    }

    bool full() const {
        return offset == buffer.size();
    }

    BodyReaderChunk()                                            = default;
    BodyReaderChunk &operator=(BodyReaderChunk &&other) noexcept = delete;
    BodyReaderChunk(BodyReaderChunk &&other) noexcept            = delete;
    BodyReaderChunk(const BodyReaderChunk &)                     = delete;
    BodyReaderChunk &operator=(const BodyReaderChunk &)          = delete;
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
    std::vector<majordomo::rest::Handler>         _handlers;
    std::deque<std::unique_ptr<BodyReaderChunk>>  _bodyReaderChunks;

    const std::array<uint8_t, 32>                 _static_secret = [] {
        std::array<uint8_t, 32> secret{};
        generate_secret(secret);
        return secret;
    }();

    majordomo::rest::Handler *findHandler(std::string_view method, std::string_view path) {
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

    std::unique_ptr<BodyReaderChunk> acquireChunk() {
        if (!_bodyReaderChunks.empty()) {
            auto chunk = std::move(_bodyReaderChunks.back());
            _bodyReaderChunks.pop_back();
            return chunk;
        }
        return std::make_unique<BodyReaderChunk>();
    }

    void releaseChunk(std::unique_ptr<BodyReaderChunk> chunk) {
        chunk->clear();
        _bodyReaderChunks.push_back(std::move(chunk));
    }
};

struct ResponseData {
    explicit ResponseData(std::shared_ptr<SharedData> sharedData_, Message &&m)
        : sharedData(std::move(sharedData_))
        , message(std::move(m))
        , errorBuffer(message.error.data(), message.error.size()) {}

    explicit ResponseData(std::shared_ptr<SharedData> sharedData_, majordomo::rest::Response &&r)
        : sharedData(std::move(sharedData_))
        , restResponse(std::move(r)) {}

    std::shared_ptr<SharedData> sharedData;
    Message                     message;
    IoBuffer                    errorBuffer;

    majordomo::rest::Response   restResponse;
    // Http3-specific (TODO: try to unify with Http2)
    // Used when streaming data from restResponse.bodyReader
    std::deque<std::unique_ptr<BodyReaderChunk>> bodyReaderChunks;
    // Used when streaming data from an IoBuffer
    IoBuffer *bodyBuffer = nullptr;
};

constexpr int kHttpOk       = 200;
constexpr int kHttpError    = 500;
constexpr int kFileNotFound = 404;

template<typename Derived, typename TStreamId>
struct SessionBase {
    using PendingRequest = std::tuple<std::uint64_t, TStreamId>;              // requestId, streamId
    using PendingPoll    = std::tuple<std::string, std::uint64_t, TStreamId>; // zmqTopic, PollingIndex, streamId
    std::map<TStreamId, Request>      _requestsByStreamId;
    std::map<TStreamId, ResponseData> _responsesByStreamId;
    std::vector<PendingRequest>       _pendingRequests;
    std::vector<PendingPoll>          _pendingPolls;
    std::shared_ptr<SharedData>       _sharedData;

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

    void addData(TStreamId streamId, std::string_view data) {
        _requestsByStreamId[streamId].payload += data;
    }

    void addHeader(TStreamId streamId, std::string_view name, std::string_view value) {
        const auto [it, inserted] = _requestsByStreamId.try_emplace(streamId, Request{});
        auto &request             = it->second;
        request.rawHeaders.emplace_back(name, value);
#ifdef OPENCMW_PROFILE_HTTP
        if (name == "x-timestamp") {
            std::println(std::cerr, "x-timestamp: {} (latency {} ns)", value, opencmw::rest::detail::latency(value).count());
        }
#endif
    }

    void respondToCorsPreflight(TStreamId streamId) {
        majordomo::rest::Response r;
        r.code = 204;
        r.headers.emplace_back("access-control-allow-methods", "GET, POST, OPTIONS");
        r.headers.emplace_back("access-control-allow-headers", "content-type, accept");
        r.headers.emplace_back("access-control-max-age", "86400");
        r.headers.emplace_back("content-length", "0");

        self().sendResponse(streamId, std::move(r));
    }

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
            respondWithError(streamId, std::format("Malformed LongPollingIdx '{}'", request.longPollIndex));
            return {};
        }

#ifdef OPENCMW_PROFILE_HTTP
        if (index % 100 == 0) {
            const std::size_t last = entry.messages.empty() ? entry.nextIndex() : entry.lastIndex();
            std::println(std::cerr, "{}: {}; delta: {}", zmqTopic, index, index <= last ? (last - index) : 0);
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
            respondToLongPoll(streamId, index, Message(entry.messages[index - entry.firstIndex]));
        }
        return result;
    }

    void processCompletedRequest(TStreamId streamId) {
        auto it = _requestsByStreamId.find(streamId);
        assert(it != _requestsByStreamId.end());
        auto &[streamid, request] = *it;

        std::string      path;
        std::string_view method;

        for (const auto &[name, value] : request.rawHeaders) {
            if (name == ":path") {
                path = value;
            } else if (name == ":method") {
                method = value;
            } else if (name == "content-type") {
                request.contentType = value;
            } else if (name == "accept") {
                request.accept = value;
            }
        }

        // if we have an externally configured handler for this method/path, use it
        if (auto handler = _sharedData->findHandler(method, path); handler) {
            majordomo::rest::Request req;
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
                    request.longPollIndex = qvalue.value_or("");
                } else if (qkey == "SubscriptionContext") {
                    request.topic           = mdp::Topic::fromMdpTopic(URI<>(qvalue.value_or("")));
                    haveSubscriptionContext = true;
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

        request.method = parseMethod(method);
        // Only GET + longPollIndex => LongPoll
        if (request.method == RestMethod::Get && !request.longPollIndex.empty()) {
            request.method = RestMethod::LongPoll;
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
            case RestMethod::Options:
               respondToCorsPreflight(streamId);
               break;
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

    static auto viewCallback() {
        return [](nghttp2_session *, int32_t /*stream_id*/, uint8_t *buf, size_t length, uint32_t *data_flags, nghttp2_data_source *source, void * /*user_data*/) {
            auto              view     = static_cast<std::span<const std::uint8_t> *>(source->ptr);
            const std::size_t copy_len = std::min(length, view->size());
            std::copy(view->data(), view->data() + copy_len, buf);
            *view = view->subspan(copy_len);
            if (view->empty()) {
                *data_flags |= NGHTTP2_DATA_FLAG_EOF;
            }
            return static_cast<ssize_t>(copy_len);
        };
    }

    void sendResponse(std::int32_t streamId, majordomo::rest::Response response) {
        // store message while sending so we don't need to copy the data
        auto                   &msg       = _responsesByStreamId.try_emplace(streamId, ResponseData{ _sharedData, std::move(response) }).first->second;

        constexpr auto          noCopy    = NGHTTP2_NV_FLAG_NO_COPY_NAME | NGHTTP2_NV_FLAG_NO_COPY_VALUE;
        const auto              statusStr = std::to_string(msg.restResponse.code);

        std::vector<nghttp2_nv> headers;
        headers.reserve(msg.restResponse.headers.size() + 2);
        // :status must go first, otherwise browsers and curl will not accept the response
        headers.push_back(nv(u8span(":status"), u8span(statusStr), NGHTTP2_NV_FLAG_NO_COPY_NAME));
        headers.push_back(nv(u8span("access-control-allow-origin"), u8span("*"), noCopy));
        if (!_sharedData->_altSvcHeaderValue.empty()) {
            headers.push_back(_sharedData->_altSvcHeader);
        }

        for (const auto &[name, value] : msg.restResponse.headers) {
            headers.push_back(nv(u8span(name), u8span(value), noCopy));
        }

        nghttp2_data_provider2 data_prd;
        data_prd.read_callback = nullptr;

        if (msg.restResponse.bodyReader) {
            data_prd.source.ptr    = &msg.restResponse;
            data_prd.read_callback = [](nghttp2_session *, int32_t stream_id, uint8_t *buf, size_t length, uint32_t *data_flags, nghttp2_data_source *source, void * /*user_data*/) -> ssize_t {
                std::ignore    = stream_id;
                auto       res = static_cast<majordomo::rest::Response *>(source->ptr);
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
        } else if (!msg.restResponse.bodyView.empty()) {
            data_prd.source.ptr    = &msg.restResponse.bodyView;
            data_prd.read_callback = viewCallback();
        } else if (!msg.restResponse.body.empty()) {
            data_prd.source.ptr    = &msg.restResponse.body;
            data_prd.read_callback = ioBufferCallback();
        }

#ifdef OPENCMW_DEBUG_HTTP
        auto formattedHeaders = headers | std::views::transform([](const auto &header) {
            return std::format("'{}'='{}'", std::string_view(reinterpret_cast<const char *>(header.name), header.namelen), std::string_view(reinterpret_cast<const char *>(header.value), header.valuelen));
        });
        HTTP_DBG("Server::H2: Sending response {} to streamId {}. Headers:\n{}\n Body: {}", msg.restResponse.code, streamId, opencmw::join(formattedHeaders, "\n"), msg.restResponse.bodyReader ? "reader" : std::format("{} bytes", msg.restResponse.body.size()));
#endif
        auto prd = data_prd.read_callback ? &data_prd : nullptr;
        if (auto rc = nghttp2_submit_response2(_session, streamId, headers.data(), headers.size(), prd); rc != 0) {
            HTTP_DBG("Server::H2: nghttp2_submit_response2 for stream ID {} failed: {}", streamId, nghttp2_strerror(rc));
            _responsesByStreamId.erase(streamId);
        }
    }

    void sendResponse(std::int32_t streamId, int responseCode, Message &&responseMessage, std::vector<nghttp2_nv> extraHeaders = {}) {
        // store message while sending so we don't need to copy the data
        auto         &msg           = _responsesByStreamId.try_emplace(streamId, ResponseData{ _sharedData, std::move(responseMessage) }).first->second;
        IoBuffer     *buf           = msg.errorBuffer.empty() ? &msg.message.data : &msg.errorBuffer;

        auto          codeStr       = std::to_string(responseCode);
        auto          contentLength = std::to_string(buf->size());
        constexpr int noCopy        = NGHTTP2_NV_FLAG_NO_COPY_NAME | NGHTTP2_NV_FLAG_NO_COPY_VALUE;
        // :status must go first
        auto headers = std::vector{ nv(u8span(":status"), u8span(codeStr)), nv(u8span("x-opencmw-topic"), u8span(msg.message.topic.str()), noCopy),
            nv(u8span("x-opencmw-service-name"), u8span(msg.message.serviceName), noCopy), nv(u8span("access-control-allow-origin"), u8span("*"), noCopy), nv(u8span("content-length"), u8span(contentLength)) };

        if (!_sharedData->_altSvcHeaderValue.empty()) {
            headers.push_back(_sharedData->_altSvcHeader);
        }
        headers.insert(headers.end(), extraHeaders.begin(), extraHeaders.end());

        nghttp2_data_provider2 data_prd;
        data_prd.source.ptr    = buf;
        data_prd.read_callback = ioBufferCallback();

#ifdef OPENCMW_DEBUG_HTTP
        auto formattedHeaders = headers | std::views::transform([](const auto &header) {
            return std::format("'{}'='{}'", std::string_view(reinterpret_cast<const char *>(header.name), header.namelen), std::string_view(reinterpret_cast<const char *>(header.value), header.valuelen));
        });
        HTTP_DBG("Server::H2: Sending response {} to streamId {}. Headers:\n{}", responseCode, streamId, opencmw::join(formattedHeaders, "\n"));
#endif
        if (auto rc = nghttp2_submit_response2(_session, streamId, headers.data(), headers.size(), &data_prd); rc != 0) {
            HTTP_DBG("Server::H2: nghttp2_submit_response2 for stream ID {} failed: {}", streamId, nghttp2_strerror(rc));
            _responsesByStreamId.erase(streamId);
        }
    }

    void respondWithRedirect(std::int32_t streamId, std::string_view location) {
        HTTP_DBG("Server::respondWithRedirect: streamId={} location={}", streamId, location);
        // :status must go first
        constexpr auto noCopy  = NGHTTP2_NV_FLAG_NO_COPY_NAME | NGHTTP2_NV_FLAG_NO_COPY_VALUE;
         auto           headers = std::vector{
             nv(u8span(":status"), u8span("302"), noCopy),
             nv(u8span("location"), u8span(location)),
             nv(u8span("access-control-allow-origin"), u8span("*"), noCopy)};

        if (!_sharedData->_altSvcHeaderValue.empty()) {
            headers.push_back(_sharedData->_altSvcHeader);
        }
        nghttp2_submit_response2(_session, streamId, headers.data(), headers.size(), nullptr);
    }

    void respondWithLongPollingRedirect(std::int32_t streamId, const URI<> &topic, std::size_t longPollIdx) {
        auto location = URI<>::UriFactory(topic).addQueryParameter("LongPollingIdx", std::to_string(longPollIdx)).build();
        respondWithRedirect(streamId, location.str());
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
        if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
            HTTP_DBG("Server::Frame sent: id={} {} {} END_STREAM", frame->hd.stream_id, frame->hd.type, frame->hd.flags);
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
        addData(stream_id, data);
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
        addHeader(frame->hd.stream_id, name, value);
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

template<typename TServer>
struct Http3Session : public SessionBase<Http3Session<TServer>, std::int64_t>, public std::enable_shared_from_this<Http3Session<TServer>> {
    enum class State {
        Initial,
        Closing,
        Draining,
    };
    State                  _state = State::Initial;
    TServer               *server_;
    ngtcp2_conn           *_conn = nullptr;
    ngtcp2_crypto_conn_ref _conn_ref;
    nghttp3_conn          *_httpconn = nullptr;
    ngtcp2_cid             _scid;
    TLSServerSession       _tlsSession;
    ngtcp2_ccerr           _last_error;
    // conn_closebuf_ contains a packet which contains CONNECTION_CLOSE.
    // This packet is repeatedly sent as a response to the incoming
    // packet in draining period.
    std::unique_ptr<Buffer> _conn_closebuf;
    bool                    _no_gso = false;
    struct {
        size_t bytes_recv;
        size_t bytes_sent;
        size_t num_pkts_recv;
        size_t next_pkts_recv = 1;
    } _close_wait;

    struct {
        bool   send_blocked     = false;
        size_t num_blocked      = 0;
        size_t num_blocked_sent = 0;
        // blocked field is effective only when send_blocked is true.
        struct {
            Endpoint                *endpoint = nullptr;
            Address                  local_addr;
            Address                  remote_addr;
            unsigned int             ecn = 0;
            std::span<const uint8_t> data;
            size_t                   gso_size = 0;
        } blocked[2];
        std::unique_ptr<uint8_t[]> data = std::unique_ptr<uint8_t[]>(new uint8_t[64 * 1024]);
    } _tx;

    static ngtcp2_conn *get_conn(ngtcp2_crypto_conn_ref *conn_ref) {
        auto session = static_cast<Http3Session *>(conn_ref->user_data);
        return session->_conn;
    }

    explicit Http3Session(TServer *server_, std::shared_ptr<SharedData> sharedData)
        : SessionBase<Http3Session, std::int64_t>(std::move(sharedData)), server_(server_), _conn_ref({ .get_conn = get_conn, .user_data = this }) {
    }

    ~Http3Session() {
        nghttp3_conn_del(_httpconn);
    }

    Http3Session(const Http3Session &)
            = delete;
    Http3Session &operator=(const Http3Session &) = delete;
    Http3Session(Http3Session &&other)            = delete;
    Http3Session &operator=(Http3Session &&other) = delete;

    static auto   viewCallback() {
        return [](nghttp3_conn *, int64_t stream_id, nghttp3_vec *vec, std::size_t /*veccnt*/, std::uint32_t *pflags, void   */*conn_user_data*/, void *stream_user_data) -> nghttp3_ssize {
            std::ignore    = stream_id;
            auto  res      = static_cast<ResponseData *>(stream_user_data);
            auto &bodyView = res->restResponse.bodyView;
            vec[0].base    = const_cast<std::uint8_t *>(bodyView.data());
            vec[0].len     = bodyView.size();
            HTTP_DBG("Server::H3::viewCallback: stream_id={} sz={} vec[0].len={}", stream_id, bodyView.size(), vec[0].len);
            *pflags = NGHTTP3_DATA_FLAG_EOF;
            return 1;
        };
    }

    static auto ioBufferCallback() {
        return [](nghttp3_conn *, int64_t stream_id, nghttp3_vec *vec, std::size_t /*veccnt*/, std::uint32_t *pflags, void * /*conn_user_data*/, void *stream_user_data) -> nghttp3_ssize {
            std::ignore = stream_id;
            auto res    = static_cast<ResponseData *>(stream_user_data);
            vec[0].base = res->bodyBuffer->data();
            vec[0].len  = res->bodyBuffer->size();
            HTTP_DBG("Server::H3::ioBufferCallback: stream_id={} sz={} vec[0].len={}", stream_id, res->bodyBuffer->size(), vec[0].len);
            *pflags = NGHTTP3_DATA_FLAG_EOF;
            return 1;
        };
    }

    void sendResponse(std::int64_t streamId, majordomo::rest::Response response) {
        // store message while sending so we don't need to copy the data
        auto &msg = this->_responsesByStreamId.try_emplace(streamId, ResponseData{ this->_sharedData, std::move(response) }).first->second;
        nghttp3_conn_set_stream_user_data(_httpconn, streamId, &msg);

        constexpr auto          noCopy    = NGHTTP3_NV_FLAG_NO_COPY_NAME | NGHTTP3_NV_FLAG_NO_COPY_VALUE;
        const auto              statusStr = std::to_string(msg.restResponse.code);

        std::vector<nghttp3_nv> headers;
        headers.reserve(msg.restResponse.headers.size() + 2);
        // :status must go first, otherwise browsers and curl will not accept the response
        headers.push_back(nv3(u8span(":status"), u8span(statusStr), NGHTTP3_NV_FLAG_NO_COPY_NAME));
        headers.push_back(nv3(u8span("access-control-allow-origin"), u8span("*"), noCopy));

        for (const auto &[name, value] : msg.restResponse.headers) {
            headers.push_back(nv3(u8span(name), u8span(value), noCopy));
        }

        nghttp3_data_reader data_prd;
        data_prd.read_data = nullptr;

        if (msg.restResponse.bodyReader) {
            data_prd.read_data = [](nghttp3_conn *, int64_t stream_id, nghttp3_vec *vec, std::size_t /*veccnt*/, std::uint32_t *pflags, void * /*conn_user_data*/, void *stream_user_data) -> nghttp3_ssize {
                std::ignore = stream_id;
                auto res    = static_cast<ResponseData *>(stream_user_data);

                // We need to cache the data from the reader until nghttp3 stream_data_acked confirms that the data was sent.
                // Use a chunk pool to avoid constant allocation/deallocation.
                if (res->bodyReaderChunks.empty() || res->bodyReaderChunks.back()->full()) {
                    res->bodyReaderChunks.push_back(res->sharedData->acquireChunk());
                }
                auto      &chunk = res->bodyReaderChunks.back();

                const auto r     = res->restResponse.bodyReader(chunk->data());
                if (!r) {
                    HTTP_DBG("Server::H3::bodyReaderCallback: stream_id={} Error reading body: {}", stream_id, r.error());
                    return NGHTTP3_ERR_CALLBACK_FAILURE;
                }
                const auto &[bytesRead, hasMore] = *r;
                HTTP_DBG("Server::H3::bodyReaderCallback: stream_id={} bytesRead={} hasMore={}", stream_id, bytesRead, hasMore);

                vec[0].base = &chunk->buffer[chunk->offset];
                vec[0].len  = bytesRead;
                chunk->bytesAdded(bytesRead);

                HTTP_DBG("Server::H3::bodyReaderCallback: stream_id={} chunks={} vec[0].len={}", stream_id, res->bodyReaderChunks.size(), vec[0].len);

                if (!hasMore) {
                    *pflags = NGHTTP3_DATA_FLAG_EOF;
                }
                return 1;
            };
        } else if (!msg.restResponse.bodyView.empty()) {
            data_prd.read_data = viewCallback();
        } else {
            msg.bodyBuffer     = &msg.restResponse.body;
            data_prd.read_data = ioBufferCallback();
        }

#ifdef OPENCMW_DEBUG_HTTP
        auto formattedHeaders = headers | std::views::transform([](const auto &header) {
            return std::format("'{}'='{}'", std::string_view(reinterpret_cast<const char *>(header.name), header.namelen), std::string_view(reinterpret_cast<const char *>(header.value), header.valuelen));
        });
        HTTP_DBG("Server::H3: Sending response {} to streamId {}. Headers:\n{}\n Body: {}", msg.restResponse.code, streamId, opencmw::join(formattedHeaders, "\n"), msg.restResponse.bodyReader ? "reader" : std::format("{} bytes", msg.restResponse.body.size()));
#endif
        auto prd = data_prd.read_data ? &data_prd : nullptr;
        if (auto rc = nghttp3_conn_submit_response(_httpconn, streamId, headers.data(), headers.size(), prd); rc != 0) {
            HTTP_DBG("Server::H3: nghttp3_conn_submit_response for stream ID {} failed: {}", streamId, nghttp3_strerror(rc));
            this->_responsesByStreamId.erase(streamId);
        }
    }

    void sendResponse(std::int64_t streamId, int responseCode, Message &&responseMessage, std::vector<nghttp2_nv> extraHeaders = {}) {
        // store message while sending so we don't need to copy the data
        auto &msg                            = this->_responsesByStreamId.try_emplace(streamId, ResponseData{ this->_sharedData, std::move(responseMessage) }).first->second;
        msg.bodyBuffer                       = msg.errorBuffer.empty() ? &msg.message.data : &msg.errorBuffer;

        auto                   codeStr       = std::to_string(responseCode);
        auto                   contentLength = std::to_string(msg.bodyBuffer->size());
        constexpr std::uint8_t noCopy        = NGHTTP3_NV_FLAG_NO_COPY_NAME | NGHTTP3_NV_FLAG_NO_COPY_VALUE;
        // :status must go first
        auto headers  = std::vector{ nv3(u8span(":status"), u8span(codeStr)), nv3(u8span("x-opencmw-topic"), u8span(msg.message.topic.str()), noCopy),
            nv3(u8span("x-opencmw-service-name"), u8span(msg.message.serviceName), noCopy), nv3(u8span("access-control-allow-origin"), u8span("*"), noCopy), nv3(u8span("content-length"), u8span(contentLength)) };

        auto nv2ToNv3 = [](const auto &nv) {
            auto mapFlags = [](uint8_t flags) {
                uint8_t r = NGHTTP2_NV_FLAG_NONE;
                if (flags & NGHTTP2_NV_FLAG_NO_COPY_NAME) {
                    r = NGHTTP3_NV_FLAG_NO_COPY_NAME;
                }
                if (flags & NGHTTP2_NV_FLAG_NO_COPY_VALUE) {
                    r |= NGHTTP3_NV_FLAG_NO_COPY_VALUE;
                }
                return r;
            };
            return nghttp3_nv{ nv.name, nv.value, nv.namelen, nv.valuelen, mapFlags(nv.flags) };
        };
        std::transform(extraHeaders.begin(), extraHeaders.end(), std::back_inserter(headers), nv2ToNv3);

        nghttp3_data_reader data_prd;
        nghttp3_conn_set_stream_user_data(_httpconn, streamId, &msg);
        data_prd.read_data = ioBufferCallback();

#ifdef OPENCMW_DEBUG_HTTP
        auto formattedHeaders = headers | std::views::transform([](const auto &header) {
            return std::format("'{}'='{}'", std::string_view(reinterpret_cast<const char *>(header.name), header.namelen), std::string_view(reinterpret_cast<const char *>(header.value), header.valuelen));
        });
        HTTP_DBG("Server::H3: Sending response {} to streamId {}. Headers:\n{}", responseCode, streamId, opencmw::join(formattedHeaders, "\n"));
#endif
        if (auto rc = nghttp3_conn_submit_response(_httpconn, streamId, headers.data(), headers.size(), &data_prd); rc != 0) {
            HTTP_DBG("Server::H3: nghttp3_conn_submit_response for stream ID {} failed: {}", streamId, nghttp2_strerror(rc));
            this->_responsesByStreamId.erase(streamId);
        }
    }

    void respondWithRedirect(std::int64_t streamId, std::string_view location) {
        HTTP_DBG("Server::H3::respondWithRedirect: streamId={} location={}", streamId, location);
        // :status must go first
        constexpr auto noCopy  = NGHTTP3_NV_FLAG_NO_COPY_NAME | NGHTTP3_NV_FLAG_NO_COPY_VALUE;
        const auto     headers = std::array{
            nv3(u8span(":status"), u8span("302"), noCopy),
            nv3(u8span("location"), u8span(location)),
            nv3(u8span("access-control-allow-origin"), u8span("*"), noCopy) };

        nghttp3_conn_submit_response(_httpconn, streamId, headers.data(), headers.size(), nullptr);
    }

    int init(const Endpoint &ep, const Address &local_addr, const sockaddr *sa, socklen_t salen, const ngtcp2_cid *dcid, const ngtcp2_cid *scid, const ngtcp2_cid *ocid, std::span<const std::uint8_t> token, ngtcp2_token_type token_type, std::uint32_t version, TLSServerContext &tls_ctx) {
        auto handshakeCompleted = [](ngtcp2_conn *, void *user_data) {
            auto session = static_cast<Http3Session *>(user_data);
            if (session->handshake_completed() != 0) {
                HTTP_DBG("Server::H3: handshake_completed failed");
                return NGTCP2_ERR_CALLBACK_FAILURE;
            }
            return 0;
        };

        auto recvStreamData = [](ngtcp2_conn *, std::uint32_t flags, std::int64_t stream_id, std::uint64_t /*offset*/, const std::uint8_t *data, std::size_t datalen, void *user_data, void * /*stream_user_data*/) {
            auto session = static_cast<Http3Session *>(user_data);
            if (session->recv_stream_data(flags, stream_id, { data, datalen }) != 0) {
                return NGTCP2_ERR_CALLBACK_FAILURE;
            }
            return 0;
        };

        auto ackedStreamDataOffset = [](ngtcp2_conn *, int64_t stream_id, uint64_t /*offset*/, uint64_t datalen, void *user_data, void * /*stream_user_data*/) {
            auto session = static_cast<Http3Session *>(user_data);
            if (session->acked_stream_data_offset(stream_id, datalen) != 0) {
                return NGTCP2_ERR_CALLBACK_FAILURE;
            }
            return 0;
        };

        auto streamOpen  = [](ngtcp2_conn *, int64_t /*stream_id*/, void  */*user_data*/) { return 0; };

        auto streamClose = [](ngtcp2_conn *, uint32_t flags, int64_t stream_id, uint64_t app_error_code, void *user_data, void * /*stream_user_data*/) {
            auto session = static_cast<Http3Session *>(user_data);

            if (!(flags & NGTCP2_STREAM_CLOSE_FLAG_APP_ERROR_CODE_SET)) {
                app_error_code = NGHTTP3_H3_NO_ERROR;
            }

            if (session->on_stream_close(stream_id, app_error_code) != 0) {
                return NGTCP2_ERR_CALLBACK_FAILURE;
            }
            return 0;
        };

        auto randCb = [](uint8_t *dest, size_t destlen, const ngtcp2_rand_ctx *) {
            auto rv = generate_secure_random({ dest, destlen });
            if (rv != 0) {
                assert(0);
                abort();
            }
        };

        auto getNewConnectionId = [](ngtcp2_conn *, ngtcp2_cid *cid, uint8_t *token, size_t cidlen, void *user_data) {
            if (generate_secure_random({ cid->data, cidlen }) != 0) {
                return NGTCP2_ERR_CALLBACK_FAILURE;
            }

            auto  session       = static_cast<Http3Session *>(user_data);
            auto &static_secret = session->_sharedData->_static_secret;

            cid->datalen        = cidlen;
            if (ngtcp2_crypto_generate_stateless_reset_token(token, static_secret.data(), static_secret.size(), cid) != 0) {
                return NGTCP2_ERR_CALLBACK_FAILURE;
            }
            session->server_->associate_cid(*cid, session->shared_from_this());

            return 0;
        };

        auto removeConnectionId = [](ngtcp2_conn *, const ngtcp2_cid *cid, void *user_data) {
            auto session = static_cast<Http3Session *>(user_data);
            session->server_->dissociate_cid(*cid);
            return 0;
        };

        auto updateKey = [](ngtcp2_conn *, uint8_t *rx_secret, uint8_t *tx_secret, ngtcp2_crypto_aead_ctx *rx_aead_ctx, uint8_t *rx_iv, ngtcp2_crypto_aead_ctx *tx_aead_ctx, uint8_t *tx_iv, const uint8_t *current_rx_secret, const uint8_t *current_tx_secret, size_t secretlen, void *user_data) {
            auto session = static_cast<Http3Session *>(user_data);
            if (session->update_key(rx_secret, tx_secret, rx_aead_ctx, rx_iv, tx_aead_ctx, tx_iv, current_rx_secret, current_tx_secret, secretlen) != 0) {
                return NGTCP2_ERR_CALLBACK_FAILURE;
            }
            return 0;
        };

        auto pathValidation = [](ngtcp2_conn *conn, uint32_t flags, const ngtcp2_path *path, const ngtcp2_path * /*old_path*/, ngtcp2_path_validation_result res, void *user_data) {
            if (res != NGTCP2_PATH_VALIDATION_RESULT_SUCCESS || !(flags & NGTCP2_PATH_VALIDATION_FLAG_NEW_TOKEN)) {
                return 0;
            }

            std::array<uint8_t, NGTCP2_CRYPTO_MAX_REGULAR_TOKENLEN> token;
            auto                                                    t             = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

            auto                                                    session       = static_cast<Http3Session *>(user_data);
            auto                                                   &static_secret = session->_sharedData->_static_secret;
            auto                                                    tokenlen      = ngtcp2_crypto_generate_regular_token(token.data(), static_secret.data(), static_secret.size(), path->remote.addr, path->remote.addrlen, static_cast<ngtcp2_tstamp>(t));
            if (tokenlen < 0) {
                HTTP_DBG("ngtcp2_crypto_generate_regular_token: {}", ngtcp2_strerror(static_cast<int>(tokenlen)));
                return 0;
            }

            if (auto rv = ngtcp2_conn_submit_new_token(conn, token.data(), static_cast<std::size_t>(tokenlen)); rv != 0) {
                HTTP_DBG("ngtcp2_conn_submit_new_token: {}", ngtcp2_strerror(rv));
                return NGTCP2_ERR_CALLBACK_FAILURE;
            }

            return 0;
        };

        auto streamReset = [](ngtcp2_conn *, int64_t stream_id, uint64_t /*final_size*/, uint64_t /*app_error_code*/, void *user_data, void * /*stream_user_data*/) {
            auto session = static_cast<Http3Session *>(user_data);
            if (session->on_stream_reset(stream_id) != 0) {
                return NGTCP2_ERR_CALLBACK_FAILURE;
            }
            return 0;
        };

        auto extendMaxRemoteStreamsBidi = [](ngtcp2_conn *, uint64_t max_streams, void *user_data) {
            auto session = static_cast<Http3Session *>(user_data);
            session->extend_max_remote_streams_bidi(max_streams);
            return 0;
        };

        auto extendMaxStreamData = [](ngtcp2_conn *, int64_t stream_id, uint64_t max_data, void *user_data, void * /*stream_user_data*/) {
            auto session = static_cast<Http3Session *>(user_data);
            if (session->extend_max_stream_data(stream_id, max_data) != 0) {
                return NGTCP2_ERR_CALLBACK_FAILURE;
            }
            return 0;
        };

        auto streamStopSending = [](ngtcp2_conn *, int64_t stream_id, uint64_t /*app_error_code*/, void *user_data, void * /*stream_user_data*/) {
            auto session = static_cast<Http3Session *>(user_data);
            if (session->on_stream_stop_sending(stream_id) != 0) {
                return NGTCP2_ERR_CALLBACK_FAILURE;
            }
            return 0;
        };

        auto recvTxKey = [](ngtcp2_conn *, ngtcp2_encryption_level level, void *user_data) {
            if (level != NGTCP2_ENCRYPTION_LEVEL_1RTT) {
                return 0;
            }
            auto session = static_cast<Http3Session *>(user_data);
            if (session->setup_httpconn() != 0) {
                return NGTCP2_ERR_CALLBACK_FAILURE;
            }
            return 0;
        };

        ngtcp2_callbacks callbacks;
        memset(&callbacks, 0, sizeof(callbacks));
        callbacks.client_initial                 = ngtcp2_crypto_client_initial_cb;
        callbacks.recv_client_initial            = ngtcp2_crypto_recv_client_initial_cb;
        callbacks.recv_crypto_data               = ngtcp2_crypto_recv_crypto_data_cb;
        callbacks.handshake_completed            = handshakeCompleted;
        callbacks.encrypt                        = ngtcp2_crypto_encrypt_cb;
        callbacks.decrypt                        = ngtcp2_crypto_decrypt_cb;
        callbacks.hp_mask                        = ngtcp2_crypto_hp_mask_cb;
        callbacks.recv_stream_data               = recvStreamData;
        callbacks.acked_stream_data_offset       = ackedStreamDataOffset;
        callbacks.stream_open                    = streamOpen;
        callbacks.stream_close                   = streamClose;
        callbacks.rand                           = randCb;
        callbacks.get_new_connection_id          = getNewConnectionId;
        callbacks.remove_connection_id           = removeConnectionId;
        callbacks.update_key                     = updateKey;
        callbacks.path_validation                = pathValidation;
        callbacks.stream_reset                   = streamReset;
        callbacks.extend_max_remote_streams_bidi = extendMaxRemoteStreamsBidi;
        callbacks.extend_max_stream_data         = extendMaxStreamData;
        callbacks.delete_crypto_aead_ctx         = ngtcp2_crypto_delete_crypto_aead_ctx_cb;
        callbacks.delete_crypto_cipher_ctx       = ngtcp2_crypto_delete_crypto_cipher_ctx_cb;
        callbacks.get_path_challenge_data        = ngtcp2_crypto_get_path_challenge_data_cb;
        callbacks.stream_stop_sending            = streamStopSending;
        callbacks.version_negotiation            = ngtcp2_crypto_version_negotiation_cb;
        callbacks.recv_tx_key                    = recvTxKey;

        _scid.datalen = NGTCP2_SV_SCIDLEN;
        if (generate_secure_random({ _scid.data, _scid.datalen }) != 0) {
            HTTP_DBG("Could not generate connection ID");
            return -1;
        }

        ngtcp2_settings settings;
        ngtcp2_settings_default(&settings);
        settings.token      = token.data();
        settings.tokenlen   = token.size();
        settings.token_type = token_type;
#ifdef OPENCMW_DEBUG_HTTP
        settings.log_printf = print_debug;
#endif
        ngtcp2_transport_params params;
        ngtcp2_transport_params_default(&params);
        params.initial_max_stream_data_bidi_local  = 65535;
        params.initial_max_stream_data_bidi_remote = 65535;
        params.initial_max_stream_data_uni         = 65535;
        params.initial_max_data                    = 128 * 1024;
        params.initial_max_streams_bidi            = 100;
        params.initial_max_streams_uni             = 3;
        params.max_idle_timeout                    = 3600 * NGTCP2_SECONDS;
        params.stateless_reset_token_present       = 0;
        params.active_connection_id_limit          = 8;

        if (ocid) {
            params.original_dcid      = *ocid;
            params.retry_scid         = *scid;
            params.retry_scid_present = 1;
        } else {
            params.original_dcid = *scid;
        }

        params.original_dcid_present = 1;

        if (ngtcp2_crypto_generate_stateless_reset_token(params.stateless_reset_token, this->_sharedData->_static_secret.data(), this->_sharedData->_static_secret.size(), &_scid) != 0) {
            return -1;
        }

        auto path = ngtcp2_path{
            .local = {
                    .addr    = const_cast<sockaddr *>(&local_addr.su.sa),
                    .addrlen = local_addr.len,
            },
            .remote = {
                    .addr    = const_cast<sockaddr *>(sa),
                    .addrlen = salen,
            },
            .user_data = const_cast<Endpoint *>(&ep),
        };
        if (auto rv = ngtcp2_conn_server_new(&_conn, dcid, &_scid, &path, version, &callbacks, &settings, &params, nullptr, this); rv != 0) {
            HTTP_DBG("ngtcp2_conn_server_new: {}", ngtcp2_strerror(rv));
            return -1;
        }

        if (ngtcp2_crypto_ossl_init() != 0) {
            HTTP_DBG("ngtcp2_crypto_ossl_init: failed");
            return -1;
        }

        if (_tlsSession.init(tls_ctx, &_conn_ref) != 0) {
            return -1;
        }

        _tlsSession.enable_keylog();

        ngtcp2_conn_set_tls_native_handle(_conn, _tlsSession.get_native_handle());

        return 0;
    }

    int on_stream_reset(int64_t stream_id) {
        if (_httpconn) {
            if (auto rv = nghttp3_conn_shutdown_stream_read(_httpconn, stream_id);
                    rv != 0) {
                HTTP_DBG("Server::H3::on_stream_reset: nghttp3_conn_shutdown_stream_read: {}", nghttp3_strerror(rv));
                return -1;
            }
        }
        return 0;
    }

    int on_stream_close(int64_t stream_id, uint64_t app_error_code) {
        HTTP_DBG("Server::H3::on_stream_close: stream_id={} app_error_code={}", stream_id, app_error_code);

        if (_httpconn) {
            if (app_error_code == 0) {
                app_error_code = NGHTTP3_H3_NO_ERROR;
            }
            auto rv = nghttp3_conn_close_stream(_httpconn, stream_id, app_error_code);
            switch (rv) {
            case 0:
                break;
            case NGHTTP3_ERR_STREAM_NOT_FOUND:
                if (ngtcp2_is_bidi_stream(stream_id)) {
                    assert(!ngtcp2_conn_is_local_stream(_conn, stream_id));
                    ngtcp2_conn_extend_max_streams_bidi(_conn, 1);
                }
                break;
            default:
                HTTP_DBG("Server::H3::on_stream_close: nghttp3_conn_close_stream: {}", nghttp3_strerror(rv));
                ngtcp2_ccerr_set_application_error(&_last_error, nghttp3_err_infer_quic_app_error_code(rv), nullptr, 0);
                return -1;
            }
        }

        return 0;
    }

    void extend_max_remote_streams_bidi(uint64_t max_streams) {
        if (!_httpconn) {
            return;
        }

        nghttp3_conn_set_max_client_streams_bidi(_httpconn, max_streams);
    }

    int extend_max_stream_data(int64_t stream_id, uint64_t max_data) {
        std::ignore = max_data;
        if (auto rv = nghttp3_conn_unblock_stream(_httpconn, stream_id); rv != 0) {
            HTTP_DBG("Server::H3::extend_max_stream_data: nghttp3_conn_unblock_stream: {}", nghttp3_strerror(rv));
            return -1;
        }
        return 0;
    }

    int setup_httpconn() {
        if (_httpconn) {
            return 0;
        }

        if (const auto n = ngtcp2_conn_get_streams_uni_left(_conn); n < 3) {
            HTTP_DBG("peer does not allow at least 3 unidirectional streams. (allows {})", n);
            return -1;
        }

        nghttp3_callbacks callbacks;
        memset(&callbacks, 0, sizeof(callbacks));
#ifdef OPENCMW_DEBUG_HTTP
        nghttp3_set_debug_vprintf_callback(print_debug2);
#endif
        callbacks.acked_stream_data = [](nghttp3_conn *, std::int64_t stream_id, std::uint64_t datalen, void *conn_user_data, void *) {
            auto session = static_cast<Http3Session *>(conn_user_data);
            return session->acked_stream_data(stream_id, datalen);
        };
        callbacks.stream_close = [](nghttp3_conn *, std::int64_t stream_id, std::uint64_t error_code, void *conn_user_data, void *) {
            std::ignore = error_code;
            HTTP_DBG("Server::H3::stream_close: stream_id={} error_code={}", stream_id, error_code);
            auto session = static_cast<Http3Session *>(conn_user_data);

            if (ngtcp2_is_bidi_stream(stream_id)) {
                assert(!ngtcp2_conn_is_local_stream(session->_conn, stream_id));
                ngtcp2_conn_extend_max_streams_bidi(session->_conn, 1);
            }

            return 0;
        };
        callbacks.recv_data = [](nghttp3_conn *, std::int64_t stream_id, const uint8_t *data, std::size_t datalen, void *conn_user_data, void * /*stream_user_data*/) {
            HTTP_DBG("Server::H3::recv_data: stream_id={} datalen={}", stream_id, datalen);
            auto dataView = std::string_view(reinterpret_cast<const char *>(data), datalen);
            auto session  = static_cast<Http3Session *>(conn_user_data);
            session->addData(stream_id, dataView);
            return 0;
        };
        callbacks.deferred_consume = [](nghttp3_conn *, std::int64_t stream_id, std::size_t datalen, void * /*conn_user_data*/, void *) {
            std::ignore = stream_id;
            std::ignore = datalen;
            HTTP_DBG("Server::H3::deferred_consume: stream_id={} datalen={}", stream_id, datalen);
            return 0;
        };
        callbacks.begin_headers = [](nghttp3_conn *, std::int64_t stream_id, void * /*conn_user_data*/, void *) {
            std::ignore = stream_id;
            HTTP_DBG("Server::H3::begin_headers: stream_id={}", stream_id);
            return 0;
        };
        callbacks.recv_header = [](nghttp3_conn *, std::int64_t stream_id, std::int32_t token, nghttp3_rcbuf *name, nghttp3_rcbuf *value, uint8_t /*flags*/, void *conn_user_data, void *) {
            std::ignore    = token;
            auto nameView  = as_view(name);
            auto valueView = as_view(value);
            HTTP_DBG("Server::H3::recv_header: stream_id={} token={} name={} value={}", stream_id, token, nameView, valueView);
            auto session = static_cast<Http3Session *>(conn_user_data);
            session->addHeader(stream_id, nameView, valueView);
            return 0;
        };
        callbacks.end_headers = [](nghttp3_conn *, std::int64_t stream_id, int fin, void * /*conn_user_data*/, void *) {
            std::ignore = stream_id;
            std::ignore = fin;
            HTTP_DBG("Server::H3::end_headers: stream_id={} fin={}", stream_id, fin);
            return 0;
        };
        callbacks.stop_sending = [](nghttp3_conn *, std::int64_t stream_id, std::uint64_t app_error_code, void * /*conn_user_data*/, void *) {
            std::ignore = stream_id;
            std::ignore = app_error_code;
            HTTP_DBG("Server::H3::stop_sending: stream_id={} error_code={}", stream_id, app_error_code);
            return 0;
        };
        callbacks.end_stream = [](nghttp3_conn *, std::int64_t stream_id, void *conn_user_data, void *) {
            HTTP_DBG("Server::H3::end_stream: stream_id={}", stream_id);
            auto session = static_cast<Http3Session *>(conn_user_data);
            session->processCompletedRequest(stream_id);
            return 0;
        };
        callbacks.reset_stream = [](nghttp3_conn *, std::int64_t stream_id, std::uint64_t error_code, void * /*conn_user_data*/, void *) {
            std::ignore = stream_id;
            std::ignore = error_code;
            HTTP_DBG("Server::H3::reset_stream: stream_id={} error_code={}", stream_id, error_code);
            return 0;
        };
        callbacks.shutdown = [](nghttp3_conn *, std::int64_t id, void * /*conn_user_data*/) {
            std::ignore = id;
            HTTP_DBG("Server::H3::shutdown: id={}", id);
            return 0;
        };
        callbacks.recv_settings = [](nghttp3_conn *, const nghttp3_settings *, void * /*conn_user_data*/) {
            HTTP_DBG("Server::H3::recv_settings");
            return 0;
        };

        nghttp3_settings settings;
        nghttp3_settings_default(&settings);
        settings.qpack_max_dtable_capacity = 4096;
        settings.qpack_blocked_streams     = 100;

        auto mem                           = nghttp3_mem_default();

        if (auto rv = nghttp3_conn_server_new(&_httpconn, &callbacks, &settings, mem, this); rv != 0) {
            HTTP_DBG("nghttp3_conn_server_new: {}", nghttp3_strerror(rv));
            return -1;
        }

        auto params = ngtcp2_conn_get_local_transport_params(_conn);

        nghttp3_conn_set_max_client_streams_bidi(_httpconn, params->initial_max_streams_bidi);

        int64_t ctrl_stream_id;

        if (auto rv = ngtcp2_conn_open_uni_stream(_conn, &ctrl_stream_id, nullptr); rv != 0) {
            HTTP_DBG("ngtcp2_conn_open_uni_stream: {}", ngtcp2_strerror(rv));
            return -1;
        }
        if (auto rv = nghttp3_conn_bind_control_stream(_httpconn, ctrl_stream_id); rv != 0) {
            HTTP_DBG("nghttp3_conn_bind_control_stream: {}", nghttp3_strerror(rv));
            return -1;
        }

        HTTP_DBG("Server::H3::setup_httpconn: stream_id={}", ctrl_stream_id);

        int64_t qpack_enc_stream_id, qpack_dec_stream_id;

        if (auto rv = ngtcp2_conn_open_uni_stream(_conn, &qpack_enc_stream_id, nullptr); rv != 0) {
            HTTP_DBG("ngtcp2_conn_open_uni_stream: {}", ngtcp2_strerror(rv));
            return -1;
        }

        if (auto rv = ngtcp2_conn_open_uni_stream(_conn, &qpack_dec_stream_id, nullptr); rv != 0) {
            HTTP_DBG("ngtcp2_conn_open_uni_stream: {}", ngtcp2_strerror(rv));
            return -1;
        }

        if (auto rv = nghttp3_conn_bind_qpack_streams(_httpconn, qpack_enc_stream_id, qpack_dec_stream_id); rv != 0) {
            HTTP_DBG("nghttp3_conn_bind_qpack_streams: {}", nghttp3_strerror(rv));
            return -1;
        }

        HTTP_DBG("Server::H3::setup_httpconn: qpack streams encoder={} decoder={}", qpack_enc_stream_id, qpack_dec_stream_id);
        return 0;
    }

    int acked_stream_data(std::int64_t stream_id, std::size_t datalen) {
        HTTP_DBG("Server::H3::acked_stream_data: stream_id={} datalen={}", stream_id, datalen);
        auto it = this->_responsesByStreamId.find(stream_id);
        assert(it != this->_responsesByStreamId.end());
        auto &response = it->second;
        if (response.restResponse.bodyReader) {
            while (datalen > 0) {
                assert(!response.bodyReaderChunks.empty());
                auto      &front = response.bodyReaderChunks.front();
                const auto toAck = std::min(datalen, front->unacked);
                front->unacked -= toAck;
                datalen -= toAck;
                HTTP_DBG("Server::H3::acked_stream_data: stream_id={} acked {} bytes; left={}", stream_id, toAck, front->unacked);
                if (front->unacked == 0) {
                    this->_sharedData->releaseChunk(std::move(response.bodyReaderChunks.front()));
                    response.bodyReaderChunks.pop_front();
                }
                HTTP_DBG("Server::H3::acked_stream_data: stream_id={} chunks={}", stream_id, response.bodyReaderChunks.size());
            }
        } else {
            // BodyBuffer/BodyView, nothing to do
            HTTP_DBG("Server::H3::acked_stream_data: stream_id={} acked {}", stream_id, datalen);
        }

        return 0;
    }

    int acked_stream_data_offset(int64_t stream_id, uint64_t datalen) {
        HTTP_DBG("Server::H3::acked_stream_data_offset: stream_id={} datalen={}", stream_id, datalen);
        if (!_httpconn) {
            return 0;
        }

        if (auto rv = nghttp3_conn_add_ack_offset(_httpconn, stream_id, datalen); rv != 0) {
            HTTP_DBG("nghttp3_conn_add_ack_offset: {}", nghttp3_strerror(rv));
            return -1;
        }

        return 0;
    }

    int handshake_completed() {
        if (_tlsSession.send_session_ticket() != 0) {
            HTTP_DBG("Unable to send session ticket");
        }

        std::array<uint8_t, NGTCP2_CRYPTO_MAX_REGULAR_TOKENLEN> token;

        auto                                                    path = ngtcp2_conn_get_path(_conn);
        auto                                                    t    = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::system_clock::now().time_since_epoch())
                         .count();

        auto tokenlen = ngtcp2_crypto_generate_regular_token(token.data(), this->_sharedData->_static_secret.data(), this->_sharedData->_static_secret.size(), path->remote.addr, path->remote.addrlen, static_cast<ngtcp2_tstamp>(t));
        if (tokenlen < 0) {
            HTTP_DBG("Server::H3::handshake_completed: ngtcp2_crypto_generate_regular_token failed");
            return 0;
        }

        if (auto rv = ngtcp2_conn_submit_new_token(_conn, token.data(), static_cast<std::size_t>(tokenlen)); rv != 0) {
            HTTP_DBG("Server::H3::handshake_completed: ngtcp2_conn_submit_new_token failed: {}", ngtcp2_strerror(rv));
            return -1;
        }

        return 0;
    }

    int on_read(const Endpoint &ep, const Address &local_addr, const sockaddr *sa, socklen_t salen, const ngtcp2_pkt_info *pi, std::span<const uint8_t> data) {
        auto path = ngtcp2_path{
            .local = {
                    .addr    = const_cast<sockaddr *>(&local_addr.su.sa),
                    .addrlen = local_addr.len,
            },
            .remote = {
                    .addr    = const_cast<sockaddr *>(sa),
                    .addrlen = salen,
            },
            .user_data = const_cast<Endpoint *>(&ep),
        };
        if (auto rv = ngtcp2_conn_read_pkt(_conn, &path, pi, data.data(), data.size(), timestamp()); rv != 0) {
            switch (rv) {
            case NGTCP2_ERR_DRAINING:
                start_draining_period();
                return NETWORK_ERR_CLOSE_WAIT;
            case NGTCP2_ERR_RETRY:
                return NETWORK_ERR_RETRY;
            case NGTCP2_ERR_DROP_CONN:
                return NETWORK_ERR_DROP_CONN;
            case NGTCP2_ERR_CRYPTO:
                if (!_last_error.error_code) {
                    ngtcp2_ccerr_set_tls_alert(&_last_error, ngtcp2_conn_get_tls_alert(_conn), nullptr, 0);
                }
                break;
            default:
                if (!_last_error.error_code) {
                    ngtcp2_ccerr_set_liberr(&_last_error, rv, nullptr, 0);
                }
            }
            return handle_error();
        }

        return 0;
    }

    void start_draining_period() {
        HTTP_DBG("Server::H3::start_draining_period");
        _state = State::Draining;
    }

    int on_stream_stop_sending(std::int64_t stream_id) {
        if (!_httpconn) {
            return 0;
        }

        if (auto rv = nghttp3_conn_shutdown_stream_read(_httpconn, stream_id); rv != 0) {
            HTTP_DBG("nghttp3_conn_shutdown_stream_read: {}", nghttp3_strerror(rv));
            return -1;
        }

        return 0;
    }

    int send_blocked_packet() {
        assert(_tx.send_blocked);

        for (; _tx.num_blocked_sent < _tx.num_blocked; ++_tx.num_blocked_sent) {
            auto       &p = _tx.blocked[_tx.num_blocked_sent];

            ngtcp2_addr local_addr{
                .addr    = &p.local_addr.su.sa,
                .addrlen = p.local_addr.len,
            };
            ngtcp2_addr remote_addr{
                .addr    = &p.remote_addr.su.sa,
                .addrlen = p.remote_addr.len,
            };

            auto [rest, rv] = server_->send_packet(*p.endpoint, _no_gso, local_addr, remote_addr, p.ecn, p.data, p.gso_size);
            if (rv != 0) {
                assert(NETWORK_ERR_SEND_BLOCKED == rv);
                p.data = rest;
                return 0;
            }
        }

        _tx.send_blocked     = false;
        _tx.num_blocked      = 0;
        _tx.num_blocked_sent = 0;

        return 0;
    }

    int on_write() {
        if (ngtcp2_conn_in_closing_period(_conn) || ngtcp2_conn_in_draining_period(_conn)) {
            return 0;
        }

        if (_tx.send_blocked) {
            if (auto rv = send_blocked_packet(); rv != 0) {
                return rv;
            }

            if (_tx.send_blocked) {
                return 0;
            }
        }

        if (auto rv = write_streams(); rv != 0) {
            return rv;
        }

        return 0;
    }

    void write_handler() {
        // this tries to mimick the state handling from the example, where different ev callbacks are set at different times

        switch (_state) {
        case State::Initial: // writecb
            switch (on_write()) {
            case 0:
            case NETWORK_ERR_CLOSE_WAIT:
                return;
            default:
                server_->remove(_conn);
            }
            break;
        case State::Closing:
        case State::Draining: { // close_waitcb
            if (ngtcp2_conn_in_closing_period(_conn)) {
                HTTP_DBG("Server::H3::write_handler: closing period is over");
                server_->remove(_conn);
                return;
            }
            if (ngtcp2_conn_in_draining_period(_conn)) {
                HTTP_DBG("Server::H3::write_handler: draining period is over");
                server_->remove(_conn);
                return;
            }

            assert(0);
        } break;
        }
    }

    void on_send_blocked(Endpoint &ep, const ngtcp2_addr &local_addr, const ngtcp2_addr &remote_addr, unsigned int ecn, std::span<const uint8_t> data, size_t gso_size) {
        assert(_tx.num_blocked || !_tx.send_blocked);
        assert(_tx.num_blocked < 2);
        assert(gso_size);

        _tx.send_blocked = true;

        auto &p          = _tx.blocked[_tx.num_blocked++];

        memcpy(&p.local_addr.su, local_addr.addr, local_addr.addrlen);
        memcpy(&p.remote_addr.su, remote_addr.addr, remote_addr.addrlen);

        p.local_addr.len  = local_addr.addrlen;
        p.remote_addr.len = remote_addr.addrlen;
        p.endpoint        = &ep;
        p.ecn             = ecn;
        p.data            = data;
        p.gso_size        = gso_size;
    }

    int write_streams() {
        std::array<nghttp3_vec, 16> vec;
        ngtcp2_path_storage         ps, prev_ps;
        uint32_t                    prev_ecn                  = 0;
        auto                        max_udp_payload_size      = ngtcp2_conn_get_max_tx_udp_payload_size(_conn);
        auto                        path_max_udp_payload_size = ngtcp2_conn_get_path_max_tx_udp_payload_size(_conn);
        ngtcp2_pkt_info             pi;
        size_t                      gso_size = 0;
        auto                        ts       = timestamp();
        auto                        txbuf    = std::span{ _tx.data.get(), std::max(ngtcp2_conn_get_send_quantum(_conn), path_max_udp_payload_size) };
        auto                        buf      = txbuf;

        ngtcp2_path_storage_zero(&ps);
        ngtcp2_path_storage_zero(&prev_ps);

        for (;;) {
            int64_t       stream_id = -1;
            int           fin       = 0;
            nghttp3_ssize sveccnt   = 0;

            if (_httpconn && ngtcp2_conn_get_max_data_left(_conn)) {
                sveccnt = nghttp3_conn_writev_stream(_httpconn, &stream_id, &fin, vec.data(), vec.size());
                if (sveccnt < 0) {
                    HTTP_DBG("nghttp3_conn_writev_stream: {}", nghttp3_strerror(static_cast<int>(sveccnt)));
                    ngtcp2_ccerr_set_application_error(&_last_error, nghttp3_err_infer_quic_app_error_code(static_cast<int>(sveccnt)), nullptr, 0);
                    return handle_error();
                }
            }

            ngtcp2_ssize ndatalen;
            auto         v     = vec.data();
            auto         vcnt  = static_cast<size_t>(sveccnt);

            uint32_t     flags = NGTCP2_WRITE_STREAM_FLAG_MORE;
            if (fin) {
                flags |= NGTCP2_WRITE_STREAM_FLAG_FIN;
            }

            auto buflen = buf.size() >= max_udp_payload_size
                                ? max_udp_payload_size
                                : path_max_udp_payload_size;

            auto nwrite = ngtcp2_conn_writev_stream(_conn, &ps.path, &pi, buf.data(), buflen, &ndatalen, flags, stream_id, reinterpret_cast<const ngtcp2_vec *>(v), vcnt, ts);
            if (nwrite < 0) {
                switch (nwrite) {
                case NGTCP2_ERR_STREAM_DATA_BLOCKED:
                    assert(ndatalen == -1);
                    nghttp3_conn_block_stream(_httpconn, stream_id);
                    continue;
                case NGTCP2_ERR_STREAM_SHUT_WR:
                    assert(ndatalen == -1);
                    nghttp3_conn_shutdown_stream_write(_httpconn, stream_id);
                    continue;
                case NGTCP2_ERR_WRITE_MORE:
                    assert(ndatalen >= 0);
                    if (auto rv = nghttp3_conn_add_write_offset(_httpconn, stream_id, static_cast<std::size_t>(ndatalen)); rv != 0) {
                        HTTP_DBG("nghttp3_conn_add_write_offset: {}", nghttp3_strerror(rv));
                        ngtcp2_ccerr_set_application_error(&_last_error, nghttp3_err_infer_quic_app_error_code(rv), nullptr, 0);
                        return handle_error();
                    }
                    continue;
                }

                assert(ndatalen == -1);

                HTTP_DBG("ngtcp2_conn_writev_stream: {}", ngtcp2_strerror(static_cast<int>(nwrite)));
                ngtcp2_ccerr_set_liberr(&_last_error, static_cast<int>(nwrite), nullptr, 0);
                return handle_error();
            } else if (ndatalen >= 0) {
                if (auto rv = nghttp3_conn_add_write_offset(_httpconn, stream_id, static_cast<std::size_t>(ndatalen)); rv != 0) {
                    HTTP_DBG("nghttp3_conn_add_write_offset: {}", nghttp3_strerror(rv));
                    ngtcp2_ccerr_set_application_error(&_last_error, nghttp3_err_infer_quic_app_error_code(rv), nullptr, 0);
                    return handle_error();
                }
            }

            if (nwrite == 0) {
                auto data = std::span{ std::begin(txbuf), std::begin(buf) };
                if (!data.empty()) {
                    auto &ep = *static_cast<Endpoint *>(prev_ps.path.user_data);

                    if (auto [rest, rv] = server_->send_packet(ep, _no_gso, prev_ps.path.local, prev_ps.path.remote, prev_ecn, data, gso_size); rv != NETWORK_ERR_OK) {
                        assert(NETWORK_ERR_SEND_BLOCKED == rv);

                        on_send_blocked(ep, prev_ps.path.local, prev_ps.path.remote, prev_ecn, rest, gso_size);
                    }
                }

                // We are congestion limited.
                ngtcp2_conn_update_pkt_tx_time(_conn, ts);
                return 0;
            }

            auto last_pkt = std::begin(buf);

            buf           = buf.subspan(static_cast<std::size_t>(nwrite));

            if (last_pkt == std::begin(txbuf)) {
                ngtcp2_path_copy(&prev_ps.path, &ps.path);
                prev_ecn = pi.ecn;
                gso_size = static_cast<std::size_t>(nwrite);
            } else if (!ngtcp2_path_eq(&prev_ps.path, &ps.path) || prev_ecn != pi.ecn || static_cast<size_t>(nwrite) > gso_size || (gso_size > path_max_udp_payload_size && static_cast<size_t>(nwrite) != gso_size)) {
                auto &ep   = *static_cast<Endpoint *>(prev_ps.path.user_data);
                auto  data = std::span{ std::begin(txbuf), last_pkt };

                if (auto [rest, rv] = server_->send_packet(ep, _no_gso, prev_ps.path.local, prev_ps.path.remote, prev_ecn, data, gso_size); rv != 0) {
                    assert(NETWORK_ERR_SEND_BLOCKED == rv);

                    on_send_blocked(ep, prev_ps.path.local, prev_ps.path.remote, prev_ecn, rest, gso_size);

                    data = std::span{ last_pkt, std::begin(buf) };
                    on_send_blocked(*static_cast<Endpoint *>(ps.path.user_data), ps.path.local, ps.path.remote, pi.ecn, data, data.size());
                }

                ngtcp2_conn_update_pkt_tx_time(_conn, ts);
                return 0;
            }

            if (buf.size() < path_max_udp_payload_size || static_cast<size_t>(nwrite) < gso_size) {
                auto &ep   = *static_cast<Endpoint *>(ps.path.user_data);
                auto  data = std::span{ std::begin(txbuf), std::begin(buf) };

                if (auto [rest, rv] = server_->send_packet(ep, _no_gso, ps.path.local, ps.path.remote, pi.ecn, data, gso_size); rv != 0) {
                    assert(NETWORK_ERR_SEND_BLOCKED == rv);

                    on_send_blocked(ep, ps.path.local, ps.path.remote, pi.ecn, rest, gso_size);
                }

                ngtcp2_conn_update_pkt_tx_time(_conn, ts);
                return 0;
            }
        }
    }

    int recv_stream_data(uint32_t flags, int64_t stream_id, std::span<const uint8_t> data) {
        HTTP_DBG("Server::QUIC::recv_stream_data: stream_id={} datalen={}", stream_id, data.size());

        auto nconsumed = nghttp3_conn_read_stream(_httpconn, stream_id, data.data(), data.size(), flags & NGTCP2_STREAM_DATA_FLAG_FIN);
        if (nconsumed < 0) {
            HTTP_DBG("nghttp3_conn_read_stream: {}", nghttp3_strerror(static_cast<int>(nconsumed)));
            ngtcp2_ccerr_set_application_error(&_last_error, nghttp3_err_infer_quic_app_error_code(static_cast<int>(nconsumed)), nullptr, 0);
            return -1;
        }

        ngtcp2_conn_extend_max_stream_offset(_conn, stream_id, static_cast<std::uint64_t>(nconsumed));
        ngtcp2_conn_extend_max_offset(_conn, static_cast<std::uint64_t>(nconsumed));

        return 0;
    }

    int send_conn_close() {
        HTTP_DBG("Server::QUIC::send_conn_close");
        assert(_conn_closebuf && _conn_closebuf->size());
        assert(_conn);
        assert(!ngtcp2_conn_in_draining_period(_conn));

        auto path = ngtcp2_conn_get_path(_conn);

        return server_->send_packet(*static_cast<Endpoint *>(path->user_data), path->local, path->remote, /* ecn = */ 0, _conn_closebuf->data());
    }

    int update_key(uint8_t *rx_secret, uint8_t *tx_secret, ngtcp2_crypto_aead_ctx *rx_aead_ctx, uint8_t *rx_iv, ngtcp2_crypto_aead_ctx *tx_aead_ctx, uint8_t *tx_iv, const uint8_t *current_rx_secret, const uint8_t *current_tx_secret, size_t secretlen) {
        std::array<uint8_t, 64> rx_key, tx_key;
        if (ngtcp2_crypto_update_key(_conn, rx_secret, tx_secret, rx_aead_ctx, rx_key.data(), rx_iv, tx_aead_ctx, tx_key.data(), tx_iv, current_rx_secret, current_tx_secret, secretlen)
                != 0) {
            return -1;
        }
        return 0;
    }

    int send_conn_close(const Endpoint &ep, const Address &local_addr, const sockaddr *sa, socklen_t salen, const ngtcp2_pkt_info * /*pi*/, std::span<const uint8_t> data) {
        assert(_conn_closebuf && _conn_closebuf->size());

        _close_wait.bytes_recv += data.size();
        ++_close_wait.num_pkts_recv;

        if (_close_wait.num_pkts_recv < _close_wait.next_pkts_recv || _close_wait.bytes_recv * 3 < _close_wait.bytes_sent + _conn_closebuf->size()) {
            return 0;
        }

        auto path = ngtcp2_path{
            .local = {
                    .addr    = const_cast<sockaddr *>(&local_addr.su.sa),
                    .addrlen = local_addr.len,
            },
            .remote = {
                    .addr    = const_cast<sockaddr *>(sa),
                    .addrlen = salen,
            },
            .user_data = const_cast<Endpoint *>(&ep),
        };

        auto rv = server_->send_packet(ep, path.local, path.remote,
                /* ecn = */ 0, _conn_closebuf->data());
        if (rv != 0) {
            return rv;
        }

        _close_wait.bytes_sent += _conn_closebuf->size();
        _close_wait.next_pkts_recv *= 2;

        return 0;
    }

    int start_closing_period() {
        if (!_conn || ngtcp2_conn_in_closing_period(_conn) || ngtcp2_conn_in_draining_period(_conn)) {
            return 0;
        }

        _state         = State::Closing;

        _conn_closebuf = std::make_unique<Buffer>(NGTCP2_MAX_UDP_PAYLOAD_SIZE);

        ngtcp2_path_storage ps;

        ngtcp2_path_storage_zero(&ps);

        ngtcp2_pkt_info pi;
        auto            n = ngtcp2_conn_write_connection_close(
                _conn, &ps.path, &pi, _conn_closebuf->wpos(), _conn_closebuf->left(),
                &_last_error, timestamp());
        if (n < 0) {
            HTTP_DBG("ngtcp2_conn_write_connection_close: {}", ngtcp2_strerror(static_cast<int>(n)));
            return -1;
        }

        if (n == 0) {
            return 0;
        }

        _conn_closebuf->push(static_cast<std::size_t>(n));
        return 0;
    }

    int handle_error() {
        if (_last_error.type == NGTCP2_CCERR_TYPE_IDLE_CLOSE) {
            return -1;
        }

        if (start_closing_period() != 0) {
            return -1;
        }

        if (ngtcp2_conn_in_draining_period(_conn)) {
            return NETWORK_ERR_CLOSE_WAIT;
        }

        if (auto rv = send_conn_close(); rv != NETWORK_ERR_OK) {
            return rv;
        }

        return NETWORK_ERR_CLOSE_WAIT;
    }
};

struct Http3ServerSocket {
    int     fd = -1;
    Address addr;

    Http3ServerSocket() = default;

    Http3ServerSocket(int fd_, Address addr_)
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
        Address address;
        auto    maybeFd = create_sock(address, "*", std::to_string(port).c_str(), AF_INET);
        if (!maybeFd) {
            return std::unexpected(std::format("Failed to create HTTP/3 server socket: {}", maybeFd.error()));
        }
        return Http3ServerSocket{ maybeFd.value(), address };
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
            return std::unexpected(std::format("Failed to set up TCP server socket: {}", maybeSsl.error()));
        }
        ssl = std::move(maybeSsl.value());
    }

    auto serverSocket = TcpSocket::create(std::move(ssl), socket(AF_INET, SOCK_STREAM, 0));
    if (!serverSocket) {
        return std::unexpected(serverSocket.error());
    }

    int reuseFlag = 1;
    if (setsockopt(serverSocket->fd, SOL_SOCKET, SO_REUSEADDR, &reuseFlag, sizeof(reuseFlag)) < 0) {
        return std::unexpected(std::format("setsockopt(SO_REUSEADDR) failed: {}", strerror(errno)));
    }

    sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port        = htons(port);
    memset(address.sin_zero, 0, sizeof(address.sin_zero));
    if (::bind(serverSocket->fd, reinterpret_cast<struct sockaddr *>(&address), sizeof(address)) < 0) {
        return std::unexpected(std::format("Bind failed: {}", strerror(errno)));
    }

    if (listen(serverSocket->fd, 32) < 0) {
        return std::unexpected(std::format("Listen failed: {}", strerror(errno)));
    }

    return serverSocket;
}

struct RestServer {
    TcpSocket                                                                 _tcpServerSocket;
    Http3ServerSocket                                                         _quicServerSocket;
    SSL_CTX_Ptr                                                               _sslCtxTcp  = SSL_CTX_Ptr(nullptr, SSL_CTX_free);
    EVP_PKEY_Ptr                                                              _key        = EVP_PKEY_Ptr(nullptr, EVP_PKEY_free);
    X509_Ptr                                                                  _cert       = X509_Ptr(nullptr, X509_free);
    std::shared_ptr<SharedData>                                               _sharedData = std::make_shared<SharedData>();
    std::map<int, std::unique_ptr<Http2Session>>                              _h2Sessions;
    std::unordered_map<ngtcp2_cid, std::shared_ptr<Http3Session<RestServer>>> _h3Sessions;
    std::vector<ngtcp2_cid>                                                   _touchedSessions;
    std::vector<ngtcp2_cid>                                                   _sessionsToRemove;
    IdGenerator                                                               _requestIdGenerator;

    Endpoint                                                                  _endpoint;
    TLSServerContext                                                          _tls_ctx;
    std::mt19937                                                              _randgen;

    std::size_t                                                               _stateless_reset_bucket = NGTCP2_STATELESS_RESET_BURST;

    RestServer()                                                                                      = default;
    RestServer(const RestServer &)                                                                    = delete;
    RestServer &operator=(const RestServer &)                                                         = delete;
    RestServer(RestServer &&)                                                                         = default;
    RestServer &operator=(RestServer &&)                                                              = default;

    RestServer(SSL_CTX_Ptr sslCtxTcp, EVP_PKEY_Ptr key, X509_Ptr cert)
        : _sslCtxTcp(std::move(sslCtxTcp)), _key(std::move(key)), _cert(std::move(cert)) {
        if (_sslCtxTcp) {
            SSL_library_init();
            SSL_load_error_strings();
            OpenSSL_add_all_algorithms();
        }
    }

    static std::expected<RestServer, std::string> unencrypted() {
        return RestServer(SSL_CTX_Ptr(nullptr, SSL_CTX_free), EVP_PKEY_Ptr(nullptr, EVP_PKEY_free), X509_Ptr(nullptr, X509_free));
    }

    static std::expected<RestServer, std::string> sslWithBuffers(std::string_view certBuffer, std::string_view keyBuffer) {
        auto maybeCert = readServerCertificateFromBuffer(certBuffer);
        if (!maybeCert) {
            return std::unexpected(maybeCert.error());
        }
        auto maybeKey = readServerPrivateKeyFromBuffer(keyBuffer);
        if (!maybeKey) {
            return std::unexpected(maybeKey.error());
        }
        auto maybeSslCtxTcp = create_ssl_ctx(maybeKey->get(), maybeCert->get());
        if (!maybeSslCtxTcp) {
            return std::unexpected(maybeSslCtxTcp.error());
        }
        return RestServer(std::move(maybeSslCtxTcp.value()), std::move(maybeKey.value()), std::move(maybeCert.value()));
    }

    static std::expected<RestServer, std::string> sslWithPaths(std::filesystem::path certPath, std::filesystem::path keyPath) {
        auto maybeCert = readServerCertificateFromFile(certPath);
        if (!maybeCert) {
            return std::unexpected(maybeCert.error());
        }
        auto maybeKey = readServerPrivateKeyFromFile(keyPath);
        if (!maybeKey) {
            return std::unexpected(maybeKey.error());
        }
        auto maybeSslCtxTcp = create_ssl_ctx(maybeKey->get(), maybeCert->get());
        if (!maybeSslCtxTcp) {
            return std::unexpected(maybeSslCtxTcp.error());
        }

        return RestServer(std::move(maybeSslCtxTcp.value()), std::move(maybeKey.value()), std::move(maybeCert.value()));
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

        auto handle = [&message, id](auto &sessions) {
            auto matchesId = [id](const auto &pendingRequest) { return std::get<0>(pendingRequest) == id; };

            auto it        = std::ranges::find_if(sessions, [matchesId](const auto &session) {
                return std::ranges::find_if(session.second->_pendingRequests, matchesId) != session.second->_pendingRequests.end();
            });

            if (it != sessions.end()) {
                auto &session                 = it->second;
                auto  pendingIt               = std::ranges::find_if(session->_pendingRequests, matchesId);
                const auto &[reqId, streamId] = *pendingIt;
                const auto code               = message.error.empty() ? kHttpOk : kHttpError;
                session->sendResponse(streamId, code, std::move(message));
                session->_pendingRequests.erase(pendingIt);
                return true;
            };
            return false;
        };

        if (!handle(_h2Sessions)) {
            handle(_h3Sessions);
        }
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
        const auto index = entry.lastIndex();

        for (auto &session : _h2Sessions | std::views::values) {
            session->handleNotification(zmqTopic, index, entry.messages.back());
        }
        for (auto &session : _h3Sessions | std::views::values) {
            session->handleNotification(zmqTopic, index, entry.messages.back());
        }
    }

    void populatePollerItems(std::vector<zmq_pollitem_t> &items) {
        if (_tcpServerSocket.fd != -1) {
            items.push_back(zmq_pollitem_t{ nullptr, _tcpServerSocket.fd, ZMQ_POLLIN, 0 });
        }
        if (_quicServerSocket.fd != -1) {
            items.push_back(zmq_pollitem_t{ nullptr, _quicServerSocket.fd, ZMQ_POLLIN | ZMQ_POLLOUT, 0 });
        }
        for (const auto &[_, session] : _h2Sessions) {
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

            auto [newSessionIt, inserted] = _h2Sessions.try_emplace(newFd, std::make_unique<Http2Session>(std::move(clientSocket.value()), _sharedData));
            assert(inserted);
            auto                  &newSession = newSessionIt->second;
            nghttp2_settings_entry iv[1]      = { { NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 3000 } };
            if (nghttp2_submit_settings(newSession->_session, NGHTTP2_FLAG_NONE, iv, 1) != 0) {
                HTTP_DBG("nghttp2_submit_settings failed");
            }
            return {};
        }

        if (fd == _quicServerSocket.fd) {
            if (read) {
                on_read();
            }
            if (write) {
                for (auto &session : _h3Sessions | std::views::values) {
                    session->write_handler();
                }
                std::erase_if(_h3Sessions, [this](const auto &pair) {
                    return std::ranges::contains(_sessionsToRemove, pair.first);
                });
                _sessionsToRemove.clear();
            }
            std::vector<Message> messages;
            for (const auto &dcid : _touchedSessions) {
                auto sessionIt = _h3Sessions.find(dcid);
                if (sessionIt == _h3Sessions.end()) {
                    continue;
                }
                auto ms = sessionIt->second->getMessages(_requestIdGenerator);
                messages.insert(messages.end(), std::make_move_iterator(ms.begin()), std::make_move_iterator(ms.end()));
            }
            _touchedSessions.clear();
            return messages;
        }

        auto sessionIt = _h2Sessions.find(fd);
        assert(sessionIt != _h2Sessions.end());
        assert(sessionIt->second->_socket.fd == fd);
        auto &session = sessionIt->second;

        if (session->_socket._state != TcpSocket::Connected) {
            if (auto r = session->_socket.continueHandshake(); !r) {
                HTTP_DBG("Handshake failed: {}", r.error());
                _h2Sessions.erase(sessionIt);
                return {};
            }
            return {};
        }

        if (write) {
            if (!session->_writeBuffer.write(session->_session, session->_socket)) {
                HTTP_DBG("Failed to write to peer");
                _h2Sessions.erase(sessionIt);
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
                _h2Sessions.erase(sessionIt);
                return {};
            }
            if (nghttp2_session_mem_recv2(session->_session, buffer.data(), static_cast<size_t>(bytes_read)) < 0) {
                HTTP_DBG("Server: nghttp2_session_mem_recv2 failed");
                _h2Sessions.erase(sessionIt);
                return {};
            }
            mightHaveMore = bytes_read == static_cast<ssize_t>(buffer.size());
        }

        return session->getMessages(_requestIdGenerator);
    }

    std::expected<void, std::string>
    bind(const majordomo::rest::Settings &settings) {
        using enum majordomo::rest::Protocol;
        std::uint16_t port = settings.port;
        if ((settings.protocols & Http2) == 0 && (settings.protocols & Http3) == 0) {
            return std::unexpected("At least one protocol must be enabled (HTTP/2 or HTTP/3)");
        }

        if (_tcpServerSocket.fd != -1) {
            return std::unexpected("Server already bound");
        }

        if ((settings.protocols & Http2) != 0) {
            auto tcpSocket = createTcpServerSocket(_sslCtxTcp.get(), port);
            if (!tcpSocket) {
                return std::unexpected(tcpSocket.error());
            }
            _tcpServerSocket = std::move(tcpSocket.value());
        }

        if ((settings.protocols & Http3) == 0) {
            return {};
        }

        if (!_sslCtxTcp) {
            return std::unexpected("HTTP/3 requires TLS");
        }

        auto quicSocket = Http3ServerSocket::create(port);
        if (!quicSocket) {
            return std::unexpected(quicSocket.error());
        }
        if (_tls_ctx.init(_key, _cert, AppProtocol::H3) != 0) {
            return std::unexpected("Failed to initialize TLS context for HTTP/3");
        }
        if (settings.upgradeHttp3) {
            _sharedData->_altSvcHeaderValue = std::format("h3=\":{}\"; ma=86400", port);
            _sharedData->_altSvcHeader      = nv(u8span("alt-svc"), u8span(_sharedData->_altSvcHeaderValue), NGHTTP2_NV_FLAG_NO_COPY_NAME | NGHTTP2_NV_FLAG_NO_COPY_VALUE);
        }
        _quicServerSocket               = std::move(quicSocket.value());
        _endpoint.fd                    = _quicServerSocket.fd;
        return {};
    }

    inline int send_version_negotiation(uint32_t version, std::span<const uint8_t> dcid, std::span<const uint8_t> scid, Endpoint &ep, const Address &local_addr, const sockaddr *sa, socklen_t salen) {
        Buffer                                              buf{ NGTCP2_MAX_UDP_PAYLOAD_SIZE };
        std::array<uint32_t, 1 + max_preferred_versionslen> sv;

        auto                                                p = std::begin(sv);

        *p++                                                  = generate_reserved_version(sa, salen, version);

        *p++        = NGTCP2_PROTO_VER_V1;
        *p++        = NGTCP2_PROTO_VER_V2;

        auto nwrite                                           = ngtcp2_pkt_write_version_negotiation(buf.wpos(), buf.left(), std::uniform_int_distribution<uint8_t>()(_randgen), dcid.data(), dcid.size(), scid.data(), scid.size(), sv.data(), static_cast<std::size_t>(p - std::begin(sv)));
        if (nwrite < 0) {
            HTTP_DBG("ngtcp2_pkt_write_version_negotiation: {}", ngtcp2_strerror(static_cast<int>(nwrite)));
            return -1;
        }

        buf.push(static_cast<std::size_t>(nwrite));

        ngtcp2_addr laddr{
            .addr    = const_cast<sockaddr *>(&local_addr.su.sa),
            .addrlen = local_addr.len,
        };
        ngtcp2_addr raddr{
            .addr    = const_cast<sockaddr *>(sa),
            .addrlen = salen,
        };

        if (send_packet(ep, laddr, raddr, /* ecn = */ 0, buf.data()) != NETWORK_ERR_OK) {
            return -1;
        }

        return 0;
    }

    int on_read() {
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
             .msg_name       = &su,
             .msg_namelen    = sizeof(su),
             .msg_iov        = &msg_iov,
             .msg_iovlen     = 1,
             .msg_control    = msg_ctrl,
             .msg_controllen = sizeof(msg_ctrl),
             .msg_flags      = 0,
        };

        for (; pktcnt < 10;) {
            msg.msg_namelen    = sizeof(su);
            msg.msg_controllen = sizeof(msg_ctrl);

            auto nread         = ::recvmsg(_quicServerSocket.fd, &msg, 0);
            if (nread == -1) {
                if (!(errno == EAGAIN || errno == ENOTCONN)) {
                    HTTP_DBG("recvmsg: {}", strerror(errno));
                }
                return 0;
            }

            // Packets less than 21 bytes never be a valid QUIC packet.
            if (nread < 21) {
                ++pktcnt;

                continue;
            }

            pi.ecn          = static_cast<uint8_t>(msghdr_get_ecn(&msg, su.storage.ss_family));
            auto local_addr = msghdr_get_local_addr(&msg, su.storage.ss_family);
            if (!local_addr) {
                ++pktcnt;
                HTTP_DBG("Unable to obtain local address");
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

                // Packets less than 21 bytes never be a valid QUIC packet.
                if (datalen < 21) {
                    break;
                }

                // Endpoint, Address kept for upstream with example code

                _endpoint.addr = *local_addr;
                _endpoint.fd   = _quicServerSocket.fd;

                auto dcid      = read_pkt(_endpoint, _endpoint.addr, &su.sa, msg.msg_namelen, &pi, { data.data(), datalen });
                if (dcid) {
                    _touchedSessions.push_back(*dcid);
                }

                data = data.subspan(datalen);
            }
        }

        return 0;
    }

    int send_stateless_connection_close(const ngtcp2_pkt_hd *chd, Endpoint &ep, const Address &local_addr, const sockaddr *sa, socklen_t salen) {
        HTTP_DBG("Server::QUIC::send_stateless_connection_close");
        Buffer buf{ NGTCP2_MAX_UDP_PAYLOAD_SIZE };

        auto   nwrite = ngtcp2_crypto_write_connection_close(buf.wpos(), buf.left(), chd->version, &chd->scid, &chd->dcid, NGTCP2_INVALID_TOKEN, nullptr, 0);
        if (nwrite < 0) {
            HTTP_DBG("ngtcp2_crypto_write_connection_close: {}", ngtcp2_strerror(static_cast<int>(nwrite)));
            return -1;
        }

        buf.push(static_cast<std::size_t>(nwrite));

        ngtcp2_addr laddr{
            .addr    = const_cast<sockaddr *>(&local_addr.su.sa),
            .addrlen = local_addr.len,
        };
        ngtcp2_addr raddr{
            .addr    = const_cast<sockaddr *>(sa),
            .addrlen = salen,
        };

        if (send_packet(ep, laddr, raddr, /* ecn = */ 0, buf.data()) != NETWORK_ERR_OK) {
            return -1;
        }

        return 0;
    }

    int send_stateless_reset(size_t pktlen, std::span<const uint8_t> dcid, Endpoint &ep, const Address &local_addr, const sockaddr *sa, socklen_t salen) {
        if (_stateless_reset_bucket == 0) {
            return 0;
        }

        --_stateless_reset_bucket;

        ngtcp2_cid cid;

        ngtcp2_cid_init(&cid, dcid.data(), dcid.size());

        std::array<uint8_t, NGTCP2_STATELESS_RESET_TOKENLEN> token;

        if (ngtcp2_crypto_generate_stateless_reset_token(token.data(), _sharedData->_static_secret.data(), _sharedData->_static_secret.size(), &cid) != 0) {
            return -1;
        }

        // SCID + minimum expansion - NGTCP2_STATELESS_RESET_TOKENLEN
        constexpr size_t max_rand_byteslen = NGTCP2_MAX_CIDLEN + 22 - NGTCP2_STATELESS_RESET_TOKENLEN;

        size_t           rand_byteslen;

        if (pktlen <= 43) {
            // As per https://datatracker.ietf.org/doc/html/rfc9000#section-10.3
            rand_byteslen = pktlen - NGTCP2_STATELESS_RESET_TOKENLEN - 1;
        } else {
            rand_byteslen = max_rand_byteslen;
        }

        std::array<uint8_t, max_rand_byteslen> rand_bytes;

        if (generate_secure_random({ rand_bytes.data(), rand_byteslen }) != 0) {
            return -1;
        }

        Buffer buf{ NGTCP2_MAX_UDP_PAYLOAD_SIZE };

        auto   nwrite = ngtcp2_pkt_write_stateless_reset(buf.wpos(), buf.left(), token.data(), rand_bytes.data(), rand_byteslen);
        if (nwrite < 0) {
            HTTP_DBG("ngtcp2_pkt_write_stateless_reset: {}", ngtcp2_strerror(static_cast<int>(nwrite)));
            return -1;
        }

        buf.push(static_cast<std::size_t>(nwrite));

        ngtcp2_addr laddr{
            .addr    = const_cast<sockaddr *>(&local_addr.su.sa),
            .addrlen = local_addr.len,
        };
        ngtcp2_addr raddr{
            .addr    = const_cast<sockaddr *>(sa),
            .addrlen = salen,
        };

        if (send_packet(ep, laddr, raddr, /* ecn = */ 0, buf.data()) != NETWORK_ERR_OK) {
            return -1;
        }

        return 0;
    }

    int verify_retry_token(ngtcp2_cid *ocid, const ngtcp2_pkt_hd *hd, const sockaddr *sa, socklen_t salen) {
        int rv;

        HTTP_DBG("Received Retry token from [{}]:{}", straddr(sa, salen), straddr(sa, salen));

        auto t = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

        rv     = ngtcp2_crypto_verify_retry_token2(ocid, hd->token, hd->tokenlen, _sharedData->_static_secret.data(), _sharedData->_static_secret.size(), hd->version, sa, salen, &hd->dcid, 10 * NGTCP2_SECONDS, static_cast<ngtcp2_tstamp>(t));
        switch (rv) {
        case 0:
            break;
        case NGTCP2_CRYPTO_ERR_VERIFY_TOKEN:
            HTTP_DBG("Could not verify Retry token");
            return -1;
        default:
            HTTP_DBG("Could not verify Retry token: {}. Continue without the token", ngtcp2_strerror(rv));
            return 1;
        }

        HTTP_DBG("Token was successfully validated");
        return 0;
    }

    int verify_token(const ngtcp2_pkt_hd *hd, const sockaddr *sa, socklen_t salen) {
        std::array<char, NI_MAXHOST> host;
        std::array<char, NI_MAXSERV> port;

        if (auto rv = getnameinfo(sa, salen, host.data(), host.size(), port.data(), port.size(), NI_NUMERICHOST | NI_NUMERICSERV); rv != 0) {
            HTTP_DBG("getnameinfo: {}", gai_strerror(rv));
            return -1;
        }

        HTTP_DBG("Received token from [{}]:{}", host.data(), port.data());

        auto t = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::system_clock::now().time_since_epoch())
                         .count();

        if (ngtcp2_crypto_verify_regular_token(hd->token, hd->tokenlen, _sharedData->_static_secret.data(), _sharedData->_static_secret.size(), sa, salen, 3600 * NGTCP2_SECONDS, static_cast<ngtcp2_tstamp>(t)) != 0) {
            HTTP_DBG("Could not verify token");
            return -1;
        }

        HTTP_DBG("Token was successfully validated");
        return 0;
    }

    int send_packet(const Endpoint &ep, const ngtcp2_addr &local_addr, const ngtcp2_addr &remote_addr, unsigned int ecn, std::span<const uint8_t> data) {
        auto no_gso  = false;
        auto [_, rv] = send_packet(ep, no_gso, local_addr, remote_addr, ecn, data, data.size());
        return rv;
    }

    std::pair<std::span<const uint8_t>, int> send_packet(const Endpoint &ep, bool &no_gso, const ngtcp2_addr &local_addr, const ngtcp2_addr &remote_addr, unsigned int ecn, std::span<const uint8_t> data, size_t gso_size) {
        assert(gso_size);

        if (no_gso && data.size() > gso_size) {
            for (; !data.empty();) {
                auto len     = std::min(gso_size, data.size());

                auto [_, rv] = send_packet(ep, no_gso, local_addr, remote_addr, ecn, { std::begin(data), len }, len);
                if (rv != 0) {
                    return { data, rv };
                }

                data = data.subspan(len);
            }

            return { {}, 0 };
        }

        iovec msg_iov{
            .iov_base = const_cast<uint8_t *>(data.data()),
            .iov_len  = data.size(),
        };

        uint8_t msg_ctrl[CMSG_SPACE(sizeof(int)) + CMSG_SPACE(sizeof(uint16_t)) + CMSG_SPACE(sizeof(in6_pktinfo))]{};

        msghdr  msg{
             .msg_name       = const_cast<sockaddr *>(remote_addr.addr),
             .msg_namelen    = remote_addr.addrlen,
             .msg_iov        = &msg_iov,
             .msg_iovlen     = 1,
             .msg_control    = msg_ctrl,
             .msg_controllen = sizeof(msg_ctrl),
             .msg_flags      = 0,
        };

        size_t controllen = 0;

        auto   cm         = CMSG_FIRSTHDR(&msg);

        switch (local_addr.addr->sa_family) {
        case AF_INET: {
            controllen += CMSG_SPACE(sizeof(in_pktinfo));
            cm->cmsg_level    = IPPROTO_IP;
            cm->cmsg_type     = IP_PKTINFO;
            cm->cmsg_len      = CMSG_LEN(sizeof(in_pktinfo));
            auto       addrin = reinterpret_cast<sockaddr_in *>(local_addr.addr);
            in_pktinfo pktinfo;
            pktinfo.ipi_spec_dst = addrin->sin_addr,
            memcpy(CMSG_DATA(cm), &pktinfo, sizeof(pktinfo));

            break;
        }
        case AF_INET6: {
            controllen += CMSG_SPACE(sizeof(in6_pktinfo));
            cm->cmsg_level     = IPPROTO_IPV6;
            cm->cmsg_type      = IPV6_PKTINFO;
            cm->cmsg_len       = CMSG_LEN(sizeof(in6_pktinfo));
            auto        addrin = reinterpret_cast<sockaddr_in6 *>(local_addr.addr);
            in6_pktinfo pktinfo;
            pktinfo.ipi6_addr = addrin->sin6_addr;
            memcpy(CMSG_DATA(cm), &pktinfo, sizeof(pktinfo));

            break;
        }
        default:
            assert(0);
        }

        if (data.size() > gso_size) {
            controllen += CMSG_SPACE(sizeof(uint16_t));
            cm             = CMSG_NXTHDR(&msg, cm);
            cm->cmsg_level = SOL_UDP;
            cm->cmsg_type  = UDP_SEGMENT;
            cm->cmsg_len   = CMSG_LEN(sizeof(uint16_t));
            uint16_t n     = static_cast<std::uint16_t>(gso_size);
            memcpy(CMSG_DATA(cm), &n, sizeof(n));
        }

        controllen += CMSG_SPACE(sizeof(int));
        cm           = CMSG_NXTHDR(&msg, cm);
        cm->cmsg_len = CMSG_LEN(sizeof(int));
        memcpy(CMSG_DATA(cm), &ecn, sizeof(ecn));

        switch (local_addr.addr->sa_family) {
        case AF_INET:
            cm->cmsg_level = IPPROTO_IP;
            cm->cmsg_type  = IP_TOS;

            break;
        case AF_INET6:
            cm->cmsg_level = IPPROTO_IPV6;
            cm->cmsg_type  = IPV6_TCLASS;

            break;
        default:
            assert(0);
        }

        msg.msg_controllen = controllen;

        ssize_t nwrite     = 0;

        do {
            nwrite = sendmsg(ep.fd, &msg, 0);
        } while (nwrite == -1 && errno == EINTR);

        if (nwrite == -1) {
            switch (errno) {
            case EAGAIN:
#if EAGAIN != EWOULDBLOCK
            case EWOULDBLOCK:
#endif // EAGAIN != EWOULDBLOCK
                return { data, NETWORK_ERR_SEND_BLOCKED };
#ifdef UDP_SEGMENT
            case EIO:
                if (data.size() > gso_size) {
                    // GSO failure; send each packet in a separate sendmsg call.
                    HTTP_DBG("sendmsg: disabling GSO due to {}", strerror(errno));
                    no_gso = true;
                    return send_packet(ep, no_gso, local_addr, remote_addr, ecn, data, gso_size);
                }
                break;
#endif // defined(UDP_SEGMENT)
            }

            HTTP_DBG("sendmsg on fd {}: {} ({})", ep.fd, strerror(errno), errno);
            // TODO We have packet which is expected to fail to send (e.g.,
            // path validation to old path).
            return { {}, NETWORK_ERR_OK };
        }

        return { {}, NETWORK_ERR_OK };
    }

    int send_retry(const ngtcp2_pkt_hd *chd, Endpoint &ep, const Address &local_addr, const sockaddr *sa, socklen_t salen, size_t max_pktlen) {
        std::array<char, NI_MAXHOST> host;
        std::array<char, NI_MAXSERV> port;

        if (auto rv = getnameinfo(sa, salen, host.data(), host.size(), port.data(), port.size(), NI_NUMERICHOST | NI_NUMERICSERV); rv != 0) {
            HTTP_DBG("getnameinfo: {}", gai_strerror(rv));
            return -1;
        }

        HTTP_DBG("Server::send_retry: host={} port={}", host.data(), port.data());

        ngtcp2_cid scid;

        scid.datalen = NGTCP2_SV_SCIDLEN;
        if (generate_secure_random({ scid.data, scid.datalen }) != 0) {
            return -1;
        }

        std::array<uint8_t, NGTCP2_CRYPTO_MAX_RETRY_TOKENLEN2> token;

        auto                                                   t        = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

        auto                                                   tokenlen = ngtcp2_crypto_generate_retry_token2(token.data(), _sharedData->_static_secret.data(), _sharedData->_static_secret.size(), chd->version, sa, salen, &scid, &chd->dcid, static_cast<ngtcp2_tstamp>(t));
        if (tokenlen < 0) {
            return -1;
        }

        HTTP_DBG("Generated address validation token");

        Buffer buf{
            std::min(static_cast<size_t>(NGTCP2_MAX_UDP_PAYLOAD_SIZE), max_pktlen)
        };

        auto nwrite = ngtcp2_crypto_write_retry(buf.wpos(), buf.left(), chd->version, &chd->scid, &scid, &chd->dcid, token.data(), static_cast<std::size_t>(tokenlen));
        if (nwrite < 0) {
            HTTP_DBG("ngtcp2_crypto_write_retry: {}", ngtcp2_strerror(static_cast<int>(nwrite)));
            return -1;
        }

        buf.push(static_cast<std::size_t>(nwrite));

        ngtcp2_addr laddr{
            .addr    = const_cast<sockaddr *>(&local_addr.su.sa),
            .addrlen = local_addr.len,
        };
        ngtcp2_addr raddr{
            .addr    = const_cast<sockaddr *>(sa),
            .addrlen = salen,
        };

        if (send_packet(ep, laddr, raddr, /* ecn = */ 0, buf.data()) != NETWORK_ERR_OK) {
            return -1;
        }

        return 0;
    }

    std::optional<ngtcp2_cid> read_pkt(Endpoint &ep, const Address &local_addr, const sockaddr *sa, socklen_t salen, const ngtcp2_pkt_info *pi, std::span<const uint8_t> data) {
        ngtcp2_version_cid vc;

        switch (auto rv = ngtcp2_pkt_decode_version_cid(&vc, data.data(), data.size(), NGTCP2_SV_SCIDLEN); rv) {
        case 0:
            break;
        case NGTCP2_ERR_VERSION_NEGOTIATION:
            send_version_negotiation(vc.version, { vc.scid, vc.scidlen }, { vc.dcid, vc.dcidlen }, ep, local_addr, sa, salen);
            return std::nullopt;
        default:
            HTTP_DBG("Could not decode version and CID from QUIC packet header: {}", ngtcp2_strerror(rv));
            return std::nullopt;
        }

        auto dcid_key   = make_cid_key({ vc.dcid, vc.dcidlen });

        auto handler_it = _h3Sessions.find(dcid_key);
        if (handler_it == std::end(_h3Sessions)) {
            ngtcp2_pkt_hd hd;

            if (auto rv = ngtcp2_accept(&hd, data.data(), data.size()); rv != 0) {
                HTTP_DBG("Unexpected packet received: length={}", data.size());

                if (!(data[0] & 0x80) && data.size() >= NGTCP2_SV_SCIDLEN + 21) {
                    send_stateless_reset(data.size(), { vc.dcid, vc.dcidlen }, ep, local_addr, sa, salen);
                }

                return std::nullopt;
            }

            ngtcp2_cid        ocid;
            ngtcp2_cid       *pocid      = nullptr;
            ngtcp2_token_type token_type = NGTCP2_TOKEN_TYPE_UNKNOWN;

            assert(hd.type == NGTCP2_PKT_INITIAL);

            if (hd.tokenlen) {
                HTTP_DBG("Perform stateless address validation");
                if (hd.tokenlen == 0) {
                    send_retry(&hd, ep, local_addr, sa, salen, data.size() * 3);
                    return std::nullopt;
                }

                if (hd.token[0] != NGTCP2_CRYPTO_TOKEN_MAGIC_RETRY2 && hd.dcid.datalen < NGTCP2_MIN_INITIAL_DCIDLEN) {
                    send_stateless_connection_close(&hd, ep, local_addr, sa, salen);
                    return std::nullopt;
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
                        return std::nullopt;
                    case 1:
                        hd.token    = nullptr;
                        hd.tokenlen = 0;
                        break;
                    }

                    break;
                case NGTCP2_CRYPTO_TOKEN_MAGIC_REGULAR:
                    if (verify_token(&hd, sa, salen) != 0) {
                        hd.token    = nullptr;
                        hd.tokenlen = 0;
                    } else {
                        token_type = NGTCP2_TOKEN_TYPE_NEW_TOKEN;
                    }
                    break;
                default:
                    HTTP_DBG("Ignore unrecognized token");
                    hd.token    = nullptr;
                    hd.tokenlen = 0;
                    break;
                }
            }

            auto h = std::make_shared<Http3Session<RestServer>>(this, _sharedData);
            if (h->init(ep, local_addr, sa, salen, &hd.scid, &hd.dcid, pocid, { hd.token, hd.tokenlen }, token_type, hd.version, _tls_ctx) != 0) {
                return std::nullopt;
            }

            switch (h->on_read(ep, local_addr, sa, salen, pi, data)) {
            case 0:
                break;
            case NETWORK_ERR_RETRY:
                send_retry(&hd, ep, local_addr, sa, salen, data.size() * 3);
                return std::nullopt;
            default:
                return std::nullopt;
            }

            if (h->on_write() != 0) {
                return std::nullopt;
            }

            std::array<ngtcp2_cid, 8> scids;

            auto                      num_scid = ngtcp2_conn_get_scid(h->_conn, nullptr);

            assert(num_scid <= scids.size());

            ngtcp2_conn_get_scid(h->_conn, scids.data());

            for (size_t i = 0; i < num_scid; ++i) {
                associate_cid(scids[i], h);
            }

            _h3Sessions.emplace(dcid_key, std::move(h));

            return dcid_key;
        }

        auto h    = (*handler_it).second;
        auto conn = h->_conn;
        if (ngtcp2_conn_in_closing_period(conn)) {
            if (h->send_conn_close(ep, local_addr, sa, salen, pi, data) != 0) {
                remove(conn);
            }
            return std::nullopt;
        }
        if (ngtcp2_conn_in_draining_period(conn)) {
            return std::nullopt;
        }

        if (auto rv = h->on_read(ep, local_addr, sa, salen, pi, data); rv != 0) {
            if (rv != NETWORK_ERR_CLOSE_WAIT) {
                remove(conn);
            }
            return std::nullopt;
        }

        return dcid_key;
    }

    void associate_cid(const ngtcp2_cid &cid, const std::shared_ptr<Http3Session<RestServer>> &session) {
        _h3Sessions.emplace(cid, session);
    }

    void dissociate_cid(const ngtcp2_cid &cid) {
        _sessionsToRemove.push_back(cid);
    }

    void remove(ngtcp2_conn *conn) {
        dissociate_cid(*ngtcp2_conn_get_client_initial_dcid(conn));

        std::vector<ngtcp2_cid> cids(ngtcp2_conn_get_scid(conn, nullptr));
        ngtcp2_conn_get_scid(conn, cids.data());

        for (const auto &cid : cids) {
            dissociate_cid(cid);
        }
    }
};

} // namespace opencmw::majordomo::detail::rest

#endif // OPENCMW_MAJORDOMO_RESTSERVER_HPP
