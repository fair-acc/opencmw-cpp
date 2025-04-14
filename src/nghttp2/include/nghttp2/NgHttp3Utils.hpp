#ifndef NGHTTP3_UTILS_H
#define NGHTTP3_UTILS_H

#include <arpa/inet.h>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <functional>
#include <iostream>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <unistd.h>

#include <openssl/crypto.h>
#include <openssl/rand.h>

namespace opencmw::nghttp3 {

// Code copied from ngtcp2 server example
// TODO(FRANK) cleanup, refactor
union sockaddr_union {
    sockaddr_storage storage;
    sockaddr         sa;
    sockaddr_in6     in6;
    sockaddr_in      in;
};

struct Address {
    socklen_t            len;
    union sockaddr_union su;
    uint32_t             ifindex;
};

// inspired by <http://blog.korfuri.fr/post/go-defer-in-cpp/>, but our
// template can take functions returning other than void.
template<typename F, typename... T>
struct Defer {
    Defer(F &&f, T &&...t)
        : f(std::bind(std::forward<F>(f), std::forward<T>(t)...)) {}
    Defer(Defer &&o) noexcept
        : f(std::move(o.f)) {}
    ~Defer() { f(); }

    using ResultType = std::invoke_result_t<F, T...>;
    std::function<ResultType()> f;
};

template<typename F, typename... T>
Defer<F, T...> defer(F &&f, T &&...t) {
    return Defer<F, T...>(std::forward<F>(f), std::forward<T>(t)...);
}

int generate_secure_random(std::span<uint8_t> data) {
    if (RAND_bytes(data.data(), static_cast<int>(data.size())) != 1) {
        return -1;
    }

    return 0;
}

// generate_secret generates secret and writes it to |secret|.
// Currently, |secret| must be 32 bytes long.
int generate_secret(std::span<uint8_t> secret) {
    std::array<uint8_t, 16> rand;

    if (generate_secure_random(rand) != 0) {
        return -1;
    }

    auto ctx = EVP_MD_CTX_new();
    if (ctx == nullptr) {
        return -1;
    }

    auto         ctx_deleter = defer(EVP_MD_CTX_free, ctx);

    unsigned int mdlen       = static_cast<unsigned int>(secret.size());
    if (!EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) || !EVP_DigestUpdate(ctx, rand.data(), rand.size()) || !EVP_DigestFinal_ex(ctx, secret.data(), &mdlen)) {
        return -1;
    }

    return 0;
}

inline void fd_set_recv_ecn(int fd, int family) {
    unsigned int tos = 1;
    switch (family) {
    case AF_INET:
        if (setsockopt(fd, IPPROTO_IP, IP_RECVTOS, &tos,
                    static_cast<socklen_t>(sizeof(tos)))
                == -1) {
            std::cerr << "setsockopt: " << strerror(errno) << std::endl;
        }
        break;
    case AF_INET6:
        if (setsockopt(fd, IPPROTO_IPV6, IPV6_RECVTCLASS, &tos,
                    static_cast<socklen_t>(sizeof(tos)))
                == -1) {
            std::cerr << "setsockopt: " << strerror(errno) << std::endl;
        }
        break;
    }
}

inline void fd_set_ip_mtu_discover(int fd, int family) {
#if defined(IP_MTU_DISCOVER) && defined(IPV6_MTU_DISCOVER)
    int val;

    switch (family) {
    case AF_INET:
        val = IP_PMTUDISC_DO;
        if (setsockopt(fd, IPPROTO_IP, IP_MTU_DISCOVER, &val,
                    static_cast<socklen_t>(sizeof(val)))
                == -1) {
            std::cerr << "setsockopt: IP_MTU_DISCOVER: " << strerror(errno)
                      << std::endl;
        }
        break;
    case AF_INET6:
        val = IPV6_PMTUDISC_DO;
        if (setsockopt(fd, IPPROTO_IPV6, IPV6_MTU_DISCOVER, &val,
                    static_cast<socklen_t>(sizeof(val)))
                == -1) {
            std::cerr << "setsockopt: IPV6_MTU_DISCOVER: " << strerror(errno)
                      << std::endl;
        }
        break;
    }
#endif // defined(IP_MTU_DISCOVER) && defined(IPV6_MTU_DISCOVER)
}

inline void fd_set_ip_dontfrag(int fd, int family) {
#if defined(IP_DONTFRAG) && defined(IPV6_DONTFRAG)
    int val = 1;

    switch (family) {
    case AF_INET:
        if (setsockopt(fd, IPPROTO_IP, IP_DONTFRAG, &val,
                    static_cast<socklen_t>(sizeof(val)))
                == -1) {
            std::cerr << "setsockopt: IP_DONTFRAG: " << strerror(errno) << std::endl;
        }
        break;
    case AF_INET6:
        if (setsockopt(fd, IPPROTO_IPV6, IPV6_DONTFRAG, &val,
                    static_cast<socklen_t>(sizeof(val)))
                == -1) {
            std::cerr << "setsockopt: IPV6_DONTFRAG: " << strerror(errno)
                      << std::endl;
        }
        break;
    }
#endif // defined(IP_DONTFRAG) && defined(IPV6_DONTFRAG)
}

inline int make_socket_nonblocking(int fd) {
    int rv;
    int flags;

    while ((flags = fcntl(fd, F_GETFL, 0)) == -1 && errno == EINTR);
    if (flags == -1) {
        return -1;
    }

    while ((rv = fcntl(fd, F_SETFL, flags | O_NONBLOCK)) == -1 && errno == EINTR);

    return rv;
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

inline int create_sock(Address &local_addr, const char *addr, const char *port,
        int family) {
    addrinfo hints{
        .ai_flags    = AI_PASSIVE,
        .ai_family   = family,
        .ai_socktype = SOCK_DGRAM,
    };
    addrinfo *res, *rp;
    int       val = 1;

    if (strcmp(addr, "*") == 0) {
        addr = nullptr;
    }

    if (auto rv = getaddrinfo(addr, port, &hints, &res); rv != 0) {
        std::cerr << "getaddrinfo: " << gai_strerror(rv) << std::endl;
        return -1;
    }

    auto res_d = defer(freeaddrinfo, res);

    int  fd    = -1;

    for (rp = res; rp; rp = rp->ai_next) {
        fd = create_nonblock_socket(rp->ai_family, rp->ai_socktype,
                rp->ai_protocol);
        if (fd == -1) {
            continue;
        }

        if (rp->ai_family == AF_INET6) {
            if (setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &val,
                        static_cast<socklen_t>(sizeof(val)))
                    == -1) {
                ::close(fd);
                continue;
            }

            if (setsockopt(fd, IPPROTO_IPV6, IPV6_RECVPKTINFO, &val,
                        static_cast<socklen_t>(sizeof(val)))
                    == -1) {
                ::close(fd);
                continue;
            }
        } else if (setsockopt(fd, IPPROTO_IP, IP_PKTINFO, &val,
                           static_cast<socklen_t>(sizeof(val)))
                   == -1) {
            ::close(fd);
            continue;
        }

        if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val,
                    static_cast<socklen_t>(sizeof(val)))
                == -1) {
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
        std::cerr << "Could not bind" << std::endl;
        return -1;
    }

    socklen_t len = sizeof(local_addr.su.storage);
    if (getsockname(fd, &local_addr.su.sa, &len) == -1) {
        std::cerr << "getsockname: " << strerror(errno) << std::endl;
        ::close(fd);
        return -1;
    }
    local_addr.len     = len;
    local_addr.ifindex = 0;

    return fd;
}

inline unsigned int msghdr_get_ecn(msghdr *msg, int family) {
    switch (family) {
    case AF_INET:
        for (auto cmsg = CMSG_FIRSTHDR(msg); cmsg; cmsg = CMSG_NXTHDR(msg, cmsg)) {
            if (cmsg->cmsg_level == IPPROTO_IP &&
#ifdef __APPLE__
                    cmsg->cmsg_type == IP_RECVTOS
#else  // !defined(__APPLE__)
                    cmsg->cmsg_type == IP_TOS
#endif // !defined(__APPLE__)
                    && cmsg->cmsg_len) {
                return *reinterpret_cast<uint8_t *>(CMSG_DATA(cmsg)) & IPTOS_ECN_MASK;
            }
        }
        break;
    case AF_INET6:
        for (auto cmsg = CMSG_FIRSTHDR(msg); cmsg; cmsg = CMSG_NXTHDR(msg, cmsg)) {
            if (cmsg->cmsg_level == IPPROTO_IPV6 && cmsg->cmsg_type == IPV6_TCLASS && cmsg->cmsg_len) {
                unsigned int tos;

                memcpy(&tos, CMSG_DATA(cmsg), sizeof(int));

                return tos & IPTOS_ECN_MASK;
            }
        }
        break;
    }

    return 0;
}

inline std::optional<Address> msghdr_get_local_addr(msghdr *msg, int family) {
    switch (family) {
    case AF_INET:
        for (auto cmsg = CMSG_FIRSTHDR(msg); cmsg; cmsg = CMSG_NXTHDR(msg, cmsg)) {
            if (cmsg->cmsg_level == IPPROTO_IP && cmsg->cmsg_type == IP_PKTINFO) {
                in_pktinfo pktinfo;
                memcpy(&pktinfo, CMSG_DATA(cmsg), sizeof(pktinfo));
                Address res{
                    .len     = sizeof(res.su.in),
                    .ifindex = static_cast<uint32_t>(pktinfo.ipi_ifindex),
                };
                auto &sa      = res.su.in;
                sa.sin_family = AF_INET;
                sa.sin_addr   = pktinfo.ipi_addr;
                return res;
            }
        }
        return {};
    case AF_INET6:
        for (auto cmsg = CMSG_FIRSTHDR(msg); cmsg; cmsg = CMSG_NXTHDR(msg, cmsg)) {
            if (cmsg->cmsg_level == IPPROTO_IPV6 && cmsg->cmsg_type == IPV6_PKTINFO) {
                in6_pktinfo pktinfo;
                memcpy(&pktinfo, CMSG_DATA(cmsg), sizeof(pktinfo));
                Address res{
                    .len     = sizeof(res.su.in6),
                    .ifindex = static_cast<uint32_t>(pktinfo.ipi6_ifindex),
                };
                auto &sa       = res.su.in6;
                sa.sin6_family = AF_INET6;
                sa.sin6_addr   = pktinfo.ipi6_addr;
                return res;
            }
        }
        return {};
    }
    return {};
}

inline size_t msghdr_get_udp_gro(msghdr *msg) {
    int gso_size = 0;

#ifdef UDP_GRO
    for (auto cmsg = CMSG_FIRSTHDR(msg); cmsg; cmsg = CMSG_NXTHDR(msg, cmsg)) {
        if (cmsg->cmsg_level == SOL_UDP && cmsg->cmsg_type == UDP_GRO) {
            memcpy(&gso_size, CMSG_DATA(cmsg), sizeof(gso_size));

            break;
        }
    }
#endif // defined(UDP_GRO)

    return static_cast<size_t>(gso_size);
}

inline void set_port(Address &dst, Address &src) {
    switch (dst.su.storage.ss_family) {
    case AF_INET:
        assert(AF_INET == src.su.storage.ss_family);
        dst.su.in.sin_port = src.su.in.sin_port;
        return;
    case AF_INET6:
        assert(AF_INET6 == src.su.storage.ss_family);
        dst.su.in6.sin6_port = src.su.in6.sin6_port;
        return;
    default:
        assert(0);
    }
}

} // namespace opencmw::nghttp3

#endif // NGHTTP3_UTILS_H
