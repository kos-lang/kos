/* SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2014-2024 Chris Dragan
 */

#include "../inc/kos_array.h"
#include "../inc/kos_buffer.h"
#include "../inc/kos_constants.h"
#include "../inc/kos_error.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_malloc.h"
#include "../inc/kos_memory.h"
#include "../inc/kos_module.h"
#include "../inc/kos_object.h"
#include "../inc/kos_string.h"
#include "../inc/kos_utils.h"
#include "../core/kos_object_internal.h"
#include "../core/kos_try.h"

#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#   pragma warning( push )
#   pragma warning( disable : 4255 )
#   include <winsock2.h>
#   include <ws2tcpip.h>
#   pragma warning( pop )
#   define SHUT_RD   SD_RECEIVE
#   define SHUT_WR   SD_SEND
#   define SHUT_RDWR SD_BOTH
#   pragma warning( disable : 4996 ) /* strerror: This function or variable may be unsafe */
#else
#   include <arpa/inet.h>
#   include <errno.h>
#   include <fcntl.h>
#   include <netdb.h>
#   include <netinet/in.h>
#   include <sys/socket.h>
#   include <sys/select.h>
#   include <sys/time.h>
#   include <sys/types.h>
#   include <sys/un.h>
#   include <unistd.h>
#endif

#if !defined(SO_REUSEPORT) && !(defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__))
#define SO_REUSEPORT (SO_REUSEADDR + 0x10000)
#endif

#if !defined(__APPLE__) && !defined(AI_DEFAULT)
#define AI_DEFAULT (AI_ADDRCONFIG | AI_V4MAPPED)
#endif

#ifdef _WIN32
typedef SOCKET         KOS_SOCKET;
typedef ADDRESS_FAMILY KOS_ADDR_FAMILY;
typedef int            DATA_LEN;
typedef int            ADDR_LEN;
typedef long           TIME_FRAGMENT;

#define KOS_INVALID_SOCKET ((KOS_SOCKET)INVALID_SOCKET)

#define reset_last_error() ((void)0)

static int get_error(void)
{
    return WSAGetLastError();
}
#else
typedef int       KOS_SOCKET;
typedef int       KOS_ADDR_FAMILY;
typedef size_t    DATA_LEN;
typedef socklen_t ADDR_LEN;
typedef unsigned  TIME_FRAGMENT;

#define KOS_INVALID_SOCKET ((KOS_SOCKET)-1)

#define closesocket close

static void reset_last_error(void)
{
    errno = 0;
}

static int get_error(void)
{
    return errno;
}
#endif

KOS_DECLARE_STATIC_CONST_STRING(str_address,               "address");
KOS_DECLARE_STATIC_CONST_STRING(str_blocking,              "blocking");
KOS_DECLARE_STATIC_CONST_STRING(str_data,                  "data");
KOS_DECLARE_STATIC_CONST_STRING(str_domain,                "domain");
KOS_DECLARE_STATIC_CONST_STRING(str_err_not_buffer,        "argument to socket.recv is not a buffer");
KOS_DECLARE_STATIC_CONST_STRING(str_err_not_buffer_or_str, "argument to socket.send is neither a buffer nor a string");
KOS_DECLARE_STATIC_CONST_STRING(str_err_too_many_to_read,  "requested read size exceeds buffer size limit");
KOS_DECLARE_STATIC_CONST_STRING(str_err_socket_not_open,   "socket not open or not a socket object");
KOS_DECLARE_STATIC_CONST_STRING(str_flags,                 "flags");
KOS_DECLARE_STATIC_CONST_STRING(str_level,                 "level");
KOS_DECLARE_STATIC_CONST_STRING(str_port,                  "port");
KOS_DECLARE_STATIC_CONST_STRING(str_protocol,              "protocol");
KOS_DECLARE_STATIC_CONST_STRING(str_socket,                "socket");
KOS_DECLARE_STATIC_CONST_STRING(str_type,                  "type");
KOS_DECLARE_STATIC_CONST_STRING(str_timeout_sec,           "timeout_sec");

typedef struct KOS_SOCKET_HOLDER_S {
    KOS_ATOMIC(uint32_t) socket_fd;
    KOS_ATOMIC(uint32_t) ref_count;
    int                  family;
#ifdef _WIN32
    int                  blocking;
#endif
} KOS_SOCKET_HOLDER;

static int acquire_socket(KOS_SOCKET_HOLDER *socket_holder)
{
    uint32_t ref_count;

    assert(socket_holder);

    do {
        ref_count = KOS_atomic_read_relaxed_u32(socket_holder->ref_count);
        if ((int32_t)ref_count <= 0)
            return (int)ref_count;
    } while ( ! KOS_atomic_cas_weak_u32(socket_holder->ref_count, ref_count, ref_count + 1));

    return (int)ref_count;
}

static void release_socket(KOS_SOCKET_HOLDER *socket_holder)
{
    if (socket_holder) {
        const int32_t ref_count = KOS_atomic_add_i32(*(KOS_ATOMIC(int32_t)*)&socket_holder->ref_count, -1);

        assert(ref_count >= 1);

        if (ref_count == 1) {
            const int32_t socket_fd = (int32_t)KOS_atomic_swap_u32(socket_holder->socket_fd, ~0U);

            if (socket_fd >= 0)
                closesocket(socket_fd);

            KOS_free(socket_holder);
        }
    }
}

static void socket_finalize(KOS_CONTEXT ctx, void *priv)
{
    if (priv)
        release_socket((KOS_SOCKET_HOLDER *)priv);
}

KOS_DECLARE_PRIVATE_CLASS(socket_priv_class);

static KOS_SOCKET_HOLDER *make_socket_holder(KOS_CONTEXT ctx,
                                             KOS_SOCKET  socket_fd,
                                             int         family)
{
    KOS_SOCKET_HOLDER *const socket_holder = (KOS_SOCKET_HOLDER *)KOS_malloc(sizeof(KOS_SOCKET_HOLDER));

    if (socket_holder) {
        socket_holder->socket_fd = (uint32_t)socket_fd;
        socket_holder->ref_count = 1;
        socket_holder->family    = family;
#ifdef _WIN32
        socket_holder->blocking  = 1;
#endif
    }
    else
        KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);

    return socket_holder;
}

static int set_socket_object(KOS_CONTEXT ctx,
                             KOS_OBJ_ID  socket_obj,
                             KOS_SOCKET  socket_fd,
                             int         family)
{
    KOS_SOCKET_HOLDER *const socket_holder = make_socket_holder(ctx, socket_fd, family);

    if (socket_holder)
        KOS_object_set_private_ptr(socket_obj, socket_holder);

    return socket_holder ? KOS_SUCCESS : KOS_ERROR_EXCEPTION;
}

static KOS_SOCKET get_socket(KOS_SOCKET_HOLDER *socket_holder)
{
    return (KOS_SOCKET)KOS_atomic_read_relaxed_u32(socket_holder->socket_fd);
}

static int is_socket_valid(KOS_SOCKET s)
{
#ifdef _WIN32
    return s != KOS_INVALID_SOCKET;
#else
    return s >= 0;
#endif
}

static int acquire_socket_object(KOS_CONTEXT         ctx,
                                 KOS_OBJ_ID          socket_obj,
                                 KOS_SOCKET_HOLDER **socket_holder)
{
    *socket_holder = (KOS_SOCKET_HOLDER *)KOS_object_get_private(socket_obj, &socket_priv_class);

    if ( ! *socket_holder || (acquire_socket(*socket_holder) <= 0)) {
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_socket_not_open));
        return KOS_ERROR_EXCEPTION;
    }

    if ( ! is_socket_valid(get_socket(*socket_holder))) {
        release_socket(*socket_holder);
        *socket_holder = KOS_NULL;

        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_socket_not_open));
        return KOS_ERROR_EXCEPTION;
    }

    return KOS_SUCCESS;
}

typedef union KOS_GENERIC_ADDR_U {
    struct sockaddr     addr;
    struct sockaddr_in  inet;
    struct sockaddr_in6 inet6;
#ifndef _WIN32
    struct sockaddr_un  local;
#endif
} KOS_GENERIC_ADDR;

static KOS_OBJ_ID address_to_string(KOS_CONTEXT             ctx,
                                    const KOS_GENERIC_ADDR *addr,
                                    ADDR_LEN                addr_len)
{
    KOS_LOCAL  val;
    KOS_OBJ_ID str_id = KOS_BADPTR;
    char       buf[40];

    KOS_init_local(ctx, &val);

    switch (addr->addr.sa_family) {

        case AF_INET:
            {
                const uint32_t ipv4_addr = ntohl(addr->inet.sin_addr.s_addr);
                unsigned       len;

                len = (unsigned)snprintf(buf, sizeof(buf), "%u.%u.%u.%u",
                                         (uint8_t)(ipv4_addr >> 24),
                                         (uint8_t)(ipv4_addr >> 16),
                                         (uint8_t)(ipv4_addr >> 8),
                                         (uint8_t)ipv4_addr);

                str_id = KOS_new_string(ctx, buf, len);
            }
            break;

        case AF_INET6:
            {
                const uint8_t *byte = (const uint8_t *)&addr->inet6.sin6_addr.s6_addr[0];
                unsigned       pos  = 0;
                unsigned       len;
                unsigned       i;

                for (i = 0; i < 16; i += 2) {

                    if (byte[i] == 0 && byte[i + 1] == 0) {
                        unsigned j;

                        if (pos == 0) {
                            buf[pos++] = ':';
                            buf[pos++] = ':';
                            continue;
                        }

                        if (pos == 2 && buf[1] == ':')
                            continue;

                        for (j = i + 2; j < 16; j++)
                            if (byte[j])
                                break;

                        if (j == 16) {
                            if (buf[pos - 1] != ':') {
                                buf[pos++] = ':';
                                buf[pos++] = ':';
                            }
                            break;
                        }
                    }

                    if (pos && buf[pos - 1] != ':')
                        buf[pos++] = ':';

                    len = (unsigned)snprintf(&buf[pos], sizeof(buf) - pos, "%02x%02x",
                                             byte[i], byte[i + 1]);
                    assert(len == 4);

                    while (buf[pos] == '0' && len > 1) {
                        unsigned k = 1;
                        for (k = 1; k < len; k++)
                            buf[pos + k - 1] = buf[pos + k];
                        --len;
                    }

                    pos += len;
                }

                str_id = KOS_new_string(ctx, buf, pos);
            }
            break;

        /* TODO AF_LOCAL */

        default:
            KOS_raise_printf(ctx, "unsupported family %u", (unsigned)addr->addr.sa_family);
            break;
    }

    KOS_destroy_top_local(ctx, &val);

    return str_id;
}

static int add_address_desc(KOS_CONTEXT             ctx,
                            KOS_OBJ_ID              ret_id,
                            const KOS_GENERIC_ADDR *addr,
                            ADDR_LEN                addr_len)
{
    KOS_LOCAL ret;
    KOS_LOCAL val;
    uint16_t  port;
    int       error = KOS_ERROR_EXCEPTION;

    KOS_init_local_with(ctx, &ret, ret_id);
    KOS_init_local(     ctx, &val);

    val.o = address_to_string(ctx, addr, addr_len);
    TRY_OBJID(val.o);

    TRY(KOS_set_property(ctx, ret.o, KOS_CONST_ID(str_address), val.o));

    switch (addr->addr.sa_family) {

        case AF_INET:
            port = ntohs(addr->inet.sin_port);
            break;

        case AF_INET6:
            port = ntohs(addr->inet6.sin6_port);
            break;

        /* TODO AF_LOCAL */

        default:
            KOS_raise_printf(ctx, "unsupported family %u", (unsigned)addr->addr.sa_family);
            RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }

    TRY(KOS_set_property(ctx, ret.o, KOS_CONST_ID(str_port), TO_SMALL_INT(port)));

cleanup:
    KOS_destroy_top_locals(ctx, &val, &ret);

    return error;
}

static KOS_OBJ_ID get_blocking(KOS_CONTEXT ctx,
                               KOS_OBJ_ID  this_obj,
                               KOS_OBJ_ID  args_obj);

static KOS_OBJ_ID set_blocking(KOS_CONTEXT ctx,
                               KOS_OBJ_ID  this_obj,
                               KOS_OBJ_ID  args_obj);

static const KOS_CONVERT socket_args[4] = {
    { KOS_CONST_ID(str_domain),   TO_SMALL_INT(PF_INET),     0, 0, KOS_NATIVE_INT32 },
    { KOS_CONST_ID(str_type),     TO_SMALL_INT(SOCK_STREAM), 0, 0, KOS_NATIVE_INT32 },
    { KOS_CONST_ID(str_protocol), TO_SMALL_INT(0),           0, 0, KOS_NATIVE_INT32 },
    KOS_DEFINE_TAIL_ARG()
};

/* @item net socket()
 *
 *     socket(domain = AF_INET, type = SOCK_STREAM, protocol = 0)
 *
 * Socket object class.
 *
 * Returns created socket object.
 *
 * `domain` is the communication domain, e.g. `AF_INET`, `AF_INET6` or `AF_LOCAL`.
 *
 * `type` specifies the semantics of communication, e.g. `SOCK_STREAM`, `SOCK_DGRAM` or `SOCK_RAW`.
 *
 * `protocol` specifies particular protocol, 0 typically indicates default protocol, but it can
 * be a specific protocol, for example `IPPROTO_TCP`, `IPPROTO_UDP` or `IPPROTO_ICMP`.
 *
 * On error throws an exception.
 */
static KOS_OBJ_ID kos_socket(KOS_CONTEXT ctx,
                             KOS_OBJ_ID  this_obj,
                             KOS_OBJ_ID  args_obj)
{
    KOS_LOCAL  this_;
    KOS_LOCAL  ret;
    KOS_SOCKET socket_fd    = KOS_INVALID_SOCKET;
    int32_t    arg_domain   = 0;
    int32_t    arg_type     = 0;
    int32_t    arg_protocol = 0;
    int        saved_errno  = 0;
    int        error;

    assert(KOS_get_array_size(args_obj) >= 3);

    KOS_init_local(     ctx, &ret);
    KOS_init_local_with(ctx, &this_, this_obj);

    TRY(KOS_extract_native_from_array(ctx, args_obj, "argument", socket_args, KOS_NULL,
                                      &arg_domain, &arg_type, &arg_protocol));

    KOS_suspend_context(ctx);

    reset_last_error();

    socket_fd = socket(arg_domain, arg_type, arg_protocol);

    if (socket_fd == (KOS_SOCKET)-1)
        saved_errno = get_error();

    KOS_resume_context(ctx);

    if (socket_fd == KOS_INVALID_SOCKET) {
        KOS_raise_errno_value(ctx, "socket", saved_errno);
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }

    ret.o = KOS_new_object_with_private(ctx, this_.o, &socket_priv_class, socket_finalize);
    TRY_OBJID(ret.o);

    TRY(KOS_set_builtin_dynamic_property(ctx,
                                         ret.o,
                                         KOS_CONST_ID(str_blocking),
                                         KOS_get_module(ctx),
                                         get_blocking,
                                         set_blocking));

    TRY(set_socket_object(ctx, ret.o, socket_fd, arg_domain));

cleanup:
    ret.o = KOS_destroy_top_locals(ctx, &this_, &ret);

    return error ? KOS_BADPTR : ret.o;
}

/* @item net socket.prototype.accept()
 *
 *     socket.prototype.accept()
 *
 * Accepts pending connection on a listening socket.
 *
 * The `this` socket must be in a listening state, i.e.
 * `listen()` must have been called on it.
 *
 * Returns an object with two properties:
 * - `socket`: new socket with the accepted connection,
 * - `address`: address of the remote host from which the connection has been made.
 *
 * On error throws an exception.
 */
static KOS_OBJ_ID kos_accept(KOS_CONTEXT ctx,
                             KOS_OBJ_ID  this_obj,
                             KOS_OBJ_ID  args_obj)
{
    KOS_LOCAL          this_;
    KOS_LOCAL          sock;
    KOS_LOCAL          ret;
    KOS_GENERIC_ADDR   addr;
    KOS_SOCKET_HOLDER *socket_holder = KOS_NULL;
    KOS_OBJ_ID         proto_obj;
    KOS_SOCKET         socket_fd     = KOS_INVALID_SOCKET;
    ADDR_LEN           addr_len      = (ADDR_LEN)sizeof(addr);
    int                saved_errno   = 0;
    int                error;

    KOS_init_local(     ctx, &ret);
    KOS_init_local(     ctx, &sock);
    KOS_init_local_with(ctx, &this_, this_obj);

    TRY(acquire_socket_object(ctx, this_obj, &socket_holder));

    KOS_suspend_context(ctx);

    reset_last_error();

    socket_fd = accept(get_socket(socket_holder), (struct sockaddr *)&addr, &addr_len);

    if (socket_fd == (KOS_SOCKET)-1)
        saved_errno = get_error();

    KOS_resume_context(ctx);

    if (socket_fd == KOS_INVALID_SOCKET) {
        KOS_raise_errno_value(ctx, "accept", saved_errno);
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }

    proto_obj = KOS_get_prototype(ctx, this_.o);

    sock.o = KOS_new_object_with_private(ctx, proto_obj, &socket_priv_class, socket_finalize);
    TRY_OBJID(sock.o);

    TRY(set_socket_object(ctx, sock.o, socket_fd, socket_holder->family));

    ret.o = KOS_new_object(ctx);
    TRY_OBJID(ret.o);

    TRY(KOS_set_property(ctx, ret.o, KOS_CONST_ID(str_socket), sock.o));

cleanup:
    release_socket(socket_holder);

    ret.o = KOS_destroy_top_locals(ctx, &this_, &ret);

    return error ? KOS_BADPTR : ret.o;
}

static int get_port(KOS_CONTEXT ctx,
                    KOS_OBJ_ID  port_arg,
                    KOS_VECTOR *port_cstr)
{
    const KOS_TYPE type = GET_OBJ_TYPE(port_arg);

    switch (type) {
        case OBJ_SMALL_INTEGER:
            /* fall through */
        case OBJ_INTEGER:
            {
                int64_t value;

                if (type == OBJ_SMALL_INTEGER)
                    value = GET_SMALL_INT(port_arg);
                else
                    value = OBJPTR(INTEGER, port_arg)->value;

                if (value < 0 || value > 0xFFFFu) {
                    KOS_raise_printf(ctx, "invalid 'port' value %" PRId64, value);
                    return KOS_ERROR_EXCEPTION;
                }
            }
            break;

        case OBJ_STRING:
            break;

        default:
            KOS_raise_printf(ctx, "'port' is %s but expected string or integer",
                             KOS_get_type_name(type));
            return KOS_ERROR_EXCEPTION;
    }

    return KOS_object_to_string_or_cstr_vec(ctx, port_arg, KOS_DONT_QUOTE, KOS_NULL, port_cstr);
}

KOS_DECLARE_STATIC_CONST_STRING(str_empty, "");
KOS_DECLARE_STATIC_CONST_STRING(str_zero,  "0");

static const KOS_CONVERT bind_args[3] = {
    { KOS_CONST_ID(str_address), KOS_CONST_ID(str_empty), 0, 0, KOS_NATIVE_STRING_PTR },
    { KOS_CONST_ID(str_port),    KOS_CONST_ID(str_zero),  0, 0, KOS_NATIVE_SKIP       },
    KOS_DEFINE_TAIL_ARG()
};

/* @item net socket.prototype.bind()
 *
 *     socket.prototype.bind(address = "", port = 0)
 *
 * Binds an address to a socket.
 *
 * `address` specifies the IP address to bind.  For IPv4 and IPv6 sockets this is
 * a hostname or a numeric IP address.  If not specified, the default address
 * 0.0.0.0 is bound.
 *
 * `port` specifies the port to bind.  It is an integer value from 0 to 65535.
 * If `port` is not specified, a random port number is chosen.  Ports below 1024
 * are typically reserved for system services and require administrator privileges.
 * `port` can also be specified as a string, in which case it can be a service name.
 *
 * Returns the socket itself (`this`).
 *
 * On error throws an exception.
 */
static KOS_OBJ_ID kos_bind(KOS_CONTEXT ctx,
                           KOS_OBJ_ID  this_obj,
                           KOS_OBJ_ID  args_obj)
{
    struct KOS_MEMPOOL_S alloc;
    KOS_LOCAL            this_;
    char                *address_cstr  = KOS_NULL;
    KOS_VECTOR           port_cstr;
    KOS_SOCKET_HOLDER   *socket_holder = KOS_NULL;
    struct addrinfo     *ai_alloc      = KOS_NULL;
    struct addrinfo     *address_info  = KOS_NULL;
    struct addrinfo      hints;
    int                  saved_errno;
    int                  error;

    KOS_init_local_with(ctx, &this_, this_obj);

    KOS_vector_init(&port_cstr);

    KOS_mempool_init_small(&alloc, 512U);

    TRY(KOS_extract_native_from_array(ctx, args_obj, "argument", bind_args, &alloc, &address_cstr));

    assert(KOS_get_array_size(args_obj) >= 2);
    {
        const KOS_OBJ_ID port_arg = KOS_array_read(ctx, args_obj, 1);
        TRY_OBJID(port_arg);
        TRY(get_port(ctx, port_arg, &port_cstr));
    }

    TRY(acquire_socket_object(ctx, this_.o, &socket_holder));

    KOS_suspend_context(ctx);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = socket_holder->family;
    hints.ai_flags  = AI_DEFAULT | AI_PASSIVE;

    error = getaddrinfo(address_cstr[0] ? address_cstr : KOS_NULL,
                        port_cstr.buffer,
                        &hints,
                        &ai_alloc);

    if (error) {
        const char *error_str = gai_strerror(error);

        KOS_resume_context(ctx);

        KOS_raise_exception_cstring(ctx, error_str);
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }

    for (address_info = ai_alloc; ; address_info = address_info->ai_next) {

        reset_last_error();

        error = bind(get_socket(socket_holder),
                     address_info->ai_addr,
                     (ADDR_LEN)address_info->ai_addrlen);

        if ( ! error)
            break;

        if ( ! address_info->ai_next)
            break;
    }

    saved_errno = get_error();

    KOS_resume_context(ctx);

    if (error) {
        KOS_raise_printf(ctx, "bind(%s, %s): %s",
                         address_cstr, port_cstr.buffer, strerror(saved_errno));
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }

cleanup:
    if (ai_alloc)
        freeaddrinfo(ai_alloc);

    release_socket(socket_holder);

    KOS_vector_destroy(&port_cstr);

    KOS_mempool_destroy(&alloc);

    this_.o = KOS_destroy_top_local(ctx, &this_);

    return error ? KOS_BADPTR : this_.o;
}

/* @item net socket.prototype.close()
 *
 *     socket.prototype.close()
 *
 * Closes the socket object if it is still opened.
 *
 * Returns the socket itself (`this`).
 *
 * On error throws an exception.
 */
/* @item net socket.prototype.release()
 *
 *     socket.prototype.release()
 *
 * Closes the socket object if it is still opened.  This function is identical
 * with `socket.prototype.close()` and it is suitable for use with the `with` statement.
 *
 * Returns the socket itself (`this`).
 *
 * On error throws an exception.
 */
static KOS_OBJ_ID kos_close(KOS_CONTEXT ctx,
                            KOS_OBJ_ID  this_obj,
                            KOS_OBJ_ID  args_obj)
{
    KOS_SOCKET_HOLDER *closed_holder;
    KOS_SOCKET_HOLDER *socket_holder;

    if (GET_OBJ_TYPE(this_obj) != OBJ_OBJECT) {
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_socket_not_open));
        return KOS_BADPTR;
    }

    closed_holder = make_socket_holder(ctx, KOS_INVALID_SOCKET, -1);
    if ( ! closed_holder)
        return KOS_BADPTR;

    socket_holder = (KOS_SOCKET_HOLDER *)KOS_object_swap_private(this_obj, &socket_priv_class, closed_holder);

    release_socket(socket_holder);

    return this_obj;

}

static const KOS_CONVERT connect_args[3] = {
    { KOS_CONST_ID(str_address), KOS_BADPTR, 0, 0, KOS_NATIVE_STRING_PTR },
    { KOS_CONST_ID(str_port),    KOS_BADPTR, 0, 0, KOS_NATIVE_SKIP       },
    KOS_DEFINE_TAIL_ARG()
};

/* @item net socket.prototype.connect()
 *
 *     socket.prototype.connect(address, port)
 *
 * Connects the socket to a remote address.
 *
 * `address` specifies the IP address to connect to.  For IPv4 and IPv6 sockets this is
 * a hostname or a numeric IP address.
 *
 * `port` specifies the port to bind.  It is an integer value from 1 to 65535 or
 * a string which is the name of the service.
 *
 * Returns the socket itself (`this`).
 *
 * On error throws an exception.
 */
static KOS_OBJ_ID kos_connect(KOS_CONTEXT ctx,
                              KOS_OBJ_ID  this_obj,
                              KOS_OBJ_ID  args_obj)
{
    struct KOS_MEMPOOL_S alloc;
    KOS_LOCAL            this_;
    char                *address_cstr  = KOS_NULL;
    KOS_VECTOR           port_cstr;
    KOS_SOCKET_HOLDER   *socket_holder = KOS_NULL;
    struct addrinfo     *ai_alloc      = KOS_NULL;
    struct addrinfo     *address_info  = KOS_NULL;
    struct addrinfo      hints;
    int                  saved_errno;
    int                  error;

    KOS_init_local_with(ctx, &this_, this_obj);

    KOS_vector_init(&port_cstr);

    KOS_mempool_init_small(&alloc, 512U);

    TRY(KOS_extract_native_from_array(ctx, args_obj, "argument", bind_args, &alloc, &address_cstr));

    assert(KOS_get_array_size(args_obj) >= 2);
    {
        const KOS_OBJ_ID port_arg = KOS_array_read(ctx, args_obj, 1);
        TRY_OBJID(port_arg);
        TRY(get_port(ctx, port_arg, &port_cstr));
    }

    TRY(acquire_socket_object(ctx, this_.o, &socket_holder));

    KOS_suspend_context(ctx);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = socket_holder->family;
    hints.ai_flags  = AI_DEFAULT;

    error = getaddrinfo(address_cstr, port_cstr.buffer, &hints, &ai_alloc);

    if (error) {
        const char *error_str = gai_strerror(error);

        KOS_resume_context(ctx);

        KOS_raise_exception_cstring(ctx, error_str);
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }

    for (address_info = ai_alloc; ; address_info = address_info->ai_next) {

        reset_last_error();

        error = connect(get_socket(socket_holder),
                        address_info->ai_addr,
                        (ADDR_LEN)address_info->ai_addrlen);

        if ( ! error)
            break;

        if ( ! address_info->ai_next)
            break;
    }

    saved_errno = get_error();

    KOS_resume_context(ctx);

    if (error) {
        KOS_raise_printf(ctx, "connect(%s, %s): %s",
                         address_cstr, port_cstr.buffer, strerror(saved_errno));
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }

cleanup:
    if (ai_alloc)
        freeaddrinfo(ai_alloc);

    release_socket(socket_holder);

    KOS_vector_destroy(&port_cstr);

    KOS_mempool_destroy(&alloc);

    this_.o = KOS_destroy_top_local(ctx, &this_);

    return error ? KOS_BADPTR : this_.o;
}

KOS_DECLARE_STATIC_CONST_STRING(str_backlog, "backlog");

static const KOS_CONVERT listen_args[2] = {
    { KOS_CONST_ID(str_backlog), TO_SMALL_INT(5), 0, 0, KOS_NATIVE_INT32 },
    KOS_DEFINE_TAIL_ARG()
};

/* @item net socket.prototype.listen()
 *
 *     socket.prototype.listen(backlog = 5)
 *
 * Prepares a socket for accepting connections.
 *
 * `backlog` specifies how many connections can be waiting.
 *
 * Returns the socket itself (`this`).
 *
 * On error throws an exception.
 */
static KOS_OBJ_ID kos_listen(KOS_CONTEXT ctx,
                             KOS_OBJ_ID  this_obj,
                             KOS_OBJ_ID  args_obj)
{
    KOS_LOCAL          this_;
    KOS_SOCKET_HOLDER *socket_holder = KOS_NULL;
    int                saved_errno;
    int                error;
    int32_t            backlog       = 0;

    KOS_init_local_with(ctx, &this_, this_obj);

    TRY(KOS_extract_native_from_array(ctx, args_obj, "argument", listen_args, KOS_NULL, &backlog));

    if (backlog < 1 || backlog >= 0x10000) {
        KOS_raise_printf(ctx, "backlog argument %d is out of range", backlog);
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }

    TRY(acquire_socket_object(ctx, this_.o, &socket_holder));

    KOS_suspend_context(ctx);

    reset_last_error();

    error = listen(get_socket(socket_holder), backlog);

    saved_errno = get_error();

    KOS_resume_context(ctx);

    if (error) {
        KOS_raise_errno_value(ctx, "listen", saved_errno);
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }

cleanup:
    release_socket(socket_holder);

    this_.o = KOS_destroy_top_local(ctx, &this_);

    return error ? KOS_BADPTR : this_.o;
}

KOS_DECLARE_STATIC_CONST_STRING(str_buffer, "buffer");
KOS_DECLARE_STATIC_CONST_STRING(str_size,   "size");

/* @item net socket.prototype.read()
 *
 *     socket.prototype.read(size = 4096, buffer = void, flags = 0)
 *
 * This is the same function as `socket.prototype.recv()`.
 */
/* @item net socket.prototype.recv()
 *
 *     socket.prototype.recv(size = 4096, buffer = void, flags = 0)
 *
 * Receives a variable number of bytes from a connected socket object.
 * Returns a buffer containing the bytes read.
 *
 * Receives as many bytes as it can, up to the specified `size`.
 *
 * `size` is the maximum bytes to receive.  `size` defaults to 4096.  Fewer
 * bytes can be received if no more bytes are available.
 *
 * If `buffer` is specified and non-void, bytes are appended to it and that buffer is
 * returned instead of creating a new buffer.
 *
 * `flags` specifies bit flag options for receiving data.  Possible bit flags are
 * `MSG_OOB`, `MSG_PEEK` and `MSG_WAITALL`.
 *
 * On error throws an exception.
 */
static KOS_OBJ_ID kos_recv(KOS_CONTEXT ctx,
                           KOS_OBJ_ID  this_obj,
                           KOS_OBJ_ID  args_obj)
{
    KOS_LOCAL          args;
    KOS_LOCAL          buf;
    int64_t            num_read;
    int64_t            to_read;
    int64_t            flags64;
    KOS_SOCKET_HOLDER *socket_holder = KOS_NULL;
    uint8_t           *data;
    KOS_OBJ_ID         arg;
    uint32_t           offset;
    int                error         = KOS_SUCCESS;
    int                saved_errno   = 0;

    assert(KOS_get_array_size(args_obj) >= 3);

    KOS_init_local(     ctx, &buf);
    KOS_init_local_with(ctx, &args, args_obj);

    TRY(acquire_socket_object(ctx, this_obj, &socket_holder));

    arg = KOS_array_read(ctx, args.o, 0);
    TRY_OBJID(arg);

    TRY(KOS_get_integer(ctx, arg, &to_read));

    if (to_read < 1)
        to_read = 1;

    arg = KOS_array_read(ctx, args.o, 2);
    TRY_OBJID(arg);

    if ( ! IS_NUMERIC_OBJ(arg)) {
        KOS_raise_printf(ctx, "flags argument is %s but expected integer",
                         KOS_get_type_name(GET_OBJ_TYPE(arg)));
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }
    else
        TRY(KOS_get_integer(ctx, arg, &flags64));

    if (flags64 & (MSG_OOB | MSG_PEEK | MSG_WAITALL)) {
        KOS_raise_printf(ctx, "flags argument 0x%" PRIx64 " contains unrecognized bits", flags64);
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }

    buf.o = KOS_array_read(ctx, args.o, 1);
    TRY_OBJID(buf.o);

    if (buf.o == KOS_VOID)
        buf.o = KOS_new_buffer(ctx, 0);
    else if (GET_OBJ_TYPE(buf.o) != OBJ_BUFFER)
        RAISE_EXCEPTION_STR(str_err_not_buffer);

    offset = KOS_get_buffer_size(buf.o);

    if (to_read > (int64_t)(0xFFFFFFFFU - offset))
        RAISE_EXCEPTION_STR(str_err_too_many_to_read);

    TRY(KOS_buffer_resize(ctx, buf.o, (unsigned)(offset + to_read)));

    data = KOS_buffer_data(ctx, buf.o);

    if ( ! data)
        RAISE_ERROR(KOS_ERROR_EXCEPTION);

    KOS_suspend_context(ctx);

    reset_last_error();

    num_read = recv(get_socket(socket_holder), (char *)(data + offset), (DATA_LEN)to_read, (int)flags64);

    if (num_read < -1)
        saved_errno = get_error();

    KOS_resume_context(ctx);

    assert(num_read <= to_read);

    TRY(KOS_buffer_resize(ctx, buf.o, (unsigned)(offset + num_read)));

    if (saved_errno) {
        KOS_raise_errno_value(ctx, "recv", saved_errno);
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }

cleanup:
    release_socket(socket_holder);

    buf.o = KOS_destroy_top_locals(ctx, &args, &buf);

    return error ? KOS_BADPTR : buf.o;
}

/* @item net socket.prototype.recvfrom()
 *
 *     socket.prototype.recvfrom(size = 4096, buffer = void, flags = 0)
 *
 * Receives a variable number of bytes from a connected socket object.
 *
 * Returns an object with two properties:
 *  - `data` - buffer containing the bytes read,
 *  - `address` - address of the sender,
 *  - `port` - port of the sender.
 *
 * Receives as many bytes as it can, up to the specified `size`.
 *
 * `size` is the maximum bytes to receive.  `size` defaults to 4096.  Fewer
 * bytes can be received if no more bytes are available.
 *
 * If `buffer` is specified and non-void, bytes are appended to it and that buffer is
 * returned instead of creating a new buffer.
 *
 * `flags` specifies bit flag options for receiving data.  Possible bit flags are
 * `MSG_OOB`, `MSG_PEEK` and `MSG_WAITALL`.
 *
 * On error throws an exception.
 */
static KOS_OBJ_ID kos_recvfrom(KOS_CONTEXT ctx,
                               KOS_OBJ_ID  this_obj,
                               KOS_OBJ_ID  args_obj)
{
    KOS_GENERIC_ADDR   addr;
    KOS_LOCAL          args;
    KOS_LOCAL          buf;
    KOS_LOCAL          ret;
    int64_t            num_read;
    int64_t            to_read;
    int64_t            flags64;
    KOS_SOCKET_HOLDER *socket_holder = KOS_NULL;
    uint8_t           *data;
    KOS_OBJ_ID         arg;
    ADDR_LEN           addr_len      = 0;
    uint32_t           offset;
    int                error         = KOS_SUCCESS;
    int                saved_errno   = 0;

    assert(KOS_get_array_size(args_obj) >= 3);

    KOS_init_local(     ctx, &ret);
    KOS_init_local(     ctx, &buf);
    KOS_init_local_with(ctx, &args, args_obj);

    TRY(acquire_socket_object(ctx, this_obj, &socket_holder));

    arg = KOS_array_read(ctx, args.o, 0);
    TRY_OBJID(arg);

    TRY(KOS_get_integer(ctx, arg, &to_read));

    if (to_read < 1)
        to_read = 1;

    arg = KOS_array_read(ctx, args.o, 2);
    TRY_OBJID(arg);

    if ( ! IS_NUMERIC_OBJ(arg)) {
        KOS_raise_printf(ctx, "flags argument is %s but expected integer",
                         KOS_get_type_name(GET_OBJ_TYPE(arg)));
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }
    else
        TRY(KOS_get_integer(ctx, arg, &flags64));

    if (flags64 & (MSG_OOB | MSG_PEEK | MSG_WAITALL)) {
        KOS_raise_printf(ctx, "flags argument 0x%" PRIx64 " contains unrecognized bits", flags64);
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }

    buf.o = KOS_array_read(ctx, args.o, 1);
    TRY_OBJID(buf.o);

    if (buf.o == KOS_VOID)
        buf.o = KOS_new_buffer(ctx, 0);
    else if (GET_OBJ_TYPE(buf.o) != OBJ_BUFFER)
        RAISE_EXCEPTION_STR(str_err_not_buffer);

    offset = KOS_get_buffer_size(buf.o);

    if (to_read > (int64_t)(0xFFFFFFFFU - offset))
        RAISE_EXCEPTION_STR(str_err_too_many_to_read);

    TRY(KOS_buffer_resize(ctx, buf.o, (unsigned)(offset + to_read)));

    data = KOS_buffer_data(ctx, buf.o);

    if ( ! data)
        RAISE_ERROR(KOS_ERROR_EXCEPTION);

    KOS_suspend_context(ctx);

    memset(&addr, 0, sizeof(addr));
    addr.addr.sa_family = (KOS_ADDR_FAMILY)socket_holder->family;
    switch (socket_holder->family) {

        case AF_INET:
            addr_len = (ADDR_LEN)sizeof(addr.inet);
            break;

        case AF_INET6:
            addr_len = (ADDR_LEN)sizeof(addr.inet6);
            break;

        /* TODO AF_LOCAL */

        default:
            KOS_resume_context(ctx);
            KOS_raise_printf(ctx, "unexpected address family %d\n", socket_holder->family);
            RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }

    reset_last_error();

    num_read = recvfrom(get_socket(socket_holder),
                        (char *)(data + offset),
                        (DATA_LEN)to_read,
                        (int)flags64,
                        (struct sockaddr *)&addr,
                        &addr_len);

    if (num_read < -1)
        saved_errno = get_error();

    KOS_resume_context(ctx);

    assert(num_read <= to_read);

    TRY(KOS_buffer_resize(ctx, buf.o, (unsigned)(offset + num_read)));

    if (saved_errno) {
        KOS_raise_errno_value(ctx, "recv", saved_errno);
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }

    ret.o = KOS_new_object(ctx);
    TRY_OBJID(ret.o);

    TRY(KOS_set_property(ctx, ret.o, KOS_CONST_ID(str_data), buf.o));

    TRY(add_address_desc(ctx, ret.o, &addr, addr_len));

cleanup:
    release_socket(socket_holder);

    ret.o = KOS_destroy_top_locals(ctx, &args, &ret);

    return error ? KOS_BADPTR : ret.o;
}

/* @item net socket.prototype.wait()
 *
 *     socket.prototype.wait(timeout_sec = void)
 *
 * Waits for data to be available to read from the socket.
 *
 * On a connected or datagram socket, this function waits for data to be
 * received and be ready to read via the `recv()` or `recvfrom()` function.
 *
 * On a listening socket, this function waits for for a connection to be
 * established and the socket to be ready to accept a new connection.
 *
 * `timeout_sec` is the timeout value in seconds.  This can be a `float`, so
 * for example to wait for 500 ms, `0.5` can be passed.  If this is `void`
 * (which is the default) the function will wait indefinitely.
 *
 * Returns a boolean indicating whether the wait operation succeeded.
 * The return value `true` indicates that there is data available on the
 * socket to read.  The return value `false` indicates that the timeout
 * was reached.
 *
 * On error throws an exception.
 */
static KOS_OBJ_ID kos_wait(KOS_CONTEXT ctx,
                           KOS_OBJ_ID  this_obj,
                           KOS_OBJ_ID  args_obj)
{
#ifdef _WIN32
    TIMEVAL            time_value;
    TIMEVAL           *timeout_tv  = KOS_NULL;
#else
    struct timeval     time_value;
    struct timeval    *timeout_tv  = KOS_NULL;
#endif
    KOS_NUMERIC        timeout;
    fd_set             fds;
    KOS_SOCKET_HOLDER *socket_holder;
    KOS_LOCAL          args;
    KOS_LOCAL          this_;
    KOS_OBJ_ID         wait_obj;
    KOS_OBJ_ID         ret_obj     = KOS_FALSE;
    int                nfds        = 0;
    int                saved_errno = 0;
    int                error;

    memset(&timeout, 0, sizeof(timeout));

    KOS_init_local_with(ctx, &this_, this_obj);
    KOS_init_local_with(ctx, &args,  args_obj);

    TRY(acquire_socket_object(ctx, this_.o, &socket_holder));

    assert(KOS_get_array_size(args.o) >= 1);

    wait_obj = KOS_array_read(ctx, args.o, 0);
    TRY_OBJID(wait_obj);

    if (wait_obj != KOS_VOID)
        TRY(KOS_get_numeric_arg(ctx, args.o, 0, &timeout));

    FD_ZERO(&fds);
    FD_SET(socket_holder->socket_fd, &fds);

#ifndef _WIN32
    nfds = socket_holder->socket_fd + 1;
#endif

    if (timeout.type != KOS_NON_NUMERIC) {
        uint64_t tv_usec;

        if (timeout.type == KOS_INTEGER_VALUE) {
            if (timeout.u.i < 0 || timeout.u.i > 2147483647) {
                KOS_raise_printf(ctx, "timeout_sec argument %" PRId64 " is out of range",
                                 timeout.u.i);
                RAISE_ERROR(KOS_ERROR_EXCEPTION);
            }
            tv_usec = (uint64_t)timeout.u.i * 1000000U;
        }
        else {
            if (timeout.u.d < 0 || timeout.u.d > 2147483647.0) {
                KOS_raise_printf(ctx, "timeout_sec argument %.6f is out of range",
                                 timeout.u.d);
                RAISE_ERROR(KOS_ERROR_EXCEPTION);
            }
            tv_usec = (uint64_t)floor(timeout.u.d * 1000000.0);
        }

        memset(&time_value, 0, sizeof(time_value));
        time_value.tv_sec  = (TIME_FRAGMENT)(tv_usec / 1000000U);
        time_value.tv_usec = (TIME_FRAGMENT)(tv_usec % 1000000U);

        timeout_tv = &time_value;
    }

    KOS_suspend_context(ctx);

    reset_last_error();

    nfds = select(nfds, &fds, KOS_NULL, KOS_NULL, timeout_tv);
    if (nfds < 0)
        saved_errno = get_error();

    KOS_resume_context(ctx);

    if (saved_errno) {
        KOS_raise_errno_value(ctx, "select", saved_errno);
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }

    ret_obj = KOS_BOOL(nfds);

cleanup:
    release_socket(socket_holder);

    KOS_destroy_top_locals(ctx, &args, &this_);

    return error ? KOS_BADPTR : ret_obj;
}

/* @item net socket.prototype.blocking
 *
 *     socket.prototype.blocking
 *
 * Blocking state of a socket.
 *
 * A newly created socket is in a blocking state.  It can be
 * changed to non-blocking by writing `false` to this property.
 * This property can also be read to determine whether a socket
 * is blocking or non-blocking.
 *
 * When a socket is in non-blocking state, the receiving functions
 * `recv()`, `read()`, `recvfrom()` will immediately return 0 bytes
 * if there was no data received.  The `wait()` function needs to be
 * used to wait until any data is received.
 *
 * However, the sending functions `send()`, `write()`, `sendto()` will
 * still block until all the data is sent.
 */
static KOS_OBJ_ID get_blocking(KOS_CONTEXT ctx,
                               KOS_OBJ_ID  this_obj,
                               KOS_OBJ_ID  args_obj)
{
    KOS_SOCKET_HOLDER *socket_holder = KOS_NULL;
    int                blocking      = 1;
    int                saved_errno   = 0;
    int                error         = KOS_SUCCESS;

    TRY(acquire_socket_object(ctx, this_obj, &socket_holder));

    KOS_suspend_context(ctx);

    reset_last_error();

#ifdef _WIN32
    blocking = socket_holder->blocking;
#else
    {
        const int flags = fcntl(get_socket(socket_holder), F_GETFL);

        if (flags != -1)
            blocking = ! (flags & O_NONBLOCK);
        else
            saved_errno = get_error();
    }
#endif

    KOS_resume_context(ctx);

    if (saved_errno) {
        KOS_raise_errno_value(ctx, "fcntl", saved_errno);
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }

cleanup:
    release_socket(socket_holder);

    return error ? KOS_BADPTR : KOS_BOOL(blocking);
}

static KOS_OBJ_ID set_blocking(KOS_CONTEXT ctx,
                               KOS_OBJ_ID  this_obj,
                               KOS_OBJ_ID  args_obj)
{
    KOS_LOCAL          this_;
    KOS_SOCKET_HOLDER *socket_holder = KOS_NULL;
    KOS_OBJ_ID         arg;
    int                blocking;
    int                saved_errno   = 0;
    int                error         = KOS_SUCCESS;

    assert(KOS_get_array_size(args_obj) >= 1);

    KOS_init_local_with(ctx, &this_, this_obj);

    TRY(acquire_socket_object(ctx, this_.o, &socket_holder));

    arg = KOS_array_read(ctx, args_obj, 0);
    TRY_OBJID(arg);

    if (GET_OBJ_TYPE(arg) != OBJ_BOOLEAN) {
        KOS_raise_printf(ctx, "blocking is a boolean, cannot set %s",
                         KOS_get_type_name(GET_OBJ_TYPE(arg)));
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }

    blocking = KOS_get_bool(arg);

    KOS_suspend_context(ctx);

    reset_last_error();

#ifdef _WIN32
    {
        unsigned long non_blocking = blocking ? 0 : 1;

        if (ioctlsocket(get_socket(socket_holder), FIONBIO, &non_blocking) == 0)
            socket_holder->blocking = blocking;
        else
            saved_errno = get_error();
    }
#else
    {
        int flags = fcntl(get_socket(socket_holder), F_GETFL);

        if (flags == -1)
            saved_errno = get_error();
        else {
            if (blocking)
                flags &= ~O_NONBLOCK;
            else
                flags |= O_NONBLOCK;

            if (fcntl(get_socket(socket_holder), F_SETFL, flags) == -1)
                saved_errno = get_error();
        }
    }
#endif

    KOS_resume_context(ctx);

    if (saved_errno) {
        KOS_raise_errno_value(ctx, "fcntl", saved_errno);
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }

cleanup:
    release_socket(socket_holder);

    this_.o = KOS_destroy_top_local(ctx, &this_);

    return error ? KOS_BADPTR : this_.o;
}

static int send_loop(KOS_SOCKET              socket_fd,
                     const char             *data,
                     DATA_LEN                size,
                     int                     flags,
                     const KOS_GENERIC_ADDR *addr,
                     ADDR_LEN                addr_len)
{
    const int send_timeout_sec = 30;
    int       num_sent;

    for (;;) {
#ifdef _WIN32
        TIMEVAL        timeout;
        int            nfds = 0;
#else
        struct timeval timeout;
        int            nfds = socket_fd + 1;
#endif
        fd_set         fds;

        reset_last_error();

        if (addr)
            num_sent = sendto(socket_fd, data, size, flags, &addr->addr, addr_len);
        else
            num_sent = send(socket_fd, data, size, flags);

        if ((DATA_LEN)num_sent == size)
            break;

        if (num_sent >= 0) {
            data += num_sent;
            size -= num_sent;
        }

        if (num_sent < 0) {
            const int error = get_error();

            /* If the socket is non-blocking, the error will indicate that the send
             * buffer is full and we need to wait to send more data.
             */
#ifdef _WIN32
            if (error != WSAEWOULDBLOCK)
                break;
#else
            if (error != EAGAIN && error != EWOULDBLOCK)
                break;
#endif
        }

        reset_last_error();

        FD_ZERO(&fds);
        FD_SET(socket_fd, &fds);

        timeout.tv_sec  = (TIME_FRAGMENT)send_timeout_sec;
        timeout.tv_usec = 0;

        nfds = select(nfds, KOS_NULL, &fds, KOS_NULL, &timeout);

        if (nfds < 0)
            break;
    }

    return num_sent;
}

static int send_one_object(KOS_CONTEXT             ctx,
                           KOS_OBJ_ID              obj_id,
                           int                     flags,
                           KOS_SOCKET_HOLDER      *socket_holder,
                           KOS_VECTOR             *cstr,
                           KOS_LOCAL              *print_args,
                           const KOS_GENERIC_ADDR *addr,
                           ADDR_LEN                addr_len)
{
    KOS_LOCAL obj;
    int       saved_errno = 0;
    int       error       = KOS_SUCCESS;

    KOS_init_local_with(ctx, &obj, obj_id);

    if (GET_OBJ_TYPE(obj.o) == OBJ_BUFFER) {

        const size_t to_write = (size_t)KOS_get_buffer_size(obj.o);

        if (to_write > 0) {

            int64_t        num_writ = 0;
            const uint8_t *data     = KOS_buffer_data_const(obj.o);

            /* Make a copy in case GC moves the buffer storage when context is suspended */
            if (kos_is_heap_object(KOS_atomic_read_relaxed_obj(OBJPTR(BUFFER, obj.o)->data))) {

                if (KOS_vector_resize(cstr, to_write)) {
                    KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);
                    RAISE_ERROR(KOS_ERROR_EXCEPTION);
                }

                memcpy(cstr->buffer, data, to_write);
                data = (uint8_t *)cstr->buffer;
            }
            else {
                assert(kos_is_tracked_object(KOS_atomic_read_relaxed_obj(OBJPTR(BUFFER, obj.o)->data)));
            }

            KOS_suspend_context(ctx);

            num_writ = send_loop(get_socket(socket_holder),
                                 (const char *)data,
                                 (DATA_LEN)to_write,
                                 flags,
                                 addr,
                                 addr_len);

            assert((num_writ < 0) || ((size_t)num_writ == to_write));

            if (num_writ < 0)
                saved_errno = get_error();

            KOS_resume_context(ctx);
        }
    }
    else if (GET_OBJ_TYPE(obj.o) == OBJ_STRING) {

        int64_t num_writ = 0;

        if (IS_BAD_PTR(print_args->o)) {
            print_args->o = KOS_new_array(ctx, 1);
            TRY_OBJID(print_args->o);
        }

        TRY(KOS_array_write(ctx, print_args->o, 0, obj.o));

        TRY(KOS_print_to_cstr_vec(ctx, print_args->o, KOS_DONT_QUOTE, cstr, " ", 1));

        if (cstr->size) {
            KOS_suspend_context(ctx);

            num_writ = send_loop(get_socket(socket_holder),
                                 cstr->buffer,
                                 (DATA_LEN)(cstr->size - 1),
                                 flags,
                                 addr,
                                 addr_len);

            assert((num_writ < 0) || ((size_t)num_writ == cstr->size - 1));

            if (num_writ < 0)
                saved_errno = get_error();

            KOS_resume_context(ctx);
        }
    }
    else
        RAISE_EXCEPTION_STR(str_err_not_buffer_or_str);

    cstr->size = 0;

    if (saved_errno) {
        KOS_raise_errno_value(ctx, "send", saved_errno);
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }

cleanup:
    KOS_destroy_top_local(ctx, &obj);

    return error;
}

/* @item net socket.prototype.write()
 *
 *     socket.prototype.write(values...)
 *
 * Sends strings or buffers containing bytes through a connected socket.
 *
 * Each argument is either a buffer or a string object.  Empty buffers
 * or strings are ignored and nothing is sent through the socket.
 *
 * If an argument is a string, it is converted to UTF-8 bytes representation
 * before being sent.
 *
 * Invoking this function without any arguments doesn't send anything
 * through the socket but ensures that the socket object is correct.
 *
 * Returns the socket itself (`this`).
 *
 * On error throws an exception.
 */
static KOS_OBJ_ID kos_write(KOS_CONTEXT ctx,
                            KOS_OBJ_ID  this_obj,
                            KOS_OBJ_ID  args_obj)
{
    KOS_VECTOR         cstr;
    KOS_LOCAL          print_args;
    KOS_LOCAL          args;
    KOS_LOCAL          this_;
    KOS_SOCKET_HOLDER *socket_holder = KOS_NULL;
    const uint32_t     num_args      = KOS_get_array_size(args_obj);
    uint32_t           i_arg;
    int                error;

    KOS_vector_init(&cstr);

    KOS_init_locals(ctx, &print_args, &args, &this_, kos_end_locals);

    args.o  = args_obj;
    this_.o = this_obj;

    TRY(acquire_socket_object(ctx, this_.o, &socket_holder));

    for (i_arg = 0; i_arg < num_args; i_arg++) {

        const KOS_OBJ_ID arg = KOS_array_read(ctx, args.o, i_arg);
        TRY_OBJID(arg);

        TRY(send_one_object(ctx, arg, 0, socket_holder, &cstr, &print_args, KOS_NULL, 0));
    }

cleanup:
    release_socket(socket_holder);

    KOS_vector_destroy(&cstr);

    this_.o = KOS_destroy_top_locals(ctx, &print_args, &this_);

    return error ? KOS_BADPTR : this_.o;
}

static const KOS_CONVERT send_args[3] = {
    KOS_DEFINE_MANDATORY_ARG(str_data                  ),
    KOS_DEFINE_OPTIONAL_ARG( str_flags, TO_SMALL_INT(0)),
    KOS_DEFINE_TAIL_ARG()
};

/* @item net socket.prototype.send()
 *
 *     socket.prototype.send(data, flags = 0)
 *
 * Send a string or a buffer containing bytes through a connected socket.
 *
 * `data` is either a buffer or a string object.  Empty buffers
 * or strings are ignored and nothing is sent through the socket.
 *
 * If `data` is a string, it is converted to UTF-8 bytes representation
 * before being sent.
 *
 * `flags` specifies bit flag options for receiving data.  Possible bit flags are
 * `MSG_OOB` and `MSG_PEEK`.
 *
 * Returns the socket itself (`this`).
 *
 * On error throws an exception.
 */
static KOS_OBJ_ID kos_send(KOS_CONTEXT ctx,
                           KOS_OBJ_ID  this_obj,
                           KOS_OBJ_ID  args_obj)
{
    KOS_VECTOR         cstr;
    KOS_LOCAL          print_args;
    KOS_LOCAL          args;
    KOS_LOCAL          this_;
    KOS_OBJ_ID         arg;
    KOS_SOCKET_HOLDER *socket_holder = KOS_NULL;
    int64_t            flags64       = 0;
    int                error;

    assert(KOS_get_array_size(args_obj) >= 2);

    KOS_vector_init(&cstr);

    KOS_init_locals(ctx, &print_args, &args, &this_, kos_end_locals);

    args.o  = args_obj;
    this_.o = this_obj;

    TRY(acquire_socket_object(ctx, this_.o, &socket_holder));

    arg = KOS_array_read(ctx, args.o, 1);
    TRY_OBJID(arg);

    if ( ! IS_NUMERIC_OBJ(arg)) {
        KOS_raise_printf(ctx, "flags argument is %s but expected integer",
                         KOS_get_type_name(GET_OBJ_TYPE(arg)));
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }
    else
        TRY(KOS_get_integer(ctx, arg, &flags64));

    if (flags64 & (MSG_OOB | MSG_PEEK)) {
        KOS_raise_printf(ctx, "flags argument 0x%" PRIx64 " contains unrecognized bits", flags64);
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }

    arg = KOS_array_read(ctx, args.o, 0);
    TRY_OBJID(arg);

    TRY(send_one_object(ctx, arg, (int)flags64, socket_holder, &cstr, &print_args, KOS_NULL, 0));

cleanup:
    release_socket(socket_holder);

    KOS_vector_destroy(&cstr);

    this_.o = KOS_destroy_top_locals(ctx, &print_args, &this_);

    return error ? KOS_BADPTR : this_.o;
}

static const KOS_CONVERT sendto_args[5] = {
    { KOS_CONST_ID(str_address), KOS_BADPTR,      0, 0, KOS_NATIVE_STRING_PTR },
    { KOS_CONST_ID(str_port),    KOS_BADPTR,      0, 0, KOS_NATIVE_SKIP       },
    { KOS_CONST_ID(str_data),    KOS_BADPTR,      0, 0, KOS_NATIVE_SKIP       },
    { KOS_CONST_ID(str_flags),   TO_SMALL_INT(0), 0, 0, KOS_NATIVE_SKIP       },
    KOS_DEFINE_TAIL_ARG()
};

/* @item net socket.prototype.sendto()
 *
 *     socket.prototype.sendto(address, port, data, flags = 0)
 *
 * Send a string or a buffer containing bytes through a remote address.
 *
 * `address` specifies the IP address to send data to.  For IPv4 and IPv6 sockets this is
 * a hostname or a numeric IP address.
 *
 * `port` specifies the remote port.  It is an integer value from 1 to 65535 or
 * a string which is the name of the service.
 *
 * `data` is either a buffer or a string object.  Empty buffers
 * or strings are ignored and nothing is sent through the socket.
 *
 * If `data` is a string, it is converted to UTF-8 bytes representation
 * before being sent.
 *
 * `flags` specifies bit flag options for receiving data.  Possible bit flags are
 * `MSG_OOB` and `MSG_PEEK`.
 *
 * Returns the socket itself (`this`).
 *
 * On error throws an exception.
 */
static KOS_OBJ_ID kos_sendto(KOS_CONTEXT ctx,
                             KOS_OBJ_ID  this_obj,
                             KOS_OBJ_ID  args_obj)
{
    struct KOS_MEMPOOL_S alloc;
    KOS_VECTOR           port_cstr;
    KOS_VECTOR           cstr;
    KOS_LOCAL            print_args;
    KOS_LOCAL            args;
    KOS_LOCAL            this_;
    struct addrinfo     *address_info  = KOS_NULL;
    struct addrinfo      hints;
    int64_t              flags64       = 0;
    KOS_OBJ_ID           arg;
    char                *address_cstr  = KOS_NULL;
    KOS_SOCKET_HOLDER   *socket_holder = KOS_NULL;
    int                  error;

    assert(KOS_get_array_size(args_obj) >= 4);

    KOS_vector_init(&cstr);
    KOS_vector_init(&port_cstr);
    KOS_mempool_init_small(&alloc, 512U);

    KOS_init_locals(ctx, &print_args, &args, &this_, kos_end_locals);

    args.o  = args_obj;
    this_.o = this_obj;

    TRY(KOS_extract_native_from_array(ctx, args_obj, "argument", sendto_args, &alloc, &address_cstr));

    assert(KOS_get_array_size(args_obj) >= 2);
    {
        const KOS_OBJ_ID port_arg = KOS_array_read(ctx, args_obj, 1);
        TRY_OBJID(port_arg);
        TRY(get_port(ctx, port_arg, &port_cstr));
    }

    TRY(acquire_socket_object(ctx, this_.o, &socket_holder));

    KOS_suspend_context(ctx);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = socket_holder->family;
    hints.ai_flags  = AI_DEFAULT;

    error = getaddrinfo(address_cstr, port_cstr.buffer, &hints, &address_info);

    if (error) {
        const char *error_str = gai_strerror(error);

        KOS_resume_context(ctx);

        KOS_raise_exception_cstring(ctx, error_str);
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }

    KOS_resume_context(ctx);

    arg = KOS_array_read(ctx, args.o, 3);
    TRY_OBJID(arg);

    if ( ! IS_NUMERIC_OBJ(arg)) {
        KOS_raise_printf(ctx, "flags argument is %s but expected integer",
                         KOS_get_type_name(GET_OBJ_TYPE(arg)));
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }
    else
        TRY(KOS_get_integer(ctx, arg, &flags64));

    if (flags64 & (MSG_OOB | MSG_PEEK)) {
        KOS_raise_printf(ctx, "flags argument 0x%" PRIx64 " contains unrecognized bits", flags64);
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }

    arg = KOS_array_read(ctx, args.o, 2);
    TRY_OBJID(arg);

    TRY(send_one_object(ctx, arg, (int)flags64, socket_holder, &cstr, &print_args,
                        (const KOS_GENERIC_ADDR *)address_info->ai_addr,
                        (ADDR_LEN)address_info->ai_addrlen));

cleanup:
    release_socket(socket_holder);

    if (address_info)
        freeaddrinfo(address_info);

    KOS_mempool_destroy(&alloc);
    KOS_vector_destroy(&port_cstr);
    KOS_vector_destroy(&cstr);

    this_.o = KOS_destroy_top_locals(ctx, &print_args, &this_);

    return error ? KOS_BADPTR : this_.o;
}

#ifdef _WIN32
typedef BOOL SOCK_OPT_BOOL;
#else
typedef int SOCK_OPT_BOOL;
#endif

static KOS_OBJ_ID getsockopt_bool(KOS_CONTEXT        ctx,
                                  KOS_SOCKET_HOLDER *socket_holder,
                                  int                level,
                                  int                option)
{
    SOCK_OPT_BOOL bool_value  = 0;
    ADDR_LEN      opt_size    = (ADDR_LEN)sizeof(bool_value);
    int           error       = 0;
    int           saved_errno = 0;

    KOS_suspend_context(ctx);

    reset_last_error();

    error = getsockopt(get_socket(socket_holder),
                       level,
                       option,
                       (char *)&bool_value,
                       &opt_size);

    if (error)
        saved_errno = get_error();

    KOS_resume_context(ctx);

    if (error) {
        KOS_raise_errno_value(ctx, "setsockopt", saved_errno);
        return KOS_BADPTR;
    }

    return KOS_BOOL(bool_value);
}

static KOS_OBJ_ID getsockopt_int(KOS_CONTEXT        ctx,
                                 KOS_SOCKET_HOLDER *socket_holder,
                                 int                level,
                                 int                option)
{
    ADDR_LEN opt_size    = (ADDR_LEN)sizeof(int);
    int      int_value   = 0;
    int      error       = 0;
    int      saved_errno = 0;

    KOS_suspend_context(ctx);

    reset_last_error();

    error = getsockopt(get_socket(socket_holder),
                       level,
                       option,
                       (char *)&int_value,
                       &opt_size);

    if (error)
        saved_errno = get_error();

    KOS_resume_context(ctx);

    if (error) {
        KOS_raise_errno_value(ctx, "setsockopt", saved_errno);
        return KOS_BADPTR;
    }

    return KOS_new_int(ctx, int_value);
}

static KOS_OBJ_ID getsockopt_time(KOS_CONTEXT        ctx,
                                  KOS_SOCKET_HOLDER *socket_holder,
                                  int                level,
                                  int                option)
{
#ifdef _WIN32
    DWORD          time_value;
#else
    struct timeval time_value;
#endif
    ADDR_LEN       opt_size    = (ADDR_LEN)sizeof(time_value);
    int            error       = 0;
    int            saved_errno = 0;

    memset(&time_value, 0, sizeof(time_value));

    KOS_suspend_context(ctx);

    reset_last_error();

    error = getsockopt(get_socket(socket_holder),
                       level,
                       option,
                       (char *)&time_value,
                       &opt_size);

    if (error)
        saved_errno = get_error();

    KOS_resume_context(ctx);

    if (error) {
        KOS_raise_errno_value(ctx, "setsockopt", saved_errno);
        return KOS_BADPTR;
    }

#ifdef _WIN32
    return KOS_new_float(ctx, (double)time_value / 1000.0);
#else
    return KOS_new_float(ctx, (double)time_value.tv_sec + (double)time_value.tv_usec / 1000000.0);
#endif
}

static const KOS_CONVERT getaddrinfo_args[3] = {
    KOS_DEFINE_MANDATORY_ARG(str_address),
    KOS_DEFINE_OPTIONAL_ARG( str_port, KOS_CONST_ID(str_empty)),
    KOS_DEFINE_TAIL_ARG()
};

/* @item net getaddrinfo()
 *
 *     getaddrinfo(address, port = void)
 *
 * Returns information about an address.
 *
 * `address` is a string with a hostname or a numeric IPv4 or IPv6 address.
 * `address` can be an empty string, in which case only port is looked up.
 *
 * `port` is a string with port number or service name.
 * `port` can be an empty string, in which case only address is looked up.
 *
 * Returns an array of objects describing translated numeric address and port
 * for each address family and protocol.  Each entry in the returned array has
 * the following properties:
 *  - address - a string containing numeric address (depending on family).
 *  - port - integer with numeric port.
 *  - family - integer with numeric address family, for example `net.AF_INET` or `net.AF_INET6`.
 *  - socktype - integer with possible socket type, for example `net.SOCK_STREAM` or `net.SOCK_DGRAM`.
 *  - protocol - integer with possible protocol, for example `net.IPPROTO_TCP` or `net.IPPROTO_UDP`.
 *
 * On error throws an exception.
 *
 * Example:
 *
 *     > getaddrinfo("localhost", "http") -> each(print)
 *     {"type": 2, "domain": 30, "port": 80, "address": "::1", "protocol": 17}
 *     {"type": 1, "domain": 30, "port": 80, "address": "::1", "protocol": 6}
 *     {"type": 2, "domain": 2, "port": 80, "address": "127.0.0.1", "protocol": 17}
 *     {"type": 1, "domain": 2, "port": 80, "address": "127.0.0.1", "protocol": 6}
 */
static KOS_OBJ_ID kos_getaddrinfo(KOS_CONTEXT ctx,
                                  KOS_OBJ_ID  this_obj,
                                  KOS_OBJ_ID  args_obj)
{
    KOS_LOCAL            addr_infos;
    KOS_LOCAL            val;
    KOS_LOCAL            one_info;
    struct addrinfo     *info      = KOS_NULL;
    struct addrinfo     *cur_info;
    KOS_VECTOR           address_cstr;
    KOS_VECTOR           port_cstr;
    unsigned             num_addrs = 0;
    int                  i;
    int                  error;

    assert(KOS_get_array_size(args_obj) >= 2);

    KOS_vector_init(&address_cstr);
    KOS_vector_init(&port_cstr);

    KOS_init_locals(ctx, &one_info, &val, &addr_infos, kos_end_locals);

    one_info.o = args_obj;

    val.o = KOS_array_read(ctx, one_info.o, 0);
    TRY_OBJID(val.o);

    if (val.o != KOS_VOID) {
        if (GET_OBJ_TYPE(val.o) != OBJ_STRING) {
            KOS_raise_printf(ctx, "address argument is %s but expected string",
                             KOS_get_type_name(GET_OBJ_TYPE(val.o)));
            RAISE_ERROR(KOS_ERROR_EXCEPTION);
        }

        TRY(KOS_string_to_cstr_vec(ctx, val.o, &address_cstr));
    }
    if ( ! address_cstr.size || ! address_cstr.buffer[0])
        address_cstr.buffer = KOS_NULL;

    val.o = KOS_array_read(ctx, one_info.o, 1);
    TRY_OBJID(val.o);

    if (val.o != KOS_VOID) {
        if (GET_OBJ_TYPE(val.o) == OBJ_STRING)
            TRY(KOS_string_to_cstr_vec(ctx, val.o, &port_cstr));
        else if (GET_OBJ_TYPE(val.o) <= OBJ_INTEGER) {
            int64_t  int_value;
            unsigned len;

            TRY(KOS_get_integer(ctx, val.o, &int_value));

            if (int_value < 0 || int_value > 0xFFFF) {
                KOS_raise_printf(ctx, "port %" PRId64 " is out of range", int_value);
                RAISE_ERROR(KOS_ERROR_EXCEPTION);
            }

            TRY(KOS_vector_resize(&port_cstr, 6));

            len = (unsigned)snprintf(port_cstr.buffer, 6, "%u", (unsigned)(int)int_value);
            TRY(KOS_vector_resize(&port_cstr, len + 1));
        }
        else {
            KOS_raise_printf(ctx, "port argument is %s but expected string or integer",
                             KOS_get_type_name(GET_OBJ_TYPE(val.o)));
            RAISE_ERROR(KOS_ERROR_EXCEPTION);
        }
    }

    if ( ! port_cstr.size || ! port_cstr.buffer[0])
        port_cstr.buffer = KOS_NULL;

    KOS_suspend_context(ctx);

    error = getaddrinfo(address_cstr.buffer,
                        port_cstr.buffer,
                        KOS_NULL,
                        &info);

    KOS_resume_context(ctx);

    if (error) {
        KOS_raise_printf(ctx, "getaddrinfo: %s", gai_strerror(error));
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }

    for (cur_info = info; cur_info; cur_info = cur_info->ai_next)
        ++num_addrs;

    addr_infos.o = KOS_new_array(ctx, num_addrs);
    TRY_OBJID(addr_infos.o);

    for (cur_info = info, i = 0; cur_info; cur_info = cur_info->ai_next, ++i) {

        one_info.o = KOS_new_object(ctx);
        TRY_OBJID(one_info.o);

        TRY(KOS_array_write(ctx, addr_infos.o, i, one_info.o));

        TRY(add_address_desc(ctx,
                             one_info.o,
                             (const KOS_GENERIC_ADDR *)cur_info->ai_addr,
                             (ADDR_LEN)cur_info->ai_addrlen));

        val.o = KOS_new_int(ctx, (int64_t)cur_info->ai_family);
        TRY_OBJID(val.o);

        TRY(KOS_set_property(ctx, one_info.o, KOS_CONST_ID(str_domain), val.o));

        val.o = KOS_new_int(ctx, (int64_t)cur_info->ai_socktype);
        TRY_OBJID(val.o);

        TRY(KOS_set_property(ctx, one_info.o, KOS_CONST_ID(str_type), val.o));

        val.o = KOS_new_int(ctx, (int64_t)cur_info->ai_protocol);
        TRY_OBJID(val.o);

        TRY(KOS_set_property(ctx, one_info.o, KOS_CONST_ID(str_protocol), val.o));
    }

cleanup:
    if (info)
        freeaddrinfo(info);

    addr_infos.o = KOS_destroy_top_locals(ctx, &one_info, &addr_infos);

    KOS_vector_destroy(&port_cstr);
    KOS_vector_destroy(&address_cstr);

    return error ? KOS_BADPTR : addr_infos.o;
}

KOS_DECLARE_STATIC_CONST_STRING(str_option, "option");

static const KOS_CONVERT getsockopt_args[3] = {
    KOS_DEFINE_MANDATORY_ARG(str_level),
    KOS_DEFINE_MANDATORY_ARG(str_option),
    KOS_DEFINE_TAIL_ARG()
};

/* @item net socket.prototype.getsockopt()
 *
 *     socket.prototype.getsockopt(level, option)
 *
 * Returns value of a socket option.
 *
 * `level` is protocol level at which the option is set, e.g.:
 * `SOL_SOCKET`, `IPPROTO_IP`, `IPPROTO_TCP`, `IPPROTO_UDP`, `IPPROTO_ICMP`.
 *
 * `option` is an integer specifying the option to retrieve.
 *
 * Possible options include the following constants in the `net` module,
 * and return values of the following types:
 *
 *  - SO_BROADCAST - bool
 *  - SO_DEBUG - bool
 *  - SO_DONTROUTE - bool
 *  - SO_KEEPALIVE - bool
 *  - SO_OOBINLINE - bool
 *  - SO_RCVBUF - integer
 *  - SO_RCVTIMEO - float (milliseconds)
 *  - SO_REUSEADDR - bool
 *  - SO_REUSEPORT - bool
 *  - SO_SNDBUF - integer
 *  - SO_SNDTIMEO - float (milliseconds)
 */
static KOS_OBJ_ID kos_getsockopt(KOS_CONTEXT ctx,
                                 KOS_OBJ_ID  this_obj,
                                 KOS_OBJ_ID  args_obj)
{
    KOS_LOCAL          args;
    KOS_LOCAL          this_;
    KOS_LOCAL          value;
    KOS_SOCKET_HOLDER *socket_holder;
    int64_t            level;
    int64_t            option;
    int                error;

    assert(KOS_get_array_size(args_obj) > 0);

    KOS_init_local(     ctx, &value);
    KOS_init_local_with(ctx, &this_, this_obj);
    KOS_init_local_with(ctx, &args,  args_obj);

    TRY(acquire_socket_object(ctx, this_.o, &socket_holder));

    value.o = KOS_array_read(ctx, args.o, 0);
    TRY_OBJID(value.o);

    if (GET_OBJ_TYPE(value.o) > OBJ_INTEGER) {
        KOS_raise_printf(ctx, "level argument is %s but expected integer",
                         KOS_get_type_name(GET_OBJ_TYPE(value.o)));
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }

    TRY(KOS_get_integer(ctx, value.o, &level));

    value.o = KOS_array_read(ctx, args.o, 1);
    TRY_OBJID(value.o);

    if (GET_OBJ_TYPE(value.o) > OBJ_INTEGER) {
        KOS_raise_printf(ctx, "option argument is %s but expected integer",
                         KOS_get_type_name(GET_OBJ_TYPE(value.o)));
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }

    TRY(KOS_get_integer(ctx, value.o, &option));

    if (level == SOL_SOCKET) {
        switch (option) {
            case SO_BROADCAST:
                /* fall through */
            case SO_DEBUG:
                /* fall through */
            case SO_DONTROUTE:
                /* fall through */
            case SO_KEEPALIVE:
                /* fall through */
            case SO_OOBINLINE:
                /* fall through */
            case SO_REUSEADDR:
                value.o = getsockopt_bool(ctx, socket_holder, (int)level, (int)option);
                break;

            case SO_REUSEPORT:
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
                value.o = getsockopt_bool(ctx, socket_holder, (int)level, (int)option);
#else
                value.o = KOS_FALSE;
#endif
                break;

            /* TODO
            case SO_LINGER:
            */

            case SO_RCVBUF:
                /* fall through */
            case SO_SNDBUF:
                value.o = getsockopt_int(ctx, socket_holder, (int)level, (int)option);
                break;

            case SO_RCVTIMEO:
                /* fall through */
            case SO_SNDTIMEO:
                value.o = getsockopt_time(ctx, socket_holder, (int)level, (int)option);
                break;

            default:
                KOS_raise_printf(ctx, "unknown option %" PRId64, option);
                RAISE_ERROR(KOS_ERROR_EXCEPTION);
        }
    }
    else {
        KOS_raise_printf(ctx, "unknown level %" PRId64, level);
        value.o = KOS_BADPTR;
    }

    if (value.o == KOS_BADPTR)
        error = KOS_ERROR_EXCEPTION;

cleanup:
    release_socket(socket_holder);

    value.o = KOS_destroy_top_locals(ctx, &args, &value);

    return error ? KOS_BADPTR : value.o;
}

static int setsockopt_bool(KOS_CONTEXT        ctx,
                           KOS_SOCKET_HOLDER *socket_holder,
                           int                level,
                           int                option,
                           KOS_OBJ_ID         value)
{
    int64_t       val64;
    SOCK_OPT_BOOL bool_value;
    int           error       = 0;
    int           saved_errno = 0;

    if (GET_OBJ_TYPE(value) == OBJ_BOOLEAN)
        val64 = (int)KOS_get_bool(value);
    else if ( ! IS_NUMERIC_OBJ(value)) {
        KOS_raise_printf(ctx, "value argument is %s but expected integer",
                         KOS_get_type_name(GET_OBJ_TYPE(value)));
        return KOS_ERROR_EXCEPTION;
    }
    else {
        error = KOS_get_integer(ctx, value, &val64);
        if (error)
            return error;
    }

    bool_value = (SOCK_OPT_BOOL)(val64 != 0);

    KOS_suspend_context(ctx);

    reset_last_error();

    error = setsockopt(get_socket(socket_holder),
                       level,
                       option,
                       (const char *)&bool_value,
                       (ADDR_LEN)sizeof(bool_value));

    if (error)
        saved_errno = get_error();

    KOS_resume_context(ctx);

    if (error) {
        KOS_raise_errno_value(ctx, "setsockopt", saved_errno);
        return KOS_ERROR_EXCEPTION;
    }

    return KOS_SUCCESS;
}

static int setsockopt_int(KOS_CONTEXT        ctx,
                          KOS_SOCKET_HOLDER *socket_holder,
                          int                level,
                          int                option,
                          KOS_OBJ_ID         value)
{
    int64_t val64;
    int     int_value;
    int     error       = 0;
    int     saved_errno = 0;

    if ( ! IS_NUMERIC_OBJ(value)) {
        KOS_raise_printf(ctx, "value argument is %s but expected integer",
                         KOS_get_type_name(GET_OBJ_TYPE(value)));
        return KOS_ERROR_EXCEPTION;
    }

    error = KOS_get_integer(ctx, value, &val64);
    if (error)
        return error;

    int_value = (int)val64;

    KOS_suspend_context(ctx);

    reset_last_error();

    error = setsockopt(get_socket(socket_holder),
                       level,
                       option,
                       (const char *)&int_value,
                       (ADDR_LEN)sizeof(int_value));

    if (error)
        saved_errno = get_error();

    KOS_resume_context(ctx);

    if (error) {
        KOS_raise_errno_value(ctx, "setsockopt", saved_errno);
        return KOS_ERROR_EXCEPTION;
    }

    return KOS_SUCCESS;
}

static int setsockopt_time(KOS_CONTEXT        ctx,
                           KOS_SOCKET_HOLDER *socket_holder,
                           int                level,
                           int                option,
                           KOS_OBJ_ID         value)
{
    KOS_NUMERIC    numeric = KOS_get_numeric(value);
    uint64_t       tv_usec;
#ifdef _WIN32
    DWORD          time_value;
#else
    struct timeval time_value;
#endif
    int            positive    = 0;
    int            error       = 0;
    int            saved_errno = 0;

    switch (numeric.type) {

        case KOS_INTEGER_VALUE:
            tv_usec  = (uint64_t)numeric.u.i * 1000000U;
            positive = numeric.u.i >= 0;
            break;

        case KOS_FLOAT_VALUE:
            tv_usec  = (uint64_t)floor(numeric.u.d * 1000000.0);
            positive = numeric.u.d >= 0;
            break;

        default:
            KOS_raise_printf(ctx, "value argument is %s but expected integer",
                             KOS_get_type_name(GET_OBJ_TYPE(value)));
            return KOS_ERROR_EXCEPTION;
    }

    if ( ! positive || (tv_usec / 1000U) > 0x7FFFFFFFU) {
        KOS_raise_printf(ctx, "value argument %" PRIu64 " us is out of range", tv_usec);
        return KOS_ERROR_EXCEPTION;
    }

#ifdef _WIN32
    time_value = (DWORD)(tv_usec + 999U / 1000U);
#else
    time_value.tv_sec  = (TIME_FRAGMENT)(tv_usec / 1000000U);
    time_value.tv_usec = (TIME_FRAGMENT)(tv_usec % 1000000U);
#endif

    KOS_suspend_context(ctx);

    reset_last_error();

    error = setsockopt(get_socket(socket_holder),
                       level,
                       option,
                       (const char *)&time_value,
                       (ADDR_LEN)sizeof(time_value));

    if (error)
        saved_errno = get_error();

    KOS_resume_context(ctx);

    if (error) {
        KOS_raise_errno_value(ctx, "setsockopt", saved_errno);
        return KOS_ERROR_EXCEPTION;
    }

    return KOS_SUCCESS;
}

KOS_DECLARE_STATIC_CONST_STRING(str_value, "value");

static const KOS_CONVERT setsockopt_args[4] = {
    KOS_DEFINE_MANDATORY_ARG(str_level),
    KOS_DEFINE_MANDATORY_ARG(str_option),
    KOS_DEFINE_MANDATORY_ARG(str_value),
    KOS_DEFINE_TAIL_ARG()
};

/* @item net socket.prototype.setsockopt()
 *
 *     socket.prototype.setsockopt(level, option, value)
 *
 * Sets a socket option.
 *
 * `level` is protocol level at which the option is set, e.g.:
 * `SOL_SOCKET`, `IPPROTO_IP`, `IPPROTO_TCP`, `IPPROTO_UDP`, `IPPROTO_ICMP`.
 *
 * `option` is an integer specifying the option to set and
 *
 * `value` is the value to set for this option.
 *
 * See `socket.prototype.getsockopt()` for list of possible
 * options and corresponding `value` types.
 *
 * Returns the socket itself (`this`).
 *
 * On error throws an exception.
 */
static KOS_OBJ_ID kos_setsockopt(KOS_CONTEXT ctx,
                                 KOS_OBJ_ID  this_obj,
                                 KOS_OBJ_ID  args_obj)
{
    KOS_LOCAL          args;
    KOS_LOCAL          value;
    KOS_LOCAL          this_;
    KOS_SOCKET_HOLDER *socket_holder;
    int64_t            level;
    int64_t            option;
    int                error;

    assert(KOS_get_array_size(args_obj) > 1);

    KOS_init_local_with(ctx, &this_, this_obj);
    KOS_init_local(     ctx, &value);
    KOS_init_local_with(ctx, &args,  args_obj);

    TRY(acquire_socket_object(ctx, this_.o, &socket_holder));

    value.o = KOS_array_read(ctx, args.o, 0);
    TRY_OBJID(value.o);

    if (GET_OBJ_TYPE(value.o) > OBJ_INTEGER) {
        KOS_raise_printf(ctx, "level argument is %s but expected integer",
                         KOS_get_type_name(GET_OBJ_TYPE(value.o)));
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }

    TRY(KOS_get_integer(ctx, value.o, &level));

    value.o = KOS_array_read(ctx, args.o, 1);
    TRY_OBJID(value.o);

    if (GET_OBJ_TYPE(value.o) > OBJ_INTEGER) {
        KOS_raise_printf(ctx, "option argument is %s but expected integer",
                         KOS_get_type_name(GET_OBJ_TYPE(value.o)));
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }

    TRY(KOS_get_integer(ctx, value.o, &option));

    value.o = KOS_array_read(ctx, args.o, 2);
    TRY_OBJID(value.o);

    if (level == SOL_SOCKET) {
        switch (option) {
            case SO_BROADCAST:
                /* fall through */
            case SO_DEBUG:
                /* fall through */
            case SO_DONTROUTE:
                /* fall through */
            case SO_KEEPALIVE:
                /* fall through */
            case SO_OOBINLINE:
                /* fall through */
            case SO_REUSEADDR:
                TRY(setsockopt_bool(ctx, socket_holder, (int)level, (int)option, value.o));
                break;

            case SO_REUSEPORT:
                /* Ignore on OSes which don't support this */
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
                TRY(setsockopt_bool(ctx, socket_holder, (int)level, (int)option, value.o));
#endif
                break;

            /* TODO
            case SO_LINGER:
            */

            case SO_RCVBUF:
                /* fall through */
            case SO_SNDBUF:
                TRY(setsockopt_int(ctx, socket_holder, (int)level, (int)option, value.o));
                break;

            case SO_RCVTIMEO:
                /* fall through */
            case SO_SNDTIMEO:
                TRY(setsockopt_time(ctx, socket_holder, (int)level, (int)option, value.o));
                break;

            default:
                KOS_raise_printf(ctx, "unknown option %" PRId64, option);
                RAISE_ERROR(KOS_ERROR_EXCEPTION);
        }
    }
    else {
        KOS_raise_printf(ctx, "unknown level %" PRId64, level);
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }

cleanup:
    release_socket(socket_holder);

    this_.o = KOS_destroy_top_locals(ctx, &args, &this_);

    return error ? KOS_BADPTR : this_.o;
}

KOS_DECLARE_STATIC_CONST_STRING(str_how, "how");

static const KOS_CONVERT shutdown_args[2] = {
    { KOS_CONST_ID(str_how), TO_SMALL_INT(SHUT_RDWR), 0, 0, KOS_NATIVE_INT32 },
    KOS_DEFINE_TAIL_ARG()
};

/* @item net socket.prototype.shutdown()
 *
 *     socket.prototype.shutdown(how = SHUT_RDWR)
 *
 * Shuts down one or two directions of the connection.
 *
 * `how` specifies if only one direction of the connection is closed
 * (`SHUT_RD` or `SHUT_WR`) or both (`SHUT_RDWR`).
 *
 * Returns the socket itself (`this`).
 *
 * On error throws an exception.
 */
static KOS_OBJ_ID kos_shutdown(KOS_CONTEXT ctx,
                               KOS_OBJ_ID  this_obj,
                               KOS_OBJ_ID  args_obj)
{
    KOS_LOCAL          this_;
    KOS_SOCKET_HOLDER *socket_holder = KOS_NULL;
    int                saved_errno;
    int                error;
    int32_t            how           = 0;

    KOS_init_local_with(ctx, &this_, this_obj);

    TRY(KOS_extract_native_from_array(ctx, args_obj, "argument", shutdown_args, KOS_NULL, &how));

    TRY(acquire_socket_object(ctx, this_.o, &socket_holder));

    KOS_suspend_context(ctx);

    reset_last_error();

    error = shutdown(get_socket(socket_holder), how);

    saved_errno = get_error();

    KOS_resume_context(ctx);

    if (error) {
        KOS_raise_errno_value(ctx, "shutdown", saved_errno);
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }

cleanup:
    release_socket(socket_holder);

    this_.o = KOS_destroy_top_local(ctx, &this_);

    return error ? KOS_BADPTR : this_.o;
}

KOS_INIT_MODULE(net, 0)(KOS_CONTEXT ctx, KOS_OBJ_ID module_obj)
{
    int       error = KOS_SUCCESS;
    KOS_LOCAL module;
    KOS_LOCAL socket_proto;

    const KOS_CONVERT recv_args[4] = {
        KOS_DEFINE_OPTIONAL_ARG(str_size,   TO_SMALL_INT(4096)),
        KOS_DEFINE_OPTIONAL_ARG(str_buffer, KOS_VOID          ),
        KOS_DEFINE_OPTIONAL_ARG(str_flags,  TO_SMALL_INT(0)   ),
        KOS_DEFINE_TAIL_ARG()
    };

    const KOS_CONVERT wait_args[2] = {
        KOS_DEFINE_OPTIONAL_ARG(str_timeout_sec, KOS_VOID),
        KOS_DEFINE_TAIL_ARG()
    };

    KOS_init_debug_output();

    KOS_init_local_with(ctx, &module, module_obj);
    KOS_init_local(     ctx, &socket_proto);

#ifdef _WIN32
    {
        WSADATA info;

        KOS_suspend_context(ctx);

        error = WSAStartup(MAKEWORD(2, 2), &info);

        KOS_resume_context(ctx);

        if (error) {
            KOS_raise_last_error(ctx, "WSAStartup", (unsigned)error);
            RAISE_ERROR(KOS_ERROR_EXCEPTION);
        }
    }
#endif

    TRY_ADD_CONSTRUCTOR(    ctx, module.o,                 "socket",      kos_socket,      socket_args, &socket_proto.o);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, socket_proto.o, "accept",      kos_accept,      KOS_NULL);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, socket_proto.o, "bind",        kos_bind,        bind_args);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, socket_proto.o, "close",       kos_close,       KOS_NULL);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, socket_proto.o, "connect",     kos_connect,     connect_args);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, socket_proto.o, "getsockopt",  kos_getsockopt,  getsockopt_args);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, socket_proto.o, "listen",      kos_listen,      listen_args);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, socket_proto.o, "read",        kos_recv,        recv_args);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, socket_proto.o, "recv",        kos_recv,        recv_args);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, socket_proto.o, "recvfrom",    kos_recvfrom,    recv_args);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, socket_proto.o, "release",     kos_close,       KOS_NULL);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, socket_proto.o, "wait",        kos_wait,        wait_args);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, socket_proto.o, "send",        kos_send,        send_args);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, socket_proto.o, "sendto",      kos_sendto,      sendto_args);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, socket_proto.o, "setsockopt",  kos_setsockopt,  setsockopt_args);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, socket_proto.o, "shutdown",    kos_shutdown,    shutdown_args);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, socket_proto.o, "write",       kos_write,       KOS_NULL);

    TRY_ADD_MEMBER_PROPERTY(ctx, module.o, socket_proto.o, "blocking",    get_blocking,    KOS_NULL);

    TRY_ADD_FUNCTION(       ctx, module.o,                 "getaddrinfo", kos_getaddrinfo, getaddrinfo_args);

#ifndef _WIN32
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "AF_LOCAL",     AF_LOCAL);
#endif
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "AF_INET",      AF_INET);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "AF_INET6",     AF_INET6);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "AF_UNSPEC",    AF_UNSPEC);

    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "SOCK_STREAM",  SOCK_STREAM);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "SOCK_DGRAM",   SOCK_DGRAM);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "SOCK_RAW",     SOCK_RAW);

    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "SHUT_RD",      SHUT_RD);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "SHUT_RDWR",    SHUT_RDWR);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "SHUT_WR",      SHUT_WR);

    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "SOL_SOCKET",   SOL_SOCKET);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "IPPROTO_IP",   IPPROTO_IP);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "IPPROTO_TCP",  IPPROTO_TCP);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "IPPROTO_UDP",  IPPROTO_UDP);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "IPPROTO_ICMP", IPPROTO_ICMP);

    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "SO_BROADCAST", SO_BROADCAST);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "SO_DEBUG",     SO_DEBUG);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "SO_DONTROUTE", SO_DONTROUTE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "SO_KEEPALIVE", SO_KEEPALIVE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "SO_LINGER",    SO_LINGER);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "SO_OOBINLINE", SO_OOBINLINE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "SO_RCVBUF",    SO_RCVBUF);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "SO_RCVTIMEO",  SO_RCVTIMEO);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "SO_REUSEADDR", SO_REUSEADDR);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "SO_REUSEPORT", SO_REUSEPORT);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "SO_SNDBUF",    SO_SNDBUF);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "SO_SNDTIMEO",  SO_SNDTIMEO);

    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "MSG_OOB",      MSG_OOB);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "MSG_PEEK",     MSG_PEEK);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "MSG_WAITALL",  MSG_WAITALL);

cleanup:
    KOS_destroy_top_locals(ctx, &socket_proto, &module);

    return error;
}
