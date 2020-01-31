/*

The MIT License

Copyright (c) 2020 Red Canary, Inc.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

*/

#define _GNU_SOURCE 1

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/syscall.h>
#include <sys/mman.h>

#include "atoms.h"
#include "util.h"

#define SYS_SOCKET          1
#define SYS_BIND            2
#define SYS_CONNECT         3
#define SYS_LISTEN          4
#define SYS_ACCEPT          5
#define SYS_GETSOCKNAME     6
#define SYS_GETPEERNAME     7
#define SYS_SOCKETPAIR      8
#define SYS_SEND            9
#define SYS_RECV            10
#define SYS_SENDTO          11
#define SYS_RECVFROM        12
#define SYS_SHUTDOWN        13
#define SYS_SETSOCKOPT      14
#define SYS_GETSOCKOPT      15
#define SYS_SENDMSG         16
#define SYS_RECVMSG         17
#define SYS_ACCEPT4         18
#define SYS_RECVMMSG        19
#define SYS_SENDMMSG        20

#define SEND_RECV_CB 512

// We do this when writing a 32bit mapped 64bit address to a four byte memory
// address. The compiler doesn't know this, but it's fine.
#pragma GCC diagnostic ignored "-Wpointer-to-int-cast"
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"

// It is possible to configure a Linux kernel that does not support syscall
// emulation, however it is highly unlikely to occur in main distros.
static int x86_int0x80(int syscall, void* arg1, void* arg2, void* arg3, void* arg4, void* arg5)
{
    int ret_value;
    asm volatile("int $0x80"
            : "=a"(ret_value)
            : "a"(syscall), "b"(arg1), "c"(arg2), "d"(arg3), "S"(arg4), "D"(arg5)
            : "memory");
    return ret_value;
}

// Chances are, none of these are to be more than a single page, but still trying
// to be "correct"
#define ALLOC_SOCKETCALL_ARGS(SIZE) \
size_t cb_args = (SIZE); \
void* args = mmap(NULL, \
    cb_args, \
    PROT_READ | PROT_WRITE, \
    MAP_ANONYMOUS | MAP_PRIVATE | MAP_32BIT, -1, 0); \
if (MAP_FAILED == args) { \
    ERROR("mmap failed: %d, %s\n", errno, strerror(errno)); \
    return -1; \
}

static inline int socketcall(int call, void* args, size_t cb_args)
{
    int prev_errno = errno;
    int ret = x86_int0x80(102, (void*)call, (void*)args, 0, 0, 0);
    if (0 > ret) {
        prev_errno = abs(ret);
        ret = -1;
    }
    munmap(args, cb_args);
    errno = prev_errno;
    return ret;
}

int socketcall_socket(int domain, int type, int protocol)
{
    ALLOC_SOCKETCALL_ARGS(sizeof(int)*3);

    ((int*)args)[0] = domain;
    ((int*)args)[1] = type;
    ((int*)args)[2] = protocol;

    return socketcall(SYS_SOCKET, args, cb_args);
}

int socketcall_send(int sockfd, const void *buf, size_t len, int flags)
{
    ALLOC_SOCKETCALL_ARGS(sizeof(int)*4 + len);

    // In order to use the kernel's 32bit entry point, all addresses and
    // pointers need to be marshalled to the lower 2GB of virtual memory.
    // The ALLOC_SOCKETCALL_ARGS takes care of getting us the correct
    // location. Since `buf` is a pointer to an array, we need to copy the
    // array to this blob, and have our pointer point to it.
    ((int*)args)[0] = sockfd;
    ((int*)args)[1] = (int)(&((int*)args)[4]);
    ((int*)args)[2] = len;
    ((int*)args)[3] = flags;
    memcpy((void*)(&((int*)args)[4]), buf, len);

    return socketcall(SYS_SEND, args, cb_args);
}

int socketcall_sendto(int sockfd, const void *buf, size_t len, int flags,
    const struct sockaddr *dest_addr, socklen_t addrlen)
{
    ALLOC_SOCKETCALL_ARGS(sizeof(int)*6 + len + addrlen);

    // In order to use the kernel's 32bit entry point, all addresses and
    // pointers need to be marshalled to the lower 2GB of virtual memory.
    // The ALLOC_SOCKETCALL_ARGS takes care of getting us the correct
    // location. Since `buf` is a pointer to an array, we need to copy the
    // array to this blob, and have our pointer point to it.
    ((int*)args)[0] = sockfd;
    ((int*)args)[1] = (int)((char*)args + sizeof(int)*6);
    ((int*)args)[2] = len;
    ((int*)args)[3] = flags;
    ((int*)args)[4] = (int)((char*)args + sizeof(int)*6 + len);
    ((int*)args)[5] = addrlen;
    memcpy(((char*)args + sizeof(int)*6), buf, len);
    memcpy(((char*)args + sizeof(int)*6 + len), dest_addr, addrlen);

    return socketcall(SYS_SENDTO, args, cb_args);
}

int socketcall_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    ALLOC_SOCKETCALL_ARGS(sizeof(int)*3 + addrlen);

    // In order to use the kernel's 32bit entry point, all addresses and
    // pointers need to be marshalled to the lower 2GB of virtual memory.
    // The ALLOC_SOCKETCALL_ARGS takes care of getting us the correct
    // location. Since `addr` is a pointer to a structure, we need to copy the
    // structure to this blob, and have our pointer point to it.
    ((int*)args)[0] = sockfd;
    ((int*)args)[1] = (int)(&((int*)args)[3]);
    ((int*)args)[2] = addrlen;
    memcpy((void*)(&((int*)args)[3]), addr, addrlen);

    return socketcall(SYS_BIND, args, cb_args);
}

int socketcall_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    ALLOC_SOCKETCALL_ARGS(sizeof(int)*3 + addrlen);

    // In order to use the kernel's 32bit entry point, all addresses and
    // pointers need to be marshalled to the lower 2GB of virtual memory.
    // The ALLOC_SOCKETCALL_ARGS takes care of getting us the correct
    // location. Since `addr` is a pointer to a structure, we need to copy the
    // structure to this blob, and have our pointer point to it.
    ((int*)args)[0] = sockfd;
    ((int*)args)[1] = (int)(&((int*)args)[3]);
    ((int*)args)[2] = addrlen;
    memcpy((void*)(&((int*)args)[3]), addr, addrlen);

    return socketcall(SYS_CONNECT, args, cb_args);
}

int socketcall_listen(int sockfd, int backlog)
{
    ALLOC_SOCKETCALL_ARGS(sizeof(int)*2);

    ((int*)args)[0] = sockfd;
    ((int*)args)[1] = backlog;

    return socketcall(SYS_LISTEN, args, cb_args);
}

int socketcall_accept4(int sockfd)
{
    ALLOC_SOCKETCALL_ARGS(sizeof(int)*4);

    ((int*)args)[0] = sockfd;
    ((int*)args)[1] = 0;
    ((int*)args)[2] = 0;
    ((int*)args)[3] = 0;

    return socketcall(SYS_ACCEPT4, args, cb_args);
}

int socketcall_recv(int sockfd) {
    ALLOC_SOCKETCALL_ARGS(sizeof(int)*4 + SEND_RECV_CB);

    // In order to use the kernel's 32bit entry point, all addresses and
    // pointers need to be marshalled to the lower 2GB of virtual memory.
    // The ALLOC_SOCKETCALL_ARGS takes care of getting us the correct
    // location. Since `addr` is a pointer to a structure, we need to copy the
    // structure to this blob, and have our pointer point to it.
    ((int*)args)[0] = sockfd;
    ((int*)args)[1] = (int)(&((int*)args)[4]);
    ((int*)args)[2] = SEND_RECV_CB;
    ((int*)args)[3] = 0;

    return socketcall(SYS_RECV, args, cb_args);
}

static inline void socket_type(int socket_type, int* family, int* type)
{
    if (SOCKET_TYPE_TCP & socket_type) {
        *type = SOCK_STREAM;
    } else {
        *type = SOCK_DGRAM;
    }

    if (SOCKET_TYPE_IPV4 & socket_type) {
        *family = AF_INET;
    } else {
        *family = AF_INET6;
    }
}

static inline int create_socket(int __socket_type, int method)
{
    int domain = 0;
    int type = 0;

    socket_type(__socket_type, &domain, &type);

    if (SOCKET_METHOD_SOCKETCALL == method) {
        return socketcall_socket(domain, type, 0);
    }

    return socket(domain, type, 0);
}

static inline int connect_socket(int socket_fd,
    int __socket_type,
    int method,
    struct addrinfo* addr,
    int port)
{
    if (__socket_type & SOCKET_TYPE_IPV4) {
        ((struct sockaddr_in*)addr->ai_addr)->sin_port = htons(port);
    } else {
        ((struct sockaddr_in6*)addr->ai_addr)->sin6_port = htons(port);
    }

    if (__socket_type & SOCKET_TYPE_TCP) {
        if (SOCKET_METHOD_SOCKETCALL == method) {
            return socketcall_connect(socket_fd, addr->ai_addr, addr->ai_addrlen);
        }

        return connect(socket_fd, addr->ai_addr, addr->ai_addrlen);
    }

    return 0;
}

static inline int send_data(int socket_fd, int __socket_type, int method, struct addrinfo* addr)
{
    char message[SEND_RECV_CB] = {0};
    urand(message, sizeof(message));

    if (__socket_type & SOCKET_TYPE_UDP) {
        if (SOCKET_METHOD_SOCKETCALL == method) {
            return socketcall_sendto(socket_fd, message, sizeof(message), 0, addr->ai_addr, addr->ai_addrlen);
        }

        return sendto(socket_fd, message, sizeof(message), 0, addr->ai_addr, addr->ai_addrlen);
    }

    if (SOCKET_METHOD_SOCKETCALL == method) {
        return socketcall_send(socket_fd, message, sizeof(message), 0);
    }

    return send(socket_fd, message, sizeof(message), 0);
}

static int bind_socket(int socket_fd, int method, const struct sockaddr *addr, socklen_t addrlen)
{
    if (SOCKET_METHOD_SOCKETCALL == method) {
        return socketcall_bind(socket_fd, addr, addrlen);
    }

    return bind(socket_fd, addr, addrlen);
}

static int listen_socket(int socket_fd, int __socket_type, int method)
{
    // UDP sockets don't listen
    if (__socket_type & SOCKET_TYPE_UDP) {
        return 0;
    }

    if (SOCKET_METHOD_SOCKETCALL == method) {
        return socketcall_listen(socket_fd, 1);
    }

    return listen(socket_fd, 1);
}

static int accept_socket(int socket_fd, int __socket_type, int method)
{
    int client_fd = -1;
    if (__socket_type & SOCKET_TYPE_TCP) {
        if (SOCKET_METHOD_SOCKETCALL == method) {
            client_fd = socketcall_accept4(socket_fd);

            if (-1 == client_fd) {
                ERROR("\t\tsocketcall_accept4 failed: %d, %s\n", errno, strerror(errno));
                goto Exit;
            }
        } else {
            client_fd = accept4(socket_fd, NULL, NULL, 0);

            if (-1 == client_fd) {
                ERROR("\t\taccept4 failed: %d, %s\n", errno, strerror(errno));
                goto Exit;
            }
        }
    } else {
        client_fd = socket_fd;
    }

    if (SOCKET_METHOD_SYSCALL == method) {
        char buf[SEND_RECV_CB] = {0};
        if (-1 == recv(client_fd, buf, sizeof(buf), 0)) {
            ERROR("\t\trecv failed: %d, %s\n", errno, strerror(errno));
            goto Exit;
        }
    } else {
        if (-1 == socketcall_recv(client_fd)) {
            ERROR("\t\tsocketcall_recv failed: %d, %s\n", errno, strerror(errno));
            goto Exit;
        }
    }

    LOGG("\t\tconnection received\n");

Exit:

    if (__socket_type & SOCKET_TYPE_TCP && -1 != client_fd) {
        shutdown(client_fd, SHUT_RDWR);
        close(client_fd);
    }

    return 0;
}

int quark_connect(pconnect_t args, int silent)
{
    int err = 0;
    int type = 0;
    int domain = 0;
    int socket_fd = -1;
    struct addrinfo* result = NULL;
    struct addrinfo hints = {0};

    if (!silent) {
        LOGY("\tquark: connect(%s %s%s %s:%u)\n",
            args->method == SOCKET_METHOD_SYSCALL ? "syscall" : "socketcall",
            args->socket_type & SOCKET_TYPE_TCP ? "tcp" : "udp",
            args->socket_type & SOCKET_TYPE_IPV4 ? "4" : "6",
            args->address, args->port);
    }

    hints.ai_flags = (AI_V4MAPPED | AI_ADDRCONFIG);
    socket_type(args->socket_type, &hints.ai_family, &hints.ai_socktype);

    if ((err = getaddrinfo(args->address, NULL, &hints, &result)) || !result) {
        ERROR("\t\tgetaddrinfo failed: %d, %s\n", err, gai_strerror(err));
        goto Exit;
    }

    socket_fd = create_socket(args->socket_type, args->method);
    if (-1 == socket_fd) {
        ERROR("\t\tcreate_socket failed: %d, %s\n", errno, strerror(errno));
        goto Exit;
    }

    err = connect_socket(socket_fd, args->socket_type, args->method, result, args->port);
    if (-1 == err) {
        ERROR("\t\tconnect_socket failed: %d, %s\n", errno, strerror(errno));
        goto Exit;
    }

    err = send_data(socket_fd, args->socket_type, args->method, result);
    if (-1 == err) {
        ERROR("\t\tsend_data failed: %d, %s\n", errno, strerror(errno));
        goto Exit;
    }

Exit:

    if (result) {
        freeaddrinfo(result);
    }

    if (-1 != socket_fd) {
        shutdown(socket_fd, SHUT_RDWR);
        close(socket_fd);
    }

    return err;
}

int quark_listen(plisten_t args)
{
    int err = 0;
    int socket_fd = -1;
    char* target_address = NULL;
    socklen_t address_length = 0;

    union {
        struct sockaddr_in ipv4;
        struct sockaddr_in6 ipv6;
    } listening_address;

    memset(&listening_address, 0, sizeof(listening_address));

    LOGY("\tquark: listen(%s %s%s %s:%u)\n",
        args->method == SOCKET_METHOD_SYSCALL ? "syscall" : "socketcall",
        args->socket_type & SOCKET_TYPE_TCP ? "tcp" : "udp",
        args->socket_type & SOCKET_TYPE_IPV4 ? "4" : "6",
        args->address, args->port);

    if (args->socket_type & SOCKET_TYPE_IPV4) {
        address_length = sizeof(listening_address.ipv4);
        listening_address.ipv4.sin_port = htons(args->port);
        listening_address.ipv4.sin_family = AF_INET;

        if (!inet_aton(args->address, &listening_address.ipv4.sin_addr)) {
            ERROR("\t\tinet_aton failed: %d, %s\n", errno, strerror(errno));
            goto Exit;
        }
    } else {
        address_length = sizeof(listening_address.ipv6);
        listening_address.ipv6.sin6_port = htons(args->port);
        listening_address.ipv6.sin6_family = AF_INET6;

        if (1 > inet_pton(AF_INET6, args->address, &listening_address.ipv6.sin6_addr)) {
            ERROR("\t\tinet_pton failed: %d, %s\n", errno, strerror(errno));
            goto Exit;
        }
    }

    socket_fd = create_socket(args->socket_type, args->method);
    if (-1 == socket_fd) {
        ERROR("\t\tcreate_socket failed: %d, %s\n", errno, strerror(errno));
        goto Exit;
    }

    err = bind_socket(socket_fd, args->method,
        (const struct sockaddr *)&listening_address, address_length);
    if (-1 == err) {
        ERROR("\t\tbind_socket failed: %d, %s\n", errno, strerror(errno));
        goto Exit;
    }

    err = listen_socket(socket_fd, args->socket_type, args->method);
    if (-1 == err) {
        ERROR("\t\tlisten_socket failed: %d, %s\n", errno, strerror(errno));
        goto Exit;
    }

    // If its the magic value for all interfaces, just target the
    // loopback device.
    if (!strcmp(args->address, "0.0.0.0") || !strcmp(args->address, "::/0")) {
        if (args->socket_type & SOCKET_TYPE_IPV6) {
            target_address = strdup("::1/128");
        } else {
            target_address = strdup("127.0.0.1");
        }
    } else {
        target_address = strdup(args->address);
    }

    pid_t sender_pid;
    switch ((sender_pid = fork()))
    {
        case 0:
        {
            pconnect_t connect_args = (pconnect_t)calloc(1, sizeof(connect_t) + strlen(target_address) + 1);
            if (NULL == connect_args) {
                ERROR("\t\tcalloc failed: %d, %s\n", errno, strerror(errno));
                exit(0);
            }

            connect_args->method = args->method;
            connect_args->socket_type = args->socket_type;
            connect_args->port = args->port;
            memcpy(connect_args->address, target_address, strlen(target_address) + 1);
            // Allow the parent to start recv/accept
            sleep(1);
            quark_connect(connect_args, 1);
            exit(0);
            break;
        }
        default:
            err = accept_socket(socket_fd, args->socket_type, args->method);

            if (-1 == err) {
                ERROR("\t\taccept_socket failed: %d, %s\n", errno, strerror(errno));
                goto Exit;
            }
            int status = 0;
            waitpid(sender_pid, &status, 0);
            break;
        case -1:
            ERROR("\t\tfork failed: %d, %s\n", errno, strerror(errno));
            goto Exit;
            break;
    }

Exit:

    if (-1 != socket_fd) {
        shutdown(socket_fd, SHUT_RDWR);
        close(socket_fd);
    }

    if (target_address) {
        free(target_address);
    }

    return err;
}
