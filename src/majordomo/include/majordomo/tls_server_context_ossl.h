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
#ifndef TLS_SERVER_CONTEXT_OSSL_H
#define TLS_SERVER_CONTEXT_OSSL_H

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif // defined(HAVE_CONFIG_H)

#include <openssl/ssl.h>

#include "nghttp2/NgHttp2Utils.hpp"
#include "shared.h"

using namespace ngtcp2;

class TLSServerContext {
public:
    TLSServerContext();
    ~TLSServerContext();

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

    // TODO(Frank) breaks TLS backend abstraction
    int      init(const opencmw::nghttp2::EVP_PKEY_Ptr &key, const opencmw::nghttp2::X509_Ptr &cert,
                 AppProtocol app_proto);
    SSL_CTX *get_native_handle() const;

    void     enable_keylog();

private:
    SSL_CTX *ssl_ctx_;
};

#endif // !defined(TLS_SERVER_CONTEXT_OSSL_H)
