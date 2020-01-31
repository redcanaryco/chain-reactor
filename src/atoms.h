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

#pragma once

typedef char byte_t;

#define EXEC_METHOD_PATH 1
#define EXEC_METHOD_DESCRIPTOR 2

#define FORK_METHOD_X86 1
#define FORK_METHOD_LIBC 2

#define SOCKET_METHOD_SYSCALL 1
#define SOCKET_METHOD_SOCKETCALL 2

#define SOCKET_TYPE_TCP  (1 << 0)
#define SOCKET_TYPE_UDP  (1 << 1)
#define SOCKET_TYPE_IPV4 (1 << 2)
#define SOCKET_TYPE_IPV6 (1 << 3)

#pragma pack(push, 1)
typedef struct {
    int method;
    byte_t argv[];
} exec_t, *pexec_t;

typedef struct {
    int method;
    byte_t argv[];
} fork_and_rename_t, *pfork_and_rename_t;

typedef struct {
    int method;
    int socket_type;
    unsigned short port;
    char address[];
} connect_t, *pconnect_t, listen_t, *plisten_t;

typedef struct {
    int unused;
    byte_t argv[];
} rm_rf_t, *prm_rf_t, copy_t, *pcopy_t;

typedef struct {
    unsigned int cb;
    char type[64];
} quark_t, *pquark_t ;

typedef struct {
    unsigned int cb;
    char name[64];
    unsigned int num_quarks;
    quark_t quarks[];
} atom_t, *patom_t;
#pragma pack(pop)

int split_atom(patom_t atom, int in_fork_and_rename);
