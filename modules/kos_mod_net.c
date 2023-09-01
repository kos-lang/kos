/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2023 Chris Dragan
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
#include <string.h>

#ifdef _WIN32
#   include <winsock2.h>
#   include <ws2tcpip.h>
#   define SHUT_RD   SD_RECEIVE
#   define SHUT_WR   SD_SEND
#   define SHUT_RDWR SD_BOTH
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

#ifdef _WIN32
typedef SOCKET KOS_SOCKET;
typedef int    DATA_LEN;
typedef int    ADDR_LEN;
typedef long   TIME_FRAGMENT;

#define KOS_INVALID_SOCKET ((KOS_SOCKET)INVALID_SOCKET)

#define reset_last_error() ((void)0)

static int get_error(void)
{
    return WSAGetLastError();
}
#else
typedef int       KOS_SOCKET;
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
KOS_DECLARE_STATIC_CONST_STRING(str_err_not_buffer,        "argument to socket.recv is not a buffer");
KOS_DECLARE_STATIC_CONST_STRING(str_err_not_buffer_or_str, "argument to socket.send is neither a buffer nor a string");
KOS_DECLARE_STATIC_CONST_STRING(str_err_too_many_to_read,  "requested read size exceeds buffer size limit");
KOS_DECLARE_STATIC_CONST_STRING(str_err_socket_not_open,   "socket not open or not a socket object");
KOS_DECLARE_STATIC_CONST_STRING(str_port,                  "port");
KOS_DECLARE_STATIC_CONST_STRING(str_socket,                "socket");
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

static int get_ip_address(KOS_CONTEXT        ctx,
                          KOS_SOCKET_HOLDER *socket_holder,
                          const char        *addr_cstr,
                          uint16_t           port,
                          KOS_GENERIC_ADDR  *addr,
                          ADDR_LEN          *addr_len)
{
    int error = KOS_SUCCESS;

#if 1
    if (addr_cstr[0]) {
        struct addrinfo  hint;
        struct addrinfo *info = KOS_NULL;

        memset(&hint, 0, sizeof(hint));

        hint.ai_family = socket_holder->family;

        KOS_suspend_context(ctx);

        error = getaddrinfo(addr_cstr,
                            KOS_NULL,
                            &hint,
                            &info);

        KOS_resume_context(ctx);

        if (error) {
            KOS_raise_printf(ctx, "getaddrinfo: %s", gai_strerror(error));
            RAISE_ERROR(KOS_ERROR_EXCEPTION);
        }
        else if (info->ai_addrlen > sizeof(*addr)) {
            KOS_DECLARE_STATIC_CONST_STRING(str_addrinfo, "getaddrinfo: address too long");

            KOS_raise_exception(ctx, KOS_CONST_ID(str_addrinfo));
            error = KOS_ERROR_EXCEPTION;
        }
        else {
            struct addrinfo *cur_info = info;

            for (; cur_info; cur_info = cur_info->ai_next)
                if (cur_info->ai_family == socket_holder->family)
                    break;

            if ( ! cur_info) {
                KOS_DECLARE_STATIC_CONST_STRING(str_addrinfo, "getaddrinfo: requested address family not available");

                freeaddrinfo(info);
                RAISE_EXCEPTION_STR(str_addrinfo);
            }

            if (socket_holder->family == AF_INET) {
                addr->inet          = *(struct sockaddr_in *)cur_info->ai_addr;
                addr->inet.sin_port = htons(port);
                *addr_len           = sizeof(addr->inet);
            }
            else {
                addr->inet6           = *(struct sockaddr_in6 *)cur_info->ai_addr;
                addr->inet6.sin6_port = htons(port);
                *addr_len             = sizeof(addr->inet6);
            }
        }

        freeaddrinfo(info);
    }
#else
    if (addr_cstr[0]) {
        struct hostent *ent;

        KOS_suspend_context(ctx);

        ent = gethostbyname(addr_cstr);

        error = ent ? KOS_SUCCESS : h_errno;

        KOS_resume_context(ctx);

        if (error) {
            KOS_raise_printf(ctx, "gethostbyname: %s", hstrerror(error));
            RAISE_ERROR(KOS_ERROR_EXCEPTION);
        }

        addr->inet.sin_family = AF_INET;
        addr->inet.sin_addr   = *(struct in_addr *)ent->h_addr;
        addr->inet.sin_port   = htons(port);
        *addr_len             = sizeof(addr->inet);
    }
#endif
    else {
        if (socket_holder->family == AF_INET) {
            addr->inet.sin_family = AF_INET;
            addr->inet.sin_port   = htons(port);
            *addr_len             = sizeof(addr->inet);
        }
        else {
            addr->inet6.sin6_family = AF_INET6;
            addr->inet6.sin6_port   = htons(port);
            *addr_len               = sizeof(addr->inet6);
        }
    }

cleanup:
    return error;
}

static int get_address(KOS_CONTEXT        ctx,
                       KOS_SOCKET_HOLDER *socket_holder,
                       const char        *addr_cstr,
                       uint16_t           port,
                       KOS_GENERIC_ADDR  *addr,
                       ADDR_LEN          *addr_len)
{
    int error = KOS_SUCCESS;

    memset(addr, 0, sizeof(*addr));

    if (socket_holder->family == AF_INET ||
        socket_holder->family == AF_INET6) {

        error = get_ip_address(ctx,
                               socket_holder,
                               addr_cstr,
                               port,
                               addr,
                               addr_len);
    }
    else {
        /* TODO AF_LOCAL */
        assert(0);
    }

    return error;
}

static KOS_OBJ_ID get_blocking(KOS_CONTEXT ctx,
                               KOS_OBJ_ID  this_obj,
                               KOS_OBJ_ID  args_obj);

static KOS_OBJ_ID set_blocking(KOS_CONTEXT ctx,
                               KOS_OBJ_ID  this_obj,
                               KOS_OBJ_ID  args_obj);

KOS_DECLARE_STATIC_CONST_STRING(str_domain,   "domain");
KOS_DECLARE_STATIC_CONST_STRING(str_type,     "type");
KOS_DECLARE_STATIC_CONST_STRING(str_protocol, "protocol");

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
 * `type` specifies the semantics of communication, e.g. `SOCK_STREAM`, `SOCK_DGRAM` or `SOCK_RAW`.
 * `protocol` specifies particular protocol, 0 typically indicates default protocol.
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

    if (socket_fd == -1)
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
 * - `address`: address of the remote host from which the connetion has been made.
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

    if (socket_fd == -1)
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

KOS_DECLARE_STATIC_CONST_STRING(str_empty, "");

static const KOS_CONVERT bind_args[3] = {
    { KOS_CONST_ID(str_address), KOS_CONST_ID(str_empty), 0, 0, KOS_NATIVE_STRING_PTR },
    { KOS_CONST_ID(str_port),    TO_SMALL_INT(0),         0, 0, KOS_NATIVE_UINT16     },
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
    KOS_GENERIC_ADDR     addr;
    ADDR_LEN             addr_len;
    KOS_LOCAL            this_;
    char                *address_cstr  = KOS_NULL;
    KOS_SOCKET_HOLDER   *socket_holder = KOS_NULL;
    int                  saved_errno;
    int                  error;
    uint16_t             port          = 0;

    KOS_init_local_with(ctx, &this_, this_obj);

    KOS_mempool_init_small(&alloc, 512U);

    TRY(KOS_extract_native_from_array(ctx, args_obj, "argument", bind_args, &alloc, &address_cstr, &port));

    TRY(acquire_socket_object(ctx, this_.o, &socket_holder));

    TRY(get_address(ctx, socket_holder, address_cstr, port, &addr, &addr_len));

    KOS_suspend_context(ctx);

    reset_last_error();

    error = bind(get_socket(socket_holder), &addr.addr, addr_len);

    saved_errno = get_error();

    KOS_resume_context(ctx);

    if (error) {
        KOS_raise_errno_value(ctx, "bind", saved_errno);
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }

cleanup:
    release_socket(socket_holder);

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
    { KOS_CONST_ID(str_port),    KOS_BADPTR, 0, 0, KOS_NATIVE_UINT16     },
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
 * `port` specifies the port to bind.  It is an integer value from 1 to 65535.
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
    KOS_GENERIC_ADDR     addr;
    ADDR_LEN             addr_len;
    KOS_LOCAL            this_;
    char                *address_cstr  = KOS_NULL;
    KOS_SOCKET_HOLDER   *socket_holder = KOS_NULL;
    int                  saved_errno;
    int                  error;
    uint16_t             port          = 0;

    KOS_init_local_with(ctx, &this_, this_obj);

    KOS_mempool_init_small(&alloc, 512U);

    TRY(KOS_extract_native_from_array(ctx, args_obj, "argument", connect_args, &alloc, &address_cstr, &port));

    TRY(acquire_socket_object(ctx, this_.o, &socket_holder));

    TRY(get_address(ctx, socket_holder, address_cstr, port, &addr, &addr_len));

    KOS_suspend_context(ctx);

    reset_last_error();

    error = connect(get_socket(socket_holder), &addr.addr, addr_len);

    saved_errno = get_error();

    KOS_resume_context(ctx);

    if (error) {
        KOS_raise_errno_value(ctx, "connect", saved_errno);
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }

cleanup:
    release_socket(socket_holder);

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
 *     socket.prototype.read(size = 4096 [, buffer])
 *
 * This is the same function as `socket.prototype.recv()`.
 */
/* @item net socket.prototype.recv()
 *
 *     socket.prototype.recv(size = 4096 [, buffer])
 *
 * Receives a variable number of bytes from a connected socket object.
 *
 * Receives as many bytes as it can, up to the specified `size`.
 *
 * `size` is the maximum bytes to receive.  `size` defaults to 4096.  Fewer
 * bytes can be received if no more bytes are available.
 *
 * If `buffer` is specified, bytes are appended to it and that buffer is
 * returned instead of creating a new buffer.
 *
 * Returns a buffer containing the bytes read.
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
    KOS_SOCKET_HOLDER *socket_holder = KOS_NULL;
    uint8_t           *data;
    KOS_OBJ_ID         arg;
    uint32_t           offset;
    int                error         = KOS_SUCCESS;
    int                saved_errno   = 0;

    assert(KOS_get_array_size(args_obj) >= 2);

    KOS_init_local(     ctx, &buf);
    KOS_init_local_with(ctx, &args, args_obj);

    TRY(acquire_socket_object(ctx, this_obj, &socket_holder));

    arg = KOS_array_read(ctx, args.o, 0);
    TRY_OBJID(arg);

    TRY(KOS_get_integer(ctx, arg, &to_read));

    if (to_read < 1)
        to_read = 1;

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

    num_read = recv(get_socket(socket_holder), (char *)(data + offset), (DATA_LEN)to_read, 0);

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
 *     socket.prototype.recvfrom()
 */
static KOS_OBJ_ID kos_recvfrom(KOS_CONTEXT ctx,
                               KOS_OBJ_ID  this_obj,
                               KOS_OBJ_ID  args_obj)
{
    /* TODO */
    return KOS_VOID;
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

        if (timeout.type == KOS_INTEGER_VALUE)
            tv_usec = (uint64_t)timeout.u.i * 1000000U;
        else
            tv_usec = (uint64_t)floor(timeout.u.d * 1000000.0);

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

/* @item net socket.prototype.write()
 *
 *     socket.prototype.write(values...)
 *
 * This is the same function as `socket.prototype.send()`.
 */
/* @item net socket.prototype.send()
 *
 *     socket.prototype.send(values...)
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
static KOS_OBJ_ID kos_send(KOS_CONTEXT ctx,
                           KOS_OBJ_ID  this_obj,
                           KOS_OBJ_ID  args_obj)
{
    KOS_VECTOR         cstr;
    KOS_LOCAL          print_args;
    KOS_LOCAL          arg;
    KOS_LOCAL          args;
    KOS_LOCAL          this_;
    KOS_SOCKET_HOLDER *socket_holder = KOS_NULL;
    const uint32_t     num_args      = KOS_get_array_size(args_obj);
    uint32_t           i_arg;
    int                error;

    KOS_vector_init(&cstr);

    KOS_init_locals(ctx, &print_args, &arg, &args, &this_, kos_end_locals);

    args.o  = args_obj;
    this_.o = this_obj;

    TRY(acquire_socket_object(ctx, this_.o, &socket_holder));

    for (i_arg = 0; i_arg < num_args; i_arg++) {

        int64_t num_writ    = 0;
        int     saved_errno = 0;

        arg.o = KOS_array_read(ctx, args.o, i_arg);
        TRY_OBJID(arg.o);

        if (GET_OBJ_TYPE(arg.o) == OBJ_BUFFER) {

            const size_t to_write = (size_t)KOS_get_buffer_size(arg.o);

            if (to_write > 0) {

                const uint8_t *data = KOS_buffer_data_const(arg.o);

                if (kos_is_heap_object(KOS_atomic_read_relaxed_obj(OBJPTR(BUFFER, arg.o)->data))) {

                    if (KOS_vector_resize(&cstr, to_write)) {
                        KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);
                        RAISE_ERROR(KOS_ERROR_EXCEPTION);
                    }

                    memcpy(cstr.buffer, data, to_write);
                    data = (uint8_t *)cstr.buffer;
                }
                else {
                    assert(kos_is_tracked_object(KOS_atomic_read_relaxed_obj(OBJPTR(BUFFER, arg.o)->data)));
                }

                KOS_suspend_context(ctx);

                reset_last_error();

                num_writ = send(get_socket(socket_holder), (const char *)data, (DATA_LEN)to_write, 0);

                if (num_writ < 0)
                    saved_errno = get_error();

                KOS_resume_context(ctx);
            }
        }
        else if (GET_OBJ_TYPE(arg.o) == OBJ_STRING) {

            if (IS_BAD_PTR(print_args.o)) {
                print_args.o = KOS_new_array(ctx, 1);
                TRY_OBJID(print_args.o);
            }

            TRY(KOS_array_write(ctx, print_args.o, 0, arg.o));

            TRY(KOS_print_to_cstr_vec(ctx, print_args.o, KOS_DONT_QUOTE, &cstr, " ", 1));

            if (cstr.size) {
                KOS_suspend_context(ctx);

                reset_last_error();

                num_writ = send(get_socket(socket_holder), cstr.buffer, (DATA_LEN)(cstr.size - 1), 0);

                if (num_writ < 0)
                    saved_errno = get_error();

                KOS_resume_context(ctx);
            }

            cstr.size = 0;
        }
        else
            RAISE_EXCEPTION_STR(str_err_not_buffer_or_str);

        if (saved_errno) {
            KOS_raise_errno_value(ctx, "send", saved_errno);
            RAISE_ERROR(KOS_ERROR_EXCEPTION);
        }
    }

cleanup:
    release_socket(socket_holder);

    KOS_vector_destroy(&cstr);

    this_.o = KOS_destroy_top_locals(ctx, &print_args, &this_);

    return error ? KOS_BADPTR : this_.o;
}

/* @item net socket.prototype.sendto()
 *
 *     socket.prototype.sendto()
 */
static KOS_OBJ_ID kos_sendto(KOS_CONTEXT ctx,
                             KOS_OBJ_ID  this_obj,
                             KOS_OBJ_ID  args_obj)
{
    /* TODO */
    return KOS_VOID;
}

KOS_DECLARE_STATIC_CONST_STRING(str_option, "option");

static const KOS_CONVERT getsockopt_args[2] = {
    KOS_DEFINE_MANDATORY_ARG(str_option),
    KOS_DEFINE_TAIL_ARG()
};

/* @item net socket.prototype.getsockopt()
 *
 *     socket.prototype.getsockopt()
 */
static KOS_OBJ_ID kos_getsockopt(KOS_CONTEXT ctx,
                                 KOS_OBJ_ID  this_obj,
                                 KOS_OBJ_ID  args_obj)
{
    /* TODO */
    return KOS_VOID;
}

#ifdef _WIN32
typedef BOOL SOCK_OPT_BOOL;
#else
typedef int SOCK_OPT_BOOL;
#endif

static int setsockopt_bool(KOS_CONTEXT        ctx,
                           KOS_SOCKET_HOLDER *socket_holder,
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
                       SOL_SOCKET,
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
                       SOL_SOCKET,
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
                       SOL_SOCKET,
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

static const KOS_CONVERT setsockopt_args[3] = {
    KOS_DEFINE_MANDATORY_ARG(str_option),
    KOS_DEFINE_MANDATORY_ARG(str_value),
    KOS_DEFINE_TAIL_ARG()
};

/* @item net socket.prototype.setsockopt()
 *
 *     socket.prototype.setsockopt()
 */
static KOS_OBJ_ID kos_setsockopt(KOS_CONTEXT ctx,
                                 KOS_OBJ_ID  this_obj,
                                 KOS_OBJ_ID  args_obj)
{
    KOS_LOCAL          args;
    KOS_LOCAL          value;
    KOS_LOCAL          this_;
    KOS_SOCKET_HOLDER *socket_holder;
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
        KOS_raise_printf(ctx, "option argument is %s but expected integer",
                         KOS_get_type_name(GET_OBJ_TYPE(value.o)));
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }

    TRY(KOS_get_integer(ctx, value.o, &option));

    value.o = KOS_array_read(ctx, args.o, 1);
    TRY_OBJID(value.o);

    switch (option) {
        case SO_BROADCAST:
            TRY(setsockopt_bool(ctx, socket_holder, (int)option, value.o));
            break;

        case SO_DEBUG:
            TRY(setsockopt_bool(ctx, socket_holder, (int)option, value.o));
            break;

        case SO_DONTROUTE:
            TRY(setsockopt_bool(ctx, socket_holder, (int)option, value.o));
            break;

        case SO_KEEPALIVE:
            TRY(setsockopt_bool(ctx, socket_holder, (int)option, value.o));
            break;

        /*
        case SO_LINGER:
        */

        case SO_OOBINLINE:
            TRY(setsockopt_bool(ctx, socket_holder, (int)option, value.o));
            break;

        case SO_RCVBUF:
            TRY(setsockopt_int(ctx, socket_holder, (int)option, value.o));
            break;

        case SO_RCVTIMEO:
            TRY(setsockopt_time(ctx, socket_holder, (int)option, value.o));
            break;

        case SO_REUSEADDR:
            TRY(setsockopt_bool(ctx, socket_holder, (int)option, value.o));
            break;

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
        case SO_REUSEPORT:
            TRY(setsockopt_bool(ctx, socket_holder, SO_REUSEPORT, value.o));
            break;
#endif

        case SO_SNDBUF:
            TRY(setsockopt_int(ctx, socket_holder, (int)option, value.o));
            break;

        case SO_SNDTIMEO:
            TRY(setsockopt_time(ctx, socket_holder, (int)option, value.o));
            break;

        default:
            KOS_raise_printf(ctx, "unknown option %" PRId64, option);
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

    const KOS_CONVERT recv_args[3] = {
        KOS_DEFINE_OPTIONAL_ARG(str_size,   TO_SMALL_INT(4096)),
        KOS_DEFINE_OPTIONAL_ARG(str_buffer, KOS_VOID          ),
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

    TRY_ADD_CONSTRUCTOR(    ctx, module.o,                 "socket",     kos_socket,     socket_args, &socket_proto.o);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, socket_proto.o, "accept",     kos_accept,     KOS_NULL);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, socket_proto.o, "bind",       kos_bind,       bind_args);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, socket_proto.o, "close",      kos_close,      KOS_NULL);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, socket_proto.o, "connect",    kos_connect,    connect_args);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, socket_proto.o, "getsockopt", kos_getsockopt, getsockopt_args);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, socket_proto.o, "listen",     kos_listen,     listen_args);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, socket_proto.o, "read",       kos_recv,       recv_args);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, socket_proto.o, "recv",       kos_recv,       recv_args);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, socket_proto.o, "recvfrom",   kos_recvfrom,   KOS_NULL);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, socket_proto.o, "release",    kos_close,      KOS_NULL);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, socket_proto.o, "wait",       kos_wait,       wait_args);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, socket_proto.o, "send",       kos_send,       KOS_NULL);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, socket_proto.o, "sendto",     kos_sendto,     KOS_NULL);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, socket_proto.o, "setsockopt", kos_setsockopt, setsockopt_args);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, socket_proto.o, "shutdown",   kos_shutdown,   shutdown_args);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, socket_proto.o, "write",      kos_send,       KOS_NULL);

    TRY_ADD_MEMBER_PROPERTY(ctx, module.o, socket_proto.o, "blocking",   get_blocking,   KOS_NULL);

#ifndef _WIN32
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "AF_LOCAL",     AF_LOCAL);
#endif
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "AF_INET",      AF_INET);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "AF_INET6",     AF_INET6);

    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "SOCK_STREAM",  SOCK_STREAM);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "SOCK_DGRAM",   SOCK_DGRAM);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "SOCK_RAW",     SOCK_RAW);

    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "SHUT_RD",      SHUT_RD);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "SHUT_RDWR",    SHUT_RDWR);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "SHUT_WR",      SHUT_WR);

    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "SO_BROADCAST", SO_BROADCAST);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "SO_DEBUG",     SO_DEBUG);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "SO_DONTROUTE", SO_DONTROUTE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "SO_KEEPALIVE", SO_KEEPALIVE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "SO_LINGER",    SO_LINGER);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "SO_OOBINLINE", SO_OOBINLINE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "SO_RCVBUF",    SO_RCVBUF);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "SO_RCVTIMEO",  SO_RCVTIMEO);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "SO_REUSEADDR", SO_REUSEADDR);
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "SO_REUSEPORT", SO_REUSEPORT);
#endif
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "SO_SNDBUF",    SO_SNDBUF);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "SO_SNDTIMEO",  SO_SNDTIMEO);

cleanup:
    KOS_destroy_top_locals(ctx, &socket_proto, &module);

    return error;
}
