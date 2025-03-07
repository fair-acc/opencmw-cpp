#ifndef OPENCMW_NGHTTP2HELPERS_H
#define OPENCMW_NGHTTP2HELPERS_H

#include <arpa/inet.h>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <expected>
#include <fcntl.h>
#include <filesystem>
#include <memory>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <openssl/pem.h>
#include <span>
#include <string_view>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <openssl/bio.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

#include <nghttp2/nghttp2.h>

#include <fmt/format.h>

#ifdef OPENCMW_DEBUG_HTTP
#include <iostream>
#define HTTP_DBG(...) fmt::println(std::cerr, __VA_ARGS__);
#else
#define HTTP_DBG(...)
#endif

namespace opencmw::nghttp2 {
using SSL_CTX_Ptr    = std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)>;
using SSL_Ptr        = std::unique_ptr<SSL, decltype(&SSL_free)>;
using X509_STORE_Ptr = std::unique_ptr<X509_STORE, decltype(&X509_STORE_free)>;
using X509_Ptr       = std::unique_ptr<X509, decltype(&X509_free)>;
using EVP_PKEY_Ptr   = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;

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

inline std::expected<X509_STORE_Ptr, std::string> createCertificateStore(std::string_view x509_ca_bundle) {
    X509_STORE_Ptr cert_store = X509_STORE_Ptr(X509_STORE_new(), X509_STORE_free);
    if (readCertificateBundleFromBuffer(*cert_store.get(), x509_ca_bundle) <= 0) {
        return std::unexpected(fmt::format("failed to read certificate bundle from buffer:\n#---start---\n{}\n#---end---\n", x509_ca_bundle));
    }
    return cert_store;
}

inline std::expected<X509_Ptr, std::string> readServerCertificateFromBuffer(std::string_view X509_ca_bundle) {
    BIO *certBio = BIO_new(BIO_s_mem());
    BIO_write(certBio, X509_ca_bundle.data(), static_cast<int>(X509_ca_bundle.size()));
    auto certX509 = X509_Ptr(PEM_read_bio_X509(certBio, nullptr, nullptr, nullptr), X509_free);
    BIO_free(certBio);
    if (!certX509) {
        return std::unexpected(fmt::format("failed to read certificate from buffer:\n#---start---\n{}\n#---end---\n", X509_ca_bundle));
    }
    return certX509;
}

inline std::expected<X509_Ptr, std::string> readServerCertificateFromFile(std::filesystem::path fpath) {
    auto path = fpath.string();
    BIO *certBio
            = BIO_new_file(path.data(), "r");
    if (!certBio) {
        return std::unexpected(fmt::format("failed to read certificate from file {}: {}", path, ERR_error_string(ERR_get_error(), nullptr)));
    }
    auto certX509 = X509_Ptr(PEM_read_bio_X509(certBio, nullptr, nullptr, nullptr), X509_free);
    BIO_free(certBio);
    if (!certX509) {
        return std::unexpected(fmt::format("failed to read certificate key from file: {}", path));
    }
    return certX509;
}

inline std::expected<EVP_PKEY_Ptr, std::string> readServerPrivateKeyFromBuffer(std::string_view x509_private_key) {
    BIO *certBio = BIO_new(BIO_s_mem());
    BIO_write(certBio, x509_private_key.data(), static_cast<int>(x509_private_key.size()));
    EVP_PKEY_Ptr privateKeyX509 = EVP_PKEY_Ptr(PEM_read_bio_PrivateKey(certBio, nullptr, nullptr, nullptr), EVP_PKEY_free);
    BIO_free(certBio);
    if (!privateKeyX509) {
        return std::unexpected(fmt::format("failed to read private key from buffer"));
    }
    return privateKeyX509;
}

inline std::expected<EVP_PKEY_Ptr, std::string> readServerPrivateKeyFromFile(std::filesystem::path fpath) {
    auto path    = fpath.string();
    BIO *certBio = BIO_new_file(path.data(), "r");
    if (!certBio) {
        return std::unexpected(fmt::format("failed to read private key from file {}: {}", path, ERR_error_string(ERR_get_error(), nullptr)));
    }
    EVP_PKEY_Ptr privateKeyX509 = EVP_PKEY_Ptr(PEM_read_bio_PrivateKey(certBio, nullptr, nullptr, nullptr), EVP_PKEY_free);
    BIO_free(certBio);
    if (!privateKeyX509) {
        return std::unexpected(fmt::format("failed to read private key from file: {}", path));
    }
    return privateKeyX509;
}
namespace detail {

inline std::expected<SSL_Ptr, std::string> create_ssl(SSL_CTX *ssl_ctx) {
    auto ssl = SSL_Ptr(SSL_new(ssl_ctx), SSL_free);
    if (!ssl) {
        return std::unexpected(fmt::format("Could not create SSL/TLS session object: {}", ERR_error_string(ERR_get_error(), nullptr)));
    }
    return ssl;
}

inline std::chrono::nanoseconds timestamp() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch());
}

constexpr std::span<uint8_t> u8span(std::string_view view) {
    return { const_cast<uint8_t *>(reinterpret_cast<const uint8_t *>(view.data())), view.size() };
}

constexpr nghttp2_nv nv(const std::span<uint8_t> &name, const std::span<uint8_t> &value, uint8_t flags = NGHTTP2_NV_FLAG_NO_COPY_NAME) {
    return { name.data(), value.data(), name.size(), value.size(), flags };
}

#ifdef OPENCMW_PROFILE_HTTP
inline std::chrono::nanoseconds latency(const std::string_view &value) {
    const auto     now    = std::chrono::high_resolution_clock::now();
    const uint64_t ts     = std::stoull(std::string(value));
    const auto     parsed = std::chrono::time_point<std::chrono::high_resolution_clock>(std::chrono::nanoseconds(ts));
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now - parsed);
}
#endif

constexpr std::string_view as_view(nghttp2_rcbuf *rcbuf) {
    auto vec = nghttp2_rcbuf_get_buf(rcbuf);
    return { reinterpret_cast<char *>(vec.base), vec.len };
}

// Convenience/RAII wrapper for a non-blocking/no-delay TCP socket that can be used with SSL or without.
struct TcpSocket {
    using AddrinfoPtr = std::unique_ptr<addrinfo, decltype(&freeaddrinfo)>;

    enum Flags {
        None       = 0x0,
        VerifyPeer = 0x1,
    };

    enum State {
        Uninitialized,
        Connecting,
        SSLConnectWantsRead,
        SSLConnectWantsWrite,
        SSLAcceptWantsRead,
        SSLAcceptWantsWrite,
        Connected
    };

    int     fd     = -1;
    int     flags  = None;
    SSL_Ptr _ssl   = SSL_Ptr(nullptr, SSL_free);
    State   _state = Uninitialized;
    AddrinfoPtr address = AddrinfoPtr(nullptr, freeaddrinfo);

    TcpSocket()
            = default;
    explicit TcpSocket(SSL_Ptr ssl_, int fd_, int flags_ = VerifyPeer)
        : fd(fd_), flags(flags_), _ssl(std::move(ssl_)) {
        int f = fcntl(fd, F_GETFL, 0);
        assert(f != -1);
        fcntl(fd, F_SETFL, f | O_NONBLOCK);

        int flag = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

        if (_ssl) {
            auto bio = BIO_new_socket(fd, BIO_NOCLOSE);
            if (!bio) {
                // TODO(Frank) do not throw here
                throw std::runtime_error("Failed to create BIO object");
            }
            // SSL will take ownership of the BIO object
            SSL_set_bio(_ssl.get(), bio, bio);
        }
    }

    TcpSocket(const TcpSocket &)            = delete;
    TcpSocket &operator=(const TcpSocket &) = delete;
    TcpSocket(TcpSocket &&other) noexcept {
        close();
        std::swap(fd, other.fd);
        std::swap(flags, other.flags);
        std::swap(_state, other._state);
        _ssl = std::move(other._ssl);
    }
    TcpSocket &operator=(TcpSocket &&other) noexcept {
        if (this != &other) {
            close();
            std::swap(fd, other.fd);
            std::swap(flags, other.flags);
            std::swap(_state, other._state);
            _ssl = std::move(other._ssl);
        }
        return *this;
    }

    ~TcpSocket() {
        close();
    }

    std::string lastError() {
        if (_ssl) {
            return ERR_error_string(ERR_get_error(), nullptr);
        } else {
            return strerror(errno);
        }
    }

    std::expected<State, std::string> continueHandshake() {
        switch (_state) {
        case Uninitialized:
            return std::unexpected("Socket not initialized");
        case SSLConnectWantsRead:
        case SSLConnectWantsWrite: {
            int ret = SSL_connect(_ssl.get());
            if (ret == 1) {
                _state = Connected;
                return _state;
            }
            if (ret == 0) {
                return std::unexpected(fmt::format("SSL handshake failed: {}", ERR_error_string(ERR_get_error(), nullptr)));
            }
            int err = SSL_get_error(_ssl.get(), ret);
            if (err == SSL_ERROR_WANT_READ) {
                _state = SSLConnectWantsRead;
                return _state;
            }
            if (err == SSL_ERROR_WANT_WRITE) {
                _state = SSLConnectWantsWrite;
                return _state;
            }
            return std::unexpected(fmt::format("SSL handshake failed: {}", ERR_error_string(ERR_get_error(), nullptr)));
        }
        case SSLAcceptWantsRead:
        case SSLAcceptWantsWrite: {
            int ret = SSL_accept(_ssl.get());
            if (ret == 1) {
                _state = Connected;
                return _state;
            }
            if (ret == 0) {
                return std::unexpected(fmt::format("SSL handshake failed: {}", ERR_error_string(ERR_get_error(), nullptr)));
            }
            int err = SSL_get_error(_ssl.get(), ret);
            if (err == SSL_ERROR_WANT_READ) {
                _state = SSLAcceptWantsRead;
                return _state;
            }
            if (err == SSL_ERROR_WANT_WRITE) {
                _state = SSLAcceptWantsWrite;
                return _state;
            }
            return std::unexpected(fmt::format("SSL handshake failed: {}", ERR_error_string(ERR_get_error(), nullptr)));
        }
        case Connecting:
        case Connected:
            return _state;
        }

        return _state;
    }

    std::expected<void, std::string> prepareConnect(std::string_view host, uint16_t port) {
        struct addrinfo hints;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family   = AF_INET;     // IPv4
        hints.ai_socktype = SOCK_STREAM; // TCP

        struct addrinfo *res;
        int              status = getaddrinfo(host.data(), nullptr, &hints, &res);
        if (status != 0) {
            return std::unexpected(fmt::format("Could not resolve address: {}", strerror(status)));
        }
        address                                                            = AddrinfoPtr(res, freeaddrinfo);
        reinterpret_cast<struct sockaddr_in *>(address->ai_addr)->sin_port = htons(port);

        if (_ssl && (flags & VerifyPeer) != 0) {
            // Use instead of SSL_set_tlsext_host_name() to avoid warning about (void*) cast
            if (auto r = SSL_ctrl(_ssl.get(), SSL_CTRL_SET_TLSEXT_HOSTNAME, TLSEXT_NAMETYPE_host_name, const_cast<char *>(host.data())); r != SSL_TLSEXT_ERR_OK && r != SSL_TLSEXT_ERR_ALERT_WARNING) {
                return std::unexpected(fmt::format("Failed to set the TLS SNI hostname: {}", ERR_error_string(ERR_get_error(), nullptr)));
            }
            SSL_set_verify(_ssl.get(), SSL_VERIFY_PEER, nullptr);
        }

        _state = Connecting;
        return {};
    }

    std::expected<void, std::string> connect() {
        assert(address);
        if (::connect(fd, address.get()->ai_addr, sizeof(sockaddr)) < 0) {
            if (errno == EINPROGRESS) {
                return {};
            }

            return std::unexpected(fmt::format("Connect failed: {}", strerror(errno)));
        }

        if (!_ssl) {
            _state = Connected;
            return {};
        }

        _state = SSLConnectWantsWrite;
        if (auto r = continueHandshake(); !r) {
            return std::unexpected(r.error());
        }
        return {};
    }

    std::expected<std::optional<TcpSocket>, std::string> accept(SSL_CTX *ssl_ctx, int flags_) {
        struct sockaddr_in client_addr;
        socklen_t          client_len = sizeof(client_addr);
        int                client_fd  = ::accept(fd, static_cast<struct sockaddr *>(static_cast<void *>(&client_addr)), &client_len);
        if (client_fd < 0) {
            if (errno == EAGAIN) {
                return {};
            }
            return std::unexpected(fmt::format("Accept failed: {}", strerror(errno)));
        }

        if (!ssl_ctx) {
            auto socket   = TcpSocket{ { nullptr, SSL_free }, client_fd };
            socket._state = Connected;
            return socket;
        }

        auto ssl = create_ssl(ssl_ctx);
        if (!ssl) {
            ::close(client_fd);
            return std::unexpected(fmt::format("Failed to create SSL object: {}", ssl.error()));
        }

        auto clientSocket   = TcpSocket(std::move(ssl.value()), client_fd, flags_);
        clientSocket._state = SSLAcceptWantsRead;
        return clientSocket;
    }

    ssize_t read(uint8_t *data, std::size_t length) {
        if (_ssl) {
            return SSL_read(_ssl.get(), data, static_cast<int>(length));
        } else {
            return ::recv(fd, data, length, 0);
        }
    }

    ssize_t write(const uint8_t *data, std::size_t length, int wflags = 0) {
        if (_ssl) {
            return SSL_write(_ssl.get(), data, static_cast<int>(length));
        } else {
            return ::send(fd, data, length, wflags);
        }
    }

    void close() {
        if (_ssl) {
            SSL_shutdown(_ssl.get());
            _ssl.reset();
        }
        if (fd != -1) {
            ::close(fd);
            fd = -1;
        }
    }
};

template<std::size_t Limit, std::size_t InitialCapacity = Limit * 2>
struct WriteBuffer {
    std::vector<uint8_t> buffer = std::vector<uint8_t>(InitialCapacity);
    std::size_t          size   = 0;

    bool                 hasData() const {
        return size > 0;
    }

    bool wantsToWrite(nghttp2_session *session) const {
        return size > 0 || nghttp2_session_want_write(session);
    }

    bool write(nghttp2_session *session, TcpSocket &socket) {
        const uint8_t *chunk;

        while (size < Limit && nghttp2_session_want_write(session)) {
            nghttp2_ssize len = nghttp2_session_mem_send2(session, &chunk);
            if (len < 0) {
                break; // out of memory, try again later
            }
            if (len == 0) {
                continue;
            }
            const std::size_t newSize = size + static_cast<std::size_t>(len);
            if (newSize > buffer.size()) {
                HTTP_DBG("Resizing buffer from {} to {}", buffer.size(), newSize);
                buffer.resize(newSize);
            }

            std::copy(chunk, chunk + len, buffer.data() + size);
            size += static_cast<std::size_t>(len);
        }

        std::size_t written = 0;
        while (size - written > 0) {
            const ssize_t n = socket.write(buffer.data() + written, size - written);
            if (n == 0 && errno == EAGAIN) {
                break;
            }
            if (n <= 0) {
                return false;
            }
            written += static_cast<std::size_t>(n);
        }
        HTTP_DBG("Write[{}]: Wrote {} bytes", socket.fd, written);
        size -= written;
        // TODO could be optimized by using a circular buffer
        std::move(buffer.data() + written, buffer.data() + size, buffer.data());
        return true;
    }
};

} // namespace detail

} // namespace opencmw::nghttp2
#endif
