/*
 * ngtcp2
 *
 * Copyright (c) 2017 ngtcp2 contributors
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
#ifndef SHARED_H
#define SHARED_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif // defined(HAVE_CONFIG_H)

#include <cassert>
#include <cstring>
#include <iostream>
#include <netinet/ip.h>
#include <optional>
#include <unistd.h>

#include <ngtcp2/ngtcp2.h>

#include "network.h"

namespace ngtcp2 {

enum class AppProtocol {
  H3,
  HQ,
};

constexpr uint8_t HQ_ALPN[] = "\xahq-interop";
constexpr uint8_t HQ_ALPN_V1[] = "\xahq-interop";

constexpr uint8_t H3_ALPN[] = "\x2h3";
constexpr uint8_t H3_ALPN_V1[] = "\x2h3";

// msghdr_get_ecn gets ECN bits from |msg|.  |family| is the address
// family from which packet is received.
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

// fd_set_recv_ecn sets socket option to |fd| so that it can receive
// ECN bits.
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

// fd_set_ip_mtu_discover sets IP(V6)_MTU_DISCOVER socket option to
// |fd|.
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

// fd_set_ip_dontfrag sets IP(V6)_DONTFRAG socket option to |fd|.
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

// msghdr_get_udp_gro returns UDP_GRO value from |msg|.  If UDP_GRO is
// not found, or UDP_GRO is not supported, this function returns 0.
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

} // namespace ngtcp2

#endif // !defined(SHARED_H)
