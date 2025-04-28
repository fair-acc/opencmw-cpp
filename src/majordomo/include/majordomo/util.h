/*
 * ngtcp2
 *
 * Copyright (c) 2017 ngtcp2 contributors
 * Copyright (c) 2012 nghttp2 contributors
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
#ifndef UTIL_H
#define UTIL_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif // defined(HAVE_CONFIG_H)

#include <sys/socket.h>

#include <cassert>
#include <chrono>
#include <iostream>
#include <netdb.h>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

#include <ngtcp2/ngtcp2.h>
#include <nghttp3/nghttp3.h>

#include <openssl/rand.h>

#include "network.h"
#include "siphash.h"
#include "template.h"

namespace ngtcp2::util {

inline nghttp3_nv make_nv(const std::string_view &name,
                          const std::string_view &value, uint8_t flags) {
  return nghttp3_nv{
    reinterpret_cast<uint8_t *>(const_cast<char *>(std::data(name))),
    reinterpret_cast<uint8_t *>(const_cast<char *>(std::data(value))),
    name.size(),
    value.size(),
    flags,
  };
}

inline nghttp3_nv make_nv_cc(const std::string_view &name,
                             const std::string_view &value) {
  return make_nv(name, value, NGHTTP3_NV_FLAG_NONE);
}

inline nghttp3_nv make_nv_nc(const std::string_view &name,
                             const std::string_view &value) {
  return make_nv(name, value, NGHTTP3_NV_FLAG_NO_COPY_NAME);
}

inline nghttp3_nv make_nv_nn(const std::string_view &name,
                             const std::string_view &value) {
  return make_nv(name, value,
                 NGHTTP3_NV_FLAG_NO_COPY_NAME | NGHTTP3_NV_FLAG_NO_COPY_VALUE);
}

inline ngtcp2_tstamp timestamp() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count();
}

inline char lowcase(char c) {
  constexpr static unsigned char tbl[] = {
    0,   1,   2,   3,   4,   5,   6,   7,   8,   9,   10,  11,  12,  13,  14,
    15,  16,  17,  18,  19,  20,  21,  22,  23,  24,  25,  26,  27,  28,  29,
    30,  31,  32,  33,  34,  35,  36,  37,  38,  39,  40,  41,  42,  43,  44,
    45,  46,  47,  48,  49,  50,  51,  52,  53,  54,  55,  56,  57,  58,  59,
    60,  61,  62,  63,  64,  'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j',
    'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y',
    'z', 91,  92,  93,  94,  95,  96,  97,  98,  99,  100, 101, 102, 103, 104,
    105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119,
    120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134,
    135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148, 149,
    150, 151, 152, 153, 154, 155, 156, 157, 158, 159, 160, 161, 162, 163, 164,
    165, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175, 176, 177, 178, 179,
    180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191, 192, 193, 194,
    195, 196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207, 208, 209,
    210, 211, 212, 213, 214, 215, 216, 217, 218, 219, 220, 221, 222, 223, 224,
    225, 226, 227, 228, 229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 239,
    240, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254,
    255,
  };
  return tbl[static_cast<unsigned char>(c)];
}

struct CaseCmp {
  bool operator()(char lhs, char rhs) const {
    return lowcase(lhs) == lowcase(rhs);
  }
};

template <typename InputIterator1, typename InputIterator2>
bool istarts_with(InputIterator1 first1, InputIterator1 last1,
                  InputIterator2 first2, InputIterator2 last2) {
  if (last1 - first1 < last2 - first2) {
    return false;
  }
  return std::equal(first2, last2, first1, CaseCmp());
}

template <typename S, typename T> bool istarts_with(const S &a, const T &b) {
  return istarts_with(a.begin(), a.end(), b.begin(), b.end());
}

// make_cid_key returns the key for |cid|.
std::string_view make_cid_key(const ngtcp2_cid *cid);
inline ngtcp2_cid make_cid_key(std::span<const uint8_t> cid) {
    assert(cid.size() <= NGTCP2_MAX_CIDLEN);

    ngtcp2_cid res;

    std::ranges::copy(cid, std::begin(res.data));
    res.datalen = cid.size();

    return res;
}

// straddr stringifies |sa| of length |salen| in a format "[IP]:PORT".
inline std::string straddr(const sockaddr *sa, socklen_t salen) {
    std::array<char, NI_MAXHOST> host;
    std::array<char, NI_MAXSERV> port;

    auto                         rv = getnameinfo(sa, salen, host.data(), host.size(), port.data(),
                                    port.size(), NI_NUMERICHOST | NI_NUMERICSERV);
    if (rv != 0) {
        std::cerr << "getnameinfo: " << gai_strerror(rv) << std::endl;
        return "";
    }
    std::string res = "[";
    res.append(host.data(), strlen(host.data()));
    res += "]:";
    res.append(port.data(), strlen(port.data()));
    return res;
}

namespace {
constexpr char B64_CHARS[] = {
  'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
  'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
  'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
  'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
  '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/',
};
} // namespace

template <typename InputIt> std::string b64encode(InputIt first, InputIt last) {
  std::string res;
  size_t len = last - first;
  if (len == 0) {
    return res;
  }
  size_t r = len % 3;
  res.resize((len + 2) / 3 * 4);
  auto j = last - r;
  auto p = std::begin(res);
  while (first != j) {
    uint32_t n = static_cast<uint8_t>(*first++) << 16;
    n += static_cast<uint8_t>(*first++) << 8;
    n += static_cast<uint8_t>(*first++);
    *p++ = B64_CHARS[n >> 18];
    *p++ = B64_CHARS[(n >> 12) & 0x3fu];
    *p++ = B64_CHARS[(n >> 6) & 0x3fu];
    *p++ = B64_CHARS[n & 0x3fu];
  }

  if (r == 2) {
    uint32_t n = static_cast<uint8_t>(*first++) << 16;
    n += static_cast<uint8_t>(*first++) << 8;
    *p++ = B64_CHARS[n >> 18];
    *p++ = B64_CHARS[(n >> 12) & 0x3fu];
    *p++ = B64_CHARS[(n >> 6) & 0x3fu];
    *p++ = '=';
  } else if (r == 1) {
    uint32_t n = static_cast<uint8_t>(*first++) << 16;
    *p++ = B64_CHARS[n >> 18];
    *p++ = B64_CHARS[(n >> 12) & 0x3fu];
    *p++ = '=';
    *p++ = '=';
  }
  return res;
}

// format_uint converts |n| into string.
template <typename T> std::string format_uint(T n) {
  if (n == 0) {
    return "0";
  }
  size_t nlen = 0;
  for (auto t = n; t; t /= 10, ++nlen)
    ;
  std::string res(nlen, '\0');
  for (; n; n /= 10) {
    res[--nlen] = (n % 10) + '0';
  }
  return res;
}

// format_uint_iec converts |n| into string with the IEC unit (either
// "G", "M", or "K").  It chooses the largest unit which does not drop
// precision.
template <typename T> std::string format_uint_iec(T n) {
  if (n >= (1 << 30) && (n & ((1 << 30) - 1)) == 0) {
    return format_uint(n / (1 << 30)) + 'G';
  }
  if (n >= (1 << 20) && (n & ((1 << 20) - 1)) == 0) {
    return format_uint(n / (1 << 20)) + 'M';
  }
  if (n >= (1 << 10) && (n & ((1 << 10) - 1)) == 0) {
    return format_uint(n / (1 << 10)) + 'K';
  }
  return format_uint(n);
}

// generate_secure_random generates a cryptographically secure pseudo
// random data of |data|.
inline int generate_secure_random(std::span<uint8_t> data) {
    if (RAND_bytes(data.data(), static_cast<int>(data.size())) != 1) {
        return -1;
    }

    return 0;
}

// generate_secret generates secret and writes it to |secret|.
// Currently, |secret| must be 32 bytes long.
inline int generate_secret(std::span<uint8_t> secret) {
    std::array<uint8_t, 16> rand;

    if (generate_secure_random(rand) != 0) {
        return -1;
    }

    auto ctx = EVP_MD_CTX_new();
    if (ctx == nullptr) {
        return -1;
    }

    auto         ctx_deleter = defer(EVP_MD_CTX_free, ctx);

    unsigned int mdlen       = secret.size();
    if (!EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) || !EVP_DigestUpdate(ctx, rand.data(), rand.size()) || !EVP_DigestFinal_ex(ctx, secret.data(), &mdlen)) {
        return -1;
    }

    return 0;
}

constexpr bool is_digit(const char c) { return '0' <= c && c <= '9'; }

constexpr bool is_hex_digit(const char c) {
  return is_digit(c) || ('A' <= c && c <= 'F') || ('a' <= c && c <= 'f');
}

// Returns integer corresponding to hex notation |c|.  If
// is_hex_digit(c) is false, it returns 256.
constexpr uint32_t hex_to_uint(char c) {
  if (c <= '9') {
    return c - '0';
  }
  if (c <= 'Z') {
    return c - 'A' + 10;
  }
  if (c <= 'z') {
    return c - 'a' + 10;
  }
  return 256;
}

template <typename InputIt>
std::string percent_decode(InputIt first, InputIt last) {
  std::string result;
  result.resize(last - first);
  auto p = std::begin(result);
  for (; first != last; ++first) {
    if (*first != '%') {
      *p++ = *first;
      continue;
    }

    if (first + 1 != last && first + 2 != last && is_hex_digit(*(first + 1)) &&
        is_hex_digit(*(first + 2))) {
      *p++ = (hex_to_uint(*(first + 1)) << 4) + hex_to_uint(*(first + 2));
      first += 2;
      continue;
    }

    *p++ = *first;
  }
  result.resize(p - std::begin(result));
  return result;
}

inline int create_nonblock_socket(int domain, int type, int protocol) {
#ifdef SOCK_NONBLOCK
    auto fd = socket(domain, type | SOCK_NONBLOCK, protocol);
    if (fd == -1) {
        return -1;
    }
#else  // !defined(SOCK_NONBLOCK)
    auto fd = socket(domain, type, protocol);
    if (fd == -1) {
        return -1;
    }

    make_socket_nonblocking(fd);
#endif // !defined(SOCK_NONBLOCK)

    return fd;
}

} // namespace ngtcp2::util

namespace std {
template <> struct hash<ngtcp2_cid> {
  hash() {
    assert(0 == ngtcp2::util::generate_secure_random(
                  as_writable_uint8_span(std::span{key})));
  }

  std::size_t operator()(const ngtcp2_cid &cid) const noexcept {
    return static_cast<size_t>(siphash24(key, {cid.data, cid.datalen}));
  }

  std::array<uint64_t, 2> key;
};
} // namespace std

inline bool operator==(const ngtcp2_cid &lhs, const ngtcp2_cid &rhs) {
  return ngtcp2_cid_eq(&lhs, &rhs);
}

#endif // !defined(UTIL_H)
