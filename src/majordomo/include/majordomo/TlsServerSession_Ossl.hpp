/*
 * ngtcp2
 *
 * Copyright (c) 2025 ngtcp2 contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef TLS_SERVER_SESSION_OSSL_H
#define TLS_SERVER_SESSION_OSSL_H

#include "NgTcp2Util.hpp"
#include "TlsSessionBase_Ossl.hpp"

#include <ngtcp2/ngtcp2.h>
#include <ngtcp2/ngtcp2_crypto.h>

#include <rest/RestUtils.hpp>

#include <iostream>

namespace opencmw::majordomo::detail::rest {

inline int alpn_select_proto_h3_cb(SSL *ssl, const unsigned char **out, unsigned char *outlen, const unsigned char *in, unsigned int inlen, void * /*arg*/) {
    auto           conn_ref = static_cast<ngtcp2_crypto_conn_ref *>(SSL_get_app_data(ssl));
    auto           conn     = conn_ref->get_conn(conn_ref);
    const uint8_t *alpn;
    size_t         alpnlen;
    // This should be the negotiated version, but we have not set the negotiated version when this callback is called.
    auto version = ngtcp2_conn_get_client_chosen_version(conn);

    switch (version) {
    case NGTCP2_PROTO_VER_V1:
    case NGTCP2_PROTO_VER_V2:
        alpn    = H3_ALPN_V1;
        alpnlen = str_size(H3_ALPN_V1);
        break;
    default:
        std::println(std::cerr, "Unexpected quic protocol version: 0x{:x}", version);
        return SSL_TLSEXT_ERR_ALERT_FATAL;
    }

    for (auto p = in, end = in + inlen; p + alpnlen <= end; p += *p + 1) {
        if (std::equal(alpn, alpn + alpnlen, p)) {
            *out    = p + 1;
            *outlen = *p;
            return SSL_TLSEXT_ERR_OK;
        }
    }

    return SSL_TLSEXT_ERR_ALERT_FATAL;
}

inline int alpn_select_proto_hq_cb(SSL *ssl, const unsigned char **out,
        unsigned char *outlen, const unsigned char *in,
        unsigned int inlen, void * /*arg*/) {
    auto conn_ref = static_cast<ngtcp2_crypto_conn_ref *>(SSL_get_app_data(ssl));
    auto conn     = conn_ref->get_conn(conn_ref);

    // This should be the negotiated version, but we have not set the negotiated version when this callback is called.
    auto           version = ngtcp2_conn_get_client_chosen_version(conn);

    const uint8_t *alpn;
    size_t         alpnlen;

    switch (version) {
    case NGTCP2_PROTO_VER_V1:
    case NGTCP2_PROTO_VER_V2:
        alpn    = HQ_ALPN_V1;
        alpnlen = str_size(HQ_ALPN_V1);
        break;
    default:

        std::println(std::cerr, "Unexpected quic protocol version: 0x{:x}", version);
        return SSL_TLSEXT_ERR_ALERT_FATAL;
    }

    for (auto p = in, end = in + inlen; p + alpnlen <= end; p += *p + 1) {
        if (std::equal(alpn, alpn + alpnlen, p)) {
            *out    = p + 1;
            *outlen = *p;
            return SSL_TLSEXT_ERR_OK;
        }
    }

    HTTP_DBG("Client did not present ALPN");

    return SSL_TLSEXT_ERR_ALERT_FATAL;
}

inline int verify_cb(int /*preverify_ok*/, X509_STORE_CTX *) {
    // We don't verify the client certificate.  Just request it for the testing purpose.
    return 1;
}

inline int gen_ticket_cb(SSL *ssl, void * /*arg*/) {
    auto conn = static_cast<ngtcp2_conn *>(SSL_get_app_data(ssl));
    auto ver  = htonl(ngtcp2_conn_get_negotiated_version(conn));
    if (!SSL_SESSION_set1_ticket_appdata(SSL_get0_session(ssl), &ver,
                sizeof(ver))) {
        return 0;
    }

    return 1;
}

inline SSL_TICKET_RETURN decrypt_ticket_cb(SSL *ssl, SSL_SESSION *session, const unsigned char * /*keyname*/, size_t /*keynamelen*/, SSL_TICKET_STATUS status, void * /*arg*/) {
    switch (status) {
    case SSL_TICKET_EMPTY:
    case SSL_TICKET_NO_DECRYPT:
        return SSL_TICKET_RETURN_IGNORE_RENEW;
    }

    uint8_t *pver;
    uint32_t ver;
    size_t   verlen;

    if (!SSL_SESSION_get0_ticket_appdata(
                session, reinterpret_cast<void **>(&pver), &verlen)
            || verlen != sizeof(ver)) {
        switch (status) {
        case SSL_TICKET_SUCCESS:
            return SSL_TICKET_RETURN_IGNORE;
        case SSL_TICKET_SUCCESS_RENEW:
        default:
            return SSL_TICKET_RETURN_IGNORE_RENEW;
        }
    }

    memcpy(&ver, pver, sizeof(ver));
    auto conn_ref = static_cast<ngtcp2_crypto_conn_ref *>(SSL_get_app_data(ssl));
    auto conn     = conn_ref->get_conn(conn_ref);

    if (ngtcp2_conn_get_client_chosen_version(conn) != ntohl(ver)) {
        switch (status) {
        case SSL_TICKET_SUCCESS:
            return SSL_TICKET_RETURN_IGNORE;
        case SSL_TICKET_SUCCESS_RENEW:
        default:
            return SSL_TICKET_RETURN_IGNORE_RENEW;
        }
    }

    switch (status) {
    case SSL_TICKET_SUCCESS:
        return SSL_TICKET_RETURN_USE;
    case SSL_TICKET_SUCCESS_RENEW:
    default:
        return SSL_TICKET_RETURN_USE_RENEW;
    }
}

class TLSServerContext {
public:
    TLSServerContext()
        : ssl_ctx_{ nullptr } {}

    ~TLSServerContext() {
        if (ssl_ctx_) {
            SSL_CTX_free(ssl_ctx_);
        }
    }

    TLSServerContext(const TLSServerContext &)            = delete;
    TLSServerContext &operator=(const TLSServerContext &) = delete;
    TLSServerContext(TLSServerContext &&other) noexcept
        : ssl_ctx_{ std::exchange(other.ssl_ctx_, nullptr) } {}
    TLSServerContext &operator=(TLSServerContext &&other) noexcept {
        if (this != &other) {
            if (ssl_ctx_) {
                SSL_CTX_free(ssl_ctx_);
            }
            ssl_ctx_ = std::exchange(other.ssl_ctx_, nullptr);
        }
        return *this;
    }

    int init(const opencmw::rest::detail::EVP_PKEY_Ptr &key, const opencmw::rest::detail::X509_Ptr &cert, AppProtocol app_proto) {
        constexpr static unsigned char sid_ctx[] = "ngtcp2 server";

        ssl_ctx_                                 = SSL_CTX_new(TLS_server_method());
        if (!ssl_ctx_) {
            std::cerr << "SSL_CTX_new: " << ERR_error_string(ERR_get_error(), nullptr)
                      << std::endl;
            return -1;
        }

        SSL_CTX_set_max_early_data(ssl_ctx_, UINT32_MAX);

        constexpr auto ssl_opts = (SSL_OP_ALL & ~SSL_OP_DONT_INSERT_EMPTY_FRAGMENTS) | SSL_OP_SINGLE_ECDH_USE | SSL_OP_CIPHER_SERVER_PREFERENCE | SSL_OP_NO_ANTI_REPLAY;

        SSL_CTX_set_options(ssl_ctx_, ssl_opts);

        SSL_CTX_set_mode(ssl_ctx_, SSL_MODE_RELEASE_BUFFERS);

        switch (app_proto) {
        case AppProtocol::H3:
            SSL_CTX_set_alpn_select_cb(ssl_ctx_, alpn_select_proto_h3_cb, nullptr);
            break;
        case AppProtocol::HQ:
            SSL_CTX_set_alpn_select_cb(ssl_ctx_, alpn_select_proto_hq_cb, nullptr);
            break;
        }

        SSL_CTX_set_default_verify_paths(ssl_ctx_);

        if (SSL_CTX_use_PrivateKey(ssl_ctx_, key.get())
                != 1) {
            std::cerr << "SSL_CTX_use_PrivateKey_file: " << ERR_error_string(ERR_get_error(), nullptr) << std::endl;
            return -1;
        }

        if (SSL_CTX_use_certificate(ssl_ctx_, cert.get()) != 1) {
            std::cerr << "SSL_CTX_use_certificate_chain_file: " << ERR_error_string(ERR_get_error(), nullptr) << std::endl;
            return -1;
        }

        if (SSL_CTX_check_private_key(ssl_ctx_) != 1) {
            std::cerr << "SSL_CTX_check_private_key: " << ERR_error_string(ERR_get_error(), nullptr) << std::endl;
            return -1;
        }

        SSL_CTX_set_session_id_context(ssl_ctx_, sid_ctx, sizeof(sid_ctx) - 1);
        SSL_CTX_set_session_ticket_cb(ssl_ctx_, gen_ticket_cb, decrypt_ticket_cb, nullptr);

        return 0;
    }

    SSL_CTX *get_native_handle() const {
        return ssl_ctx_;
    }

    void enable_keylog() {
    }

private:
    SSL_CTX *ssl_ctx_;
};

class TLSServerSession : public TLSSessionBase {
public:
    int init(const TLSServerContext &tls_ctx, ngtcp2_crypto_conn_ref *conn_ref) {
        auto ssl_ctx = tls_ctx.get_native_handle();

        auto ssl     = SSL_new(ssl_ctx);
        if (!ssl) {
            std::cerr << "SSL_new: " << ERR_error_string(ERR_get_error(), nullptr) << std::endl;
            return -1;
        }

        ngtcp2_crypto_ossl_ctx_set_ssl(ossl_ctx_, ssl);

        if (ngtcp2_crypto_ossl_configure_server_session(ssl) != 0) {
            std::cerr << "ngtcp2_crypto_ossl_configure_server_session failed" << std::endl;
            return -1;
        }
        SSL_set_app_data(ssl, conn_ref);
        SSL_set_accept_state(ssl);
        SSL_set_quic_tls_early_data_enabled(ssl, 1);

        return 0;
    }

    // ticket is sent automatically.
    int send_session_ticket() { return 0; }
};

} // namespace opencmw::majordomo::detail::rest

#endif // TLS_SERVER_SESSION_OSSL_H
