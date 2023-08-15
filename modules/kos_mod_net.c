/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2023 Chris Dragan
 */

#include "../inc/kos_array.h"
#include "../inc/kos_constants.h"
#include "../inc/kos_error.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_malloc.h"
#include "../inc/kos_module.h"
#include "../inc/kos_object.h"
#include "../inc/kos_utils.h"
#include "../core/kos_try.h"

#include <string.h>

#ifdef _WIN32
#   include <winsock2.h>
#else
#   include <arpa/inet.h>
#   include <errno.h>
#   include <netinet/in.h>
#   include <sys/socket.h>
#   include <sys/un.h>
#   include <unistd.h>
#endif

#ifdef _WIN32
#define reset_last_error() ((void)0)

static int get_error()
{
    return WSAGetLastError();
}
#else
static void reset_last_error()
{
    errno = 0;
}

static int get_error()
{
    return errno;
}
#endif

KOS_DECLARE_STATIC_CONST_STRING(str_err_socket_not_open, "socket not open or not a socket object");

typedef struct KOS_SOCKET_HOLDER_S {
    KOS_ATOMIC(uint32_t) socket_fd;
    KOS_ATOMIC(uint32_t) ref_count;
    int                  family;
} KOS_SOCKET_HOLDER;

static int acquire_socket(KOS_SOCKET_HOLDER *socket_holder)
{
    uint32_t ref_count;

    assert(socket_holder);

    do {
        ref_count = KOS_atomic_read_relaxed_u32(socket_holder->ref_count);
        if ((int32_t)ref_count <= 0)
            return (int)ref_count;
    } while ( ! KOS_atomic_cas_weak_u32(socket_holder->ref_count, ref_count, ref_count+ 1));

    return (int)ref_count;
}

static void release_socket(KOS_SOCKET_HOLDER *socket_holder)
{
    if (socket_holder) {
        const int32_t ref_count = KOS_atomic_add_i32(*(KOS_ATOMIC(int32_t)*)&socket_holder->ref_count, -1);

        assert(ref_count >= 1);

        if (ref_count == 1) {
            const int32_t socket_fd = (int32_t)KOS_atomic_swap_u32(socket_holder->socket_fd, (uint32_t)-1);

            if (socket_fd >= 0) {
                /* TODO shutdown */
                close(socket_fd);
            }

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

static int set_socket_object(KOS_CONTEXT ctx,
                             KOS_OBJ_ID  socket_obj,
                             int         socket_fd,
                             int         family)
{
    KOS_SOCKET_HOLDER *const socket_holder = (KOS_SOCKET_HOLDER *)KOS_malloc(sizeof(KOS_SOCKET_HOLDER));

    if ( ! socket_holder) {
        KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);
        return KOS_ERROR_EXCEPTION;
    }

    socket_holder->socket_fd = socket_fd;
    socket_holder->ref_count = 1;
    socket_holder->family    = family;

    KOS_object_set_private_ptr(socket_obj, socket_holder);

    return KOS_SUCCESS;
}

static int get_socket(KOS_SOCKET_HOLDER *socket_holder)
{
    return socket_holder ? (int)KOS_atomic_read_relaxed_u32(socket_holder->socket_fd) : -1;
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

    if ( ! get_socket(*socket_holder)) {
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
    struct sockaddr_un  local;
} KOS_GENERIC_ADDR;

#ifdef _WIN32
typedef int ADDR_LEN;
#else
typedef socklen_t ADDR_LEN;
#endif

static int get_address(KOS_CONTEXT       ctx,
                       KOS_OBJ_ID        addr_obj,
                       KOS_GENERIC_ADDR *addr,
                       ADDR_LEN         *addr_len)
{
    /* TODO */
    memset(&addr->inet, 0, sizeof(addr->inet));
    addr->inet.sin_family = AF_INET;
    addr->inet.sin_port   = 1024;
    addr->inet.sin_addr.s_addr = htonl(0x7F000001U);
    return KOS_SUCCESS;
}

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
 */
static KOS_OBJ_ID kos_socket(KOS_CONTEXT ctx,
                             KOS_OBJ_ID  this_obj,
                             KOS_OBJ_ID  args_obj)
{
    KOS_LOCAL this_;
    KOS_LOCAL ret;
    int       socket_fd    = -1;
    int32_t   arg_domain   = 0;
    int32_t   arg_type     = 0;
    int32_t   arg_protocol = 0;
    int       saved_errno;
    int       error;

    assert(KOS_get_array_size(args_obj) >= 3);

    KOS_init_local(     ctx, &ret);
    KOS_init_local_with(ctx, &this_, this_obj);

    TRY(KOS_extract_native_from_array(ctx, args_obj, "argument", socket_args, KOS_NULL,
                                      &arg_domain, &arg_type, &arg_protocol));

    KOS_suspend_context(ctx);

    reset_last_error();

    socket_fd = socket(arg_domain, arg_type, arg_protocol);

    saved_errno = get_error();

    KOS_resume_context(ctx);

    if (socket_fd < 0) {
        KOS_raise_errno_value(ctx, "socket", saved_errno);
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }

    ret.o = KOS_new_object_with_private(ctx, this_.o, &socket_priv_class, socket_finalize);
    TRY_OBJID(ret.o);

    TRY(set_socket_object(ctx, ret.o, socket_fd, arg_domain));

cleanup:
    ret.o = KOS_destroy_top_locals(ctx, &this_, &ret);

    return error ? KOS_BADPTR : ret.o;
}

/* @item net socket.prototype.accept()
 *
 *     socket.prototype.accept()
 */
static KOS_OBJ_ID kos_accept(KOS_CONTEXT ctx,
                             KOS_OBJ_ID  this_obj,
                             KOS_OBJ_ID  args_obj)
{
    KOS_SOCKET_HOLDER *socket_holder = KOS_NULL;
    int                error;

    TRY(acquire_socket_object(ctx, this_obj, &socket_holder));

    KOS_suspend_context(ctx);

    /* TODO */

    KOS_resume_context(ctx);

cleanup:
    release_socket(socket_holder);

    return KOS_VOID;
}

KOS_DECLARE_STATIC_CONST_STRING(str_address, "address");

static const KOS_CONVERT bind_args[2] = {
    KOS_DEFINE_MANDATORY_ARG(str_address),
    KOS_DEFINE_TAIL_ARG()
};

/* @item net socket.prototype.bind()
 *
 *     socket.prototype.bind(address)
 *
 * Binds an address to a socket.  `address` specifies the address to bind.
 *
 * Returns the `this` socket object.
 *
 * On error throws an exception.
 */
static KOS_OBJ_ID kos_bind(KOS_CONTEXT ctx,
                           KOS_OBJ_ID  this_obj,
                           KOS_OBJ_ID  args_obj)
{
    KOS_GENERIC_ADDR   addr;
    ADDR_LEN           addr_len;
    KOS_LOCAL          this_;
    KOS_SOCKET_HOLDER *socket_holder = KOS_NULL;
    KOS_OBJ_ID         addr_obj;
    int                saved_errno;
    int                error;

    KOS_init_local_with(ctx, &this_, this_obj);

    addr_obj = KOS_array_read(ctx, args_obj, 0);
    assert( ! IS_BAD_PTR(addr_obj));

    TRY(get_address(ctx, addr_obj, &addr, &addr_len));

    TRY(acquire_socket_object(ctx, this_.o, &socket_holder));

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

    this_.o = KOS_destroy_top_local(ctx, &this_);

    return error ? KOS_NULL : this_.o;
}

/* @item net socket.prototype.close()
 *
 *     socket.prototype.close()
 */
static KOS_OBJ_ID kos_close(KOS_CONTEXT ctx,
                            KOS_OBJ_ID  this_obj,
                            KOS_OBJ_ID  args_obj)
{
    /* TODO */
    return KOS_VOID;
}

/* @item net socket.prototype.connect()
 *
 *     socket.prototype.connect()
 */
static KOS_OBJ_ID kos_connect(KOS_CONTEXT ctx,
                              KOS_OBJ_ID  this_obj,
                              KOS_OBJ_ID  args_obj)
{
    /* TODO */
    return KOS_VOID;
}

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

/* @item net socket.prototype.listen()
 *
 *     socket.prototype.listen()
 */
static KOS_OBJ_ID kos_listen(KOS_CONTEXT ctx,
                             KOS_OBJ_ID  this_obj,
                             KOS_OBJ_ID  args_obj)
{
    /* TODO */
    return KOS_VOID;
}

/* @item net socket.prototype.read()
 *
 *     socket.prototype.read()
 */
static KOS_OBJ_ID kos_read(KOS_CONTEXT ctx,
                           KOS_OBJ_ID  this_obj,
                           KOS_OBJ_ID  args_obj)
{
    /* TODO */
    return KOS_VOID;
}

/* @item net socket.prototype.recv()
 *
 *     socket.prototype.recv()
 */
static KOS_OBJ_ID kos_recv(KOS_CONTEXT ctx,
                           KOS_OBJ_ID  this_obj,
                           KOS_OBJ_ID  args_obj)
{
    /* TODO */
    return KOS_VOID;
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

/* @item net socket.prototype.select()
 *
 *     socket.prototype.select()
 */
static KOS_OBJ_ID kos_select(KOS_CONTEXT ctx,
                             KOS_OBJ_ID  this_obj,
                             KOS_OBJ_ID  args_obj)
{
    /* TODO */
    return KOS_VOID;
}

/* @item net socket.prototype.send()
 *
 *     socket.prototype.send()
 */
static KOS_OBJ_ID kos_send(KOS_CONTEXT ctx,
                           KOS_OBJ_ID  this_obj,
                           KOS_OBJ_ID  args_obj)
{
    /* TODO */
    return KOS_VOID;
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

/* @item net socket.prototype.setsockopt()
 *
 *     socket.prototype.setsockopt()
 */
static KOS_OBJ_ID kos_setsockopt(KOS_CONTEXT ctx,
                                 KOS_OBJ_ID  this_obj,
                                 KOS_OBJ_ID  args_obj)
{
    /* TODO */
    return KOS_VOID;
}

/* @item net socket.prototype.shutdown()
 *
 *     socket.prototype.shutdown()
 */
static KOS_OBJ_ID kos_shutdown(KOS_CONTEXT ctx,
                               KOS_OBJ_ID  this_obj,
                               KOS_OBJ_ID  args_obj)
{
    /* TODO */
    return KOS_VOID;
}

/* @item net socket.prototype.write()
 *
 *     socket.prototype.write()
 */
static KOS_OBJ_ID kos_write(KOS_CONTEXT ctx,
                            KOS_OBJ_ID  this_obj,
                            KOS_OBJ_ID  args_obj)
{
    /* TODO */
    return KOS_VOID;
}

KOS_INIT_MODULE(net, 0)(KOS_CONTEXT ctx, KOS_OBJ_ID module_obj)
{
    int       error = KOS_SUCCESS;
    KOS_LOCAL module;
    KOS_LOCAL socket_proto;

    KOS_init_debug_output();

    KOS_init_local_with(ctx, &module, module_obj);
    KOS_init_local(     ctx, &socket_proto);

    TRY_ADD_CONSTRUCTOR(    ctx, module.o,                 "socket",     kos_socket,     socket_args, &socket_proto.o);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, socket_proto.o, "accept",     kos_accept,     KOS_NULL);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, socket_proto.o, "bind",       kos_bind,       bind_args);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, socket_proto.o, "close",      kos_close,      KOS_NULL);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, socket_proto.o, "connect",    kos_connect,    KOS_NULL);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, socket_proto.o, "getsockopt", kos_getsockopt, KOS_NULL);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, socket_proto.o, "listen",     kos_listen,     KOS_NULL);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, socket_proto.o, "read",       kos_read,       KOS_NULL);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, socket_proto.o, "recv",       kos_recv,       KOS_NULL);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, socket_proto.o, "recvfrom",   kos_recvfrom,   KOS_NULL);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, socket_proto.o, "release",    kos_close,      KOS_NULL);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, socket_proto.o, "select",     kos_select,     KOS_NULL);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, socket_proto.o, "send",       kos_send,       KOS_NULL);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, socket_proto.o, "sendto",     kos_sendto,     KOS_NULL);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, socket_proto.o, "setsockopt", kos_setsockopt, KOS_NULL);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, socket_proto.o, "shutdown",   kos_shutdown,   KOS_NULL);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, socket_proto.o, "write",      kos_write,      KOS_NULL);

    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "AF_LOCAL", AF_LOCAL);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "AF_INET",  AF_INET);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "AF_INET6", AF_INET6);

    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "SOCK_STREAM", SOCK_STREAM);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "SOCK_DGRAM",  SOCK_DGRAM);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "SOCK_RAW",    SOCK_RAW);

cleanup:
    KOS_destroy_top_locals(ctx, &socket_proto, &module);

    return error;
}
