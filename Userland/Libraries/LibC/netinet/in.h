/*
 * Copyright (c) 2018-2020, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <Kernel/API/POSIX/netinet/in.h>
#include <endian.h>

__BEGIN_DECLS

in_addr_t inet_addr(char const*);

static inline uint16_t htons(uint16_t value)
{
#if __BYTE_ORDER == __LITTLE_ENDIAN
    return __builtin_bswap16(value);
#else
    return value;
#endif
}

static inline uint16_t ntohs(uint16_t value)
{
    return htons(value);
}

static inline uint32_t htonl(uint32_t value)
{
#if __BYTE_ORDER == __LITTLE_ENDIAN
    return __builtin_bswap32(value);
#else
    return value;
#endif
}

static inline uint32_t ntohl(uint32_t value)
{
    return htonl(value);
}

// https://datatracker.ietf.org/doc/html/rfc870

// The first type of address, or class A, has a 7-bit network number
// and a 24-bit local address.  The highest-order bit is set to 0.
// This allows 128 class A networks.
//
//                      1                   2                   3
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |0|   NETWORK   |                Local Address                  |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//
//                        Class A Address

#define IN_CLASSA(x) (((x)&0x80000000) == 0x00000000)
#define IN_CLASSA_NET 0xff000000

// The second type of address, class B, has a 14-bit network number
// and a 16-bit local address.  The two highest-order bits are set to
// 1-0.  This allows 16,384 class B networks.
//
//                      1                   2                   3
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |1 0|           NETWORK         |          Local Address        |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//
//                        Class B Address

#define IN_CLASSB(x) (((x)&0xc0000000) == 0x80000000)
#define IN_CLASSB_NET 0xffff0000

// The third type of address, class C, has a 21-bit network number
// and a 8-bit local address.  The three highest-order bits are set
// to 1-1-0.  This allows 2,097,152 class C networks.
//
//                      1                   2                   3
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |1 1 0|                    NETWORK              | Local Address |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//
//                        Class C Address

#define IN_CLASSC(x) (((x)&0xe0000000) == 0xc0000000)
#define IN_CLASSC_NET 0xffffff00

#define IN_MULTICAST(x) (((x)&0xf0000000) == 0xe0000000)

// NOTE: The IPv6 Addressing Scheme that we detect are documented in RFC# 2373.
//       See: https://datatracker.ietf.org/doc/html/rfc2373

// RFC# 2373 - 2.5.3 The Loopback Address
#define IN6_IS_ADDR_LOOPBACK(addr) \
    ((addr)->s6_addr[0] == 0 && (addr)->s6_addr[1] == 0 && (addr)->s6_addr[2] == 0 && (addr)->s6_addr[3] == 0 && (addr)->s6_addr[4] == 0 && (addr)->s6_addr[5] == 0 && (addr)->s6_addr[6] == 0 && (addr)->s6_addr[7] == 0 && (addr)->s6_addr[8] == 0 && (addr)->s6_addr[9] == 0 && (addr)->s6_addr[10] == 0 && (addr)->s6_addr[11] == 0 && (addr)->s6_addr[12] == 0 && (addr)->s6_addr[13] == 0 && (addr)->s6_addr[14] == 0 && (addr)->s6_addr[15] == 1)

// RFC# 2373 - 2.5.4 IPv6 Addresses with Embedded IPv4 Addresses
#define IN6_IS_ADDR_V4COMPAT(addr) \
    ((((addr)->s6_addr[0]) == 0) && (((addr)->s6_addr[1]) == 0) && (((addr)->s6_addr[2]) == 0) && (((addr)->s6_addr[3]) == 0) && (((addr)->s6_addr[4]) == 0) && (((addr)->s6_addr[5]) == 0) && (((addr)->s6_addr[6]) == 0) && (((addr)->s6_addr[7]) == 0) && (((addr)->s6_addr[8]) == 0) && (((addr)->s6_addr[9]) == 0) && (((addr)->s6_addr[10]) == 0x00) && (((addr)->s6_addr[11]) == 0x00))

#define IN6_IS_ADDR_V4MAPPED(addr) \
    ((((addr)->s6_addr[0]) == 0) && (((addr)->s6_addr[1]) == 0) && (((addr)->s6_addr[2]) == 0) && (((addr)->s6_addr[3]) == 0) && (((addr)->s6_addr[4]) == 0) && (((addr)->s6_addr[5]) == 0) && (((addr)->s6_addr[6]) == 0) && (((addr)->s6_addr[7]) == 0) && (((addr)->s6_addr[8]) == 0) && (((addr)->s6_addr[9]) == 0) && (((addr)->s6_addr[10]) == 0xFF) && (((addr)->s6_addr[11]) == 0xFF))

// RFC# 2373 - 2.5.8 Local-Use IPv6 Unicast Addresses
#define IN6_IS_ADDR_LINKLOCAL(addr) \
    (((addr)->s6_addr[0] == 0xfe) && (((addr)->s6_addr[1] & 0xc0) == 0x80))

#define IN6_IS_ADDR_SITELOCAL(addr) \
    (((addr)->s6_addr[0] == 0xfe) && (((addr)->s6_addr[1] & 0xc0) == 0xc0))

// RFC# 2373 - 2.7 Multicast Addresses
#define IN6_IS_ADDR_MULTICAST(addr) \
    (((addr)->s6_addr[0] == 0xff))

#define IN6_IS_ADDR_MC_NODELOCAL(addr) \
    (((addr)->s6_addr[0] == 0xff) && (((addr)->s6_addr[1] & 0x0f) == 0x01))

#define IN6_IS_ADDR_MC_LINKLOCAL(addr) \
    (((addr)->s6_addr[0] == 0xff) && (((addr)->s6_addr[1] & 0x0f) == 0x02))

#define IN6_IS_ADDR_MC_SITELOCAL(addr) \
    (((addr)->s6_addr[0] == 0xff) && (((addr)->s6_addr[1] & 0x0f) == 0x05))

#define IN6_IS_ADDR_MC_ORGLOCAL(addr) \
    (((addr)->s6_addr[0] == 0xff) && (((addr)->s6_addr[1] & 0x0f) == 0x08))

#define IN6_IS_ADDR_MC_GLOBAL(addr) \
    (((addr)->s6_addr[0] == 0xff) && (((addr)->s6_addr[1] & 0x0f) == 0x0e))

__END_DECLS
