#ifndef OPENCMW_CPP_RESTCLIENT_NATIVE_HPP
#define OPENCMW_CPP_RESTCLIENT_NATIVE_HPP

#include <algorithm>
#include <memory>
#include <ranges>

#include <ClientContext.hpp>
#include <MdpMessage.hpp>
#include <MIME.hpp>
#include <opencmw.hpp>
#include <ThreadPool.hpp>

#include "RestDefaultClientCertificates.hpp"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wuninitialized"
#pragma GCC diagnostic ignored "-Wuseless-cast"
#include <httplib.h>
#pragma GCC diagnostic pop

namespace opencmw::client {

inline constexpr static const char *LONG_POLLING_IDX_TAG = "LongPollingIdx";

class MinIoThreads {
    const int _minThreads = 1;

public:
    MinIoThreads() = default;
    MinIoThreads(int value) noexcept
        : _minThreads(value){};
    constexpr operator int() const noexcept { return _minThreads; };
};

class MaxIoThreads {
    const int _maxThreads = 10'000;

public:
    MaxIoThreads() = default;
    MaxIoThreads(int value) noexcept
        : _maxThreads(value){};
    constexpr operator int() const noexcept { return _maxThreads; };
};

struct ClientCertificates {
    std::string _certificates;

    ClientCertificates() = default;
    ClientCertificates(const char *X509_ca_bundle) noexcept
        : _certificates(X509_ca_bundle){};
    ClientCertificates(const std::string &X509_ca_bundle) noexcept
        : _certificates(X509_ca_bundle){};
    constexpr operator std::string() const noexcept { return _certificates; };
};

namespace detail {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
inline int readCertificateBundleFromBuffer(X509_STORE &cert_store, const std::string_view &X509_ca_bundle) {
    BIO *cbio = BIO_new_mem_buf(X509_ca_bundle.data(), static_cast<int>(X509_ca_bundle.size()));
    if (!cbio) {
        return -1;
    }
    STACK_OF(X509_INFO) *inf = PEM_X509_INFO_read_bio(cbio, nullptr, nullptr, nullptr);

    if (!inf) {
        BIO_free(cbio); // cleanup
        return -1;
    }
    // iterate over all entries from the pem file, add them to the x509_store one by one
    int count = 0;
    for (int i = 0; i < sk_X509_INFO_num(inf); i++) {
        X509_INFO *itmp = sk_X509_INFO_value(inf, i);
        if (itmp->x509) {
            X509_STORE_add_cert(&cert_store, itmp->x509);
            count++;
        }
        if (itmp->crl) {
            X509_STORE_add_crl(&cert_store, itmp->crl);
            count++;
        }
    }

    sk_X509_INFO_pop_free(inf, X509_INFO_free);
    BIO_free(cbio);
    return count;
}

inline X509_STORE *createCertificateStore(const std::string_view &X509_ca_bundle) {
    X509_STORE *cert_store = X509_STORE_new();
    if (detail::readCertificateBundleFromBuffer(*cert_store, X509_ca_bundle) <= 0) {
        X509_STORE_free(cert_store);
        throw std::invalid_argument(fmt::format("failed to read certificate bundle from buffer:\n#---start---\n{}\n#---end---\n", X509_ca_bundle));
    }
    return cert_store;
}

inline X509 *readServerCertificateFromFile(const std::string_view &X509_ca_bundle) {
    BIO *certBio = BIO_new(BIO_s_mem());
    BIO_write(certBio, X509_ca_bundle.data(), static_cast<int>(X509_ca_bundle.size()));
    X509 *certX509 = PEM_read_bio_X509(certBio, nullptr, nullptr, nullptr);
    BIO_free(certBio);
    if (certX509) {
        return certX509;
    }
    X509_free(certX509);
    throw std::invalid_argument(fmt::format("failed to read certificate from buffer:\n#---start---\n{}\n#---end---\n", X509_ca_bundle));
}

inline EVP_PKEY *readServerPrivateKeyFromFile(const std::string_view &X509_private_key) {
    BIO *certBio = BIO_new(BIO_s_mem());
    BIO_write(certBio, X509_private_key.data(), static_cast<int>(X509_private_key.size()));
    EVP_PKEY *privateKeyX509 = PEM_read_bio_PrivateKey(certBio, nullptr, nullptr, nullptr);
    BIO_free(certBio);
    if (privateKeyX509) {
        return privateKeyX509;
    }
    EVP_PKEY_free(privateKeyX509);
    throw std::invalid_argument(fmt::format("failed to read private key from buffer"));
}

#endif

} // namespace detail

class RestClient : public ClientBase {
    static const httplib::Headers EVT_STREAM_HEADERS;
    using ThreadPoolType = std::shared_ptr<BasicThreadPool<IO_BOUND>>;

    std::string                            _name;
    MIME::MimeType                         _mimeType;
    std::atomic<bool>                      _run = true;
    const int                              _minIoThreads;
    const int                              _maxIoThreads;
    ThreadPoolType                         _thread_pool;
    std::string                            _caCertificate;

    std::mutex                             _subscriptionLock;
    std::map<URI<STRICT>, httplib::Client> _subscription1;
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    X509_STORE                               *_client_cert_store = nullptr;
    std::map<URI<STRICT>, httplib::SSLClient> _subscription2;
#endif

public:
    static bool CHECK_CERTIFICATES;

    /**
     * Initialises a basic RestClient
     *
     * usage example:
     * RestClient client("clientName", DefaultContentTypeHeader(MIME::HTML), MinIoThreads(2), MaxIoThreads(5), ClientCertificates(testCertificate))
     *
     * @tparam Args see argument example above. Order is arbitrary.
     * @param initArgs
     */
    template<typename... Args>
    explicit(false) RestClient(Args... initArgs)
        : _name(detail::find_argument_value<false, std::string>([] { return "RestClient"; }, initArgs...)), //
        _mimeType(detail::find_argument_value<true, DefaultContentTypeHeader>([this] { return MIME::JSON; }, initArgs...))
        , _minIoThreads(detail::find_argument_value<true, MinIoThreads>([] { return MinIoThreads(); }, initArgs...))
        , _maxIoThreads(detail::find_argument_value<true, MaxIoThreads>([] { return MaxIoThreads(); }, initArgs...))
        , _thread_pool(detail::find_argument_value<true, ThreadPoolType>([this] { return std::make_shared<BasicThreadPool<IO_BOUND>>(_name, _minIoThreads, _maxIoThreads); }, initArgs...))
        , _caCertificate(detail::find_argument_value<true, ClientCertificates>([] { return rest::DefaultCertificate().get(); }, initArgs...)) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
        if (_client_cert_store != nullptr) {
            X509_STORE_free(_client_cert_store);
        }
        _client_cert_store = detail::createCertificateStore(_caCertificate);
#endif
    }
    ~RestClient() override { RestClient::stop(); };

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    std::vector<std::string> protocols() noexcept override { return { "http", "https" }; }
#else
    std::vector<std::string> protocols() noexcept override { return { "http" }; }
#endif
    void                         stop() noexcept override { stopAllSubscriptions(); };
    [[nodiscard]] std::string    name() const noexcept { return _name; }
    [[nodiscard]] ThreadPoolType threadPool() const noexcept { return _thread_pool; }
    [[nodiscard]] MIME::MimeType defaultMimeType() const noexcept { return _mimeType; }
    [[nodiscard]] std::string    clientCertificate() const noexcept { return _caCertificate; }

    void                         request(Command cmd) override {
        switch (cmd.command) {
        case mdp::Command::Get:
        case mdp::Command::Set:
            _thread_pool->execute([this, cmd = std::move(cmd)]() mutable { executeCommand(std::move(cmd)); });
            return;
        case mdp::Command::Subscribe:
            _thread_pool->execute([this, cmd = std::move(cmd)]() mutable { startSubscription(std::move(cmd)); });
            return;
        case mdp::Command::Unsubscribe: // deregister existing subscription URI is key
            _thread_pool->execute([this, cmd = std::move(cmd)]() mutable { stopSubscription(cmd); });
            return;
        default:
            throw std::invalid_argument("command type is undefined");
        }
    }

private:
    httplib::Headers getPreferredContentTypeHeader(const URI<STRICT> &uri) const {
        auto mimeType = std::string(_mimeType.typeName());
        if (const auto acceptHeader = uri.queryParamMap().find(detail::ACCEPT_HEADER); acceptHeader != uri.queryParamMap().end() && acceptHeader->second) {
            mimeType = acceptHeader->second->c_str();
        }
        const httplib::Headers headers = { { detail::ACCEPT_HEADER, mimeType }, { detail::CONTENT_TYPE_HEADER, mimeType } };
        return headers;
    }

    static void returnMdpMessage(Command &cmd, const httplib::Result &result, const std::string &errorMsgExt = "") noexcept {
        if (!cmd.callback) {
            return;
        }

        const auto errorMsg = [&]() -> std::optional<std::string> {
            // Result contains a nullptr
            if (!result) {
                return errorMsgExt.empty() ? "Unknown error, empty result" : errorMsgExt;
            }

            // No error
            if (result && result->status >= 200 && result->status < 400 && errorMsgExt.empty()) {
                return {};
            }

            const auto httpError = httplib::detail::status_message(result->status);
            return fmt::format("{} - {}:{}", result->status, httpError, errorMsgExt.empty() ? result->body : errorMsgExt);
        }();

        try {
            cmd.callback(mdp::Message{
                    .id              = 0,
                    .arrivalTime     = std::chrono::system_clock::now(),
                    .protocolName    = cmd.endpoint.scheme().value(),
                    .command         = mdp::Command::Final,
                    .clientRequestID = cmd.clientRequestID,
                    .endpoint        = cmd.endpoint,
                    .data            = errorMsg ? IoBuffer() : IoBuffer(result->body.data(), result->body.size()),
                    .error           = errorMsg.value_or(""),
                    .rbac            = IoBuffer() });
        } catch (const std::exception &e) {
            std::cerr << fmt::format("caught exception '{}' in RestClient::returnMdpMessage(cmd={}, {}: {})", e.what(), cmd.endpoint, result->status, result.value().body) << std::endl;
        } catch (...) {
            std::cerr << fmt::format("caught unknown exception in RestClient::returnMdpMessage(cmd={}, {}: {})", cmd.endpoint, result->status, result.value().body) << std::endl;
        }
    }

    void executeCommand(Command &&cmd) const {
        std::cout << "RestClient::request(" << (cmd.endpoint.str()) << ")" << std::endl;
        auto preferredHeader = getPreferredContentTypeHeader(cmd.endpoint);

        auto endpointBuilder = URI<>::factory(cmd.endpoint);

        if (cmd.command == mdp::Command::Set) {
            preferredHeader.insert(std::make_pair("X-OPENCMW-METHOD"s, "PUT"s));
            endpointBuilder = std::move(endpointBuilder).addQueryParameter("_bodyOverride", std::string(cmd.data.asString()));
        }

        auto endpoint = endpointBuilder.build();

        auto callback = [&cmd, &preferredHeader, &endpoint]<typename ClientType>(ClientType &client) {
            client.set_read_timeout(cmd.timeout); // default keep-alive value
            if (const httplib::Result &result = client.Get(endpoint.relativeRef()->data(), preferredHeader)) {
                returnMdpMessage(cmd, result);
            } else {
                std::stringstream errorStr(fmt::format("\"{}\"", result.error()));
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
                if (auto sslResult = client.get_openssl_verify_result(); sslResult) {
                    errorStr << fmt::format(" - SSL error: '{}'", X509_verify_cert_error_string(sslResult));
                }
#endif
                const std::string errorMsg = fmt::format("GET request failed for: '{}' - {} - CHECK_CERTIFICATES: {}", cmd.endpoint.str(), errorStr.str(), CHECK_CERTIFICATES);
                returnMdpMessage(cmd, result, errorMsg);
            }
        };

        if (cmd.endpoint.scheme() && equal_with_case_ignore(cmd.endpoint.scheme().value(), "https")) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
            httplib::SSLClient client(cmd.endpoint.hostName().value(), cmd.endpoint.port() ? cmd.endpoint.port().value() : 443);
            client.set_ca_cert_store(_client_cert_store);
            client.enable_server_certificate_verification(CHECK_CERTIFICATES);
            callback(client);
#else
            throw std::invalid_argument("https is not supported");
#endif
        } else if (cmd.endpoint.scheme() && equal_with_case_ignore(cmd.endpoint.scheme().value(), "http")) {
            httplib::Client client(cmd.endpoint.hostName().value(), cmd.endpoint.port() ? cmd.endpoint.port().value() : 80);
            callback(client);
            return;
        } else {
            if (cmd.endpoint.scheme()) {
                throw std::invalid_argument(fmt::format("unsupported protocol '{}' for endpoint '{}'", cmd.endpoint.scheme(), cmd.endpoint.str()));
            } else {
                throw std::invalid_argument(fmt::format("no protocol provided for endpoint '{}'", cmd.endpoint.str()));
            }
        }
    }

    bool equal_with_case_ignore(const std::string &a, const std::string &b) const {
        return std::ranges::equal(a, b, [](const char ca, const char cb) noexcept { return ::tolower(ca) == ::tolower(cb); });
    }

    void startSubscription(Command &&cmd) {
        std::scoped_lock lock(_subscriptionLock);
        if (equal_with_case_ignore(*cmd.endpoint.scheme(), "http")
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
                || equal_with_case_ignore(*cmd.endpoint.scheme(), "https")
#endif
        ) {
            auto it = _subscription1.find(cmd.endpoint);
            if (it == _subscription1.end()) {
                auto &client              = _subscription1.try_emplace(cmd.endpoint, httplib::Client(cmd.endpoint.hostName().value(), cmd.endpoint.port().value())).first->second;

                auto  longPollingEndpoint = [&] {
                    if (!cmd.endpoint.queryParamMap().contains(LONG_POLLING_IDX_TAG)) {
                        return URI<>::factory(cmd.endpoint).addQueryParameter(LONG_POLLING_IDX_TAG, "Next").build();
                    } else {
                        return URI<>::factory(cmd.endpoint).build();
                    }
                }();

                const auto pollHeaders = getPreferredContentTypeHeader(longPollingEndpoint);
                auto       endpoint    = longPollingEndpoint.relativeRef().value();
                client.set_read_timeout(cmd.timeout); // default keep-alive value
                while (_run) {
                    auto redirect_get = [&client](auto url, auto headers) {
                        for (;;) {
                            auto result = client.Get(url, headers);
                            if (!result) return result;

                            if (result->status >= 300 && result->status < 400) {
                                url = httplib::detail::decode_url(result.value().get_header_value("location"), true);
                            } else {
                                return result;
                            }
                        }
                    };
                    if (const httplib::Result &result = redirect_get(endpoint, pollHeaders)) {
                        returnMdpMessage(cmd, result);
                    } else {                                      // failed or server is down -> wait until retry
                        std::this_thread::sleep_for(cmd.timeout); // time-out until potential retry
                        if (_run) {
                            returnMdpMessage(cmd, result, fmt::format("Long-Polling-GET request failed for {}: {}", cmd.endpoint.str(), result.error()));
                        }
                    }
                }
            }
        } else {
            throw std::invalid_argument(fmt::format("unsupported scheme '{}' for requested subscription '{}'", cmd.endpoint.scheme(), cmd.endpoint.str()));
        }
    }

    void stopSubscription(const Command &cmd) {
        // stop subscription that matches URI
        std::scoped_lock lock(_subscriptionLock);
        if (equal_with_case_ignore(*cmd.endpoint.scheme(), "http")) {
            auto it = _subscription1.find(cmd.endpoint);
            if (it != _subscription1.end()) {
                it->second.stop();
                _subscription1.erase(it);
                return;
            }
        } else if (equal_with_case_ignore(*cmd.endpoint.scheme(), "https")) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
            auto it = _subscription2.find(cmd.endpoint);
            if (it != _subscription2.end()) {
                it->second.stop();
                _subscription2.erase(it);
                return;
            }
#else
            throw std::runtime_error("https is not supported - enable CPPHTTPLIB_OPENSSL_SUPPORT");
#endif
        } else {
            throw std::invalid_argument(fmt::format("unsupported scheme '{}' for requested subscription '{}'", cmd.endpoint.scheme(), cmd.endpoint.str()));
        }
    }

    void stopAllSubscriptions() noexcept {
        _run = false;
        std::scoped_lock lock(_subscriptionLock);
        std::ranges::for_each(_subscription1, [](auto &pair) { pair.second.stop(); });
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
        std::ranges::for_each(_subscription2, [](auto &pair) { pair.second.stop(); });
#endif
    }
};
inline bool                   RestClient::CHECK_CERTIFICATES = true;
inline const httplib::Headers RestClient::EVT_STREAM_HEADERS = { { detail::ACCEPT_HEADER, MIME::EVENT_STREAM.typeName().data() } };

} // namespace opencmw::client

#endif // OPENCMW_CPP_RESTCLIENT_NATIVE_HPP
