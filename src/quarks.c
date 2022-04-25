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
#include <wordexp.h>
#include <limits.h>
#include <libgen.h>
#include <syscall.h>
#include <grp.h>
#include <pwd.h>

#include "atoms.h"
#include "util.h"
#include "settings.h"

int quark_connect(pconnect_t args);
int quark_listen(plisten_t args);

// Mussel thinks its cooler than us, it doesn't implement a real execveat
static int execveat(int dirfd, const char *pathname,
                    char *const argv[], char *const envp[],
                    int flags)
{
    int ret = syscall(322, dirfd, pathname, argv, envp, flags);
    if (0 > ret) {
        errno = abs(ret);
        return -1;
    }
    return ret;
}

int quark_exec(pexec_t args)
{
    int err = -1;
    pid_t pid = -1;
    size_t argc = 0;
    char** expanded_argv = NULL;
    wordexp_t expanded_arg = {0};

    char** argv = make_argv(args->argv);
    if (NULL == argv) {
        ERROR("\tmake_argv failed: %d, %s\n", errno, strerror(errno));
        exit(1);
    }

    char** ii = argv;
    char** arg = argv;
    LOGY("\tquark: %s(\"", METHOD_PATH == args->method ? "execve" : "execveat");
    while (*arg) {
        LOGY("%s ", *arg);
        arg++;
    }
    LOGY("\b\")\n");

    while (ii[0]) {
        argc++;
        ii++;
    }

    // +1 so the syscalls are happy, they expect a null terminated array
    expanded_argv = (char**)calloc(argc + 1, sizeof(*expanded_argv));

    int no_wordexp = check_settings_flag(FLAGS_NO_WORDEXP);
    for (int jj = 0; jj < argc; ++jj) {
        if (no_wordexp || wordexp(argv[jj], &expanded_arg, WRDE_REUSE)) {
            expanded_argv[jj] = strdup(argv[jj]);
        } else {
            expanded_argv[jj] = strdup(expanded_arg.we_wordv[0]);
        }
    }

    free_argv(argv);

    switch((pid=fork()))
    {
        case 0:
        {
            int fd = open("/dev/null", O_WRONLY | O_CLOEXEC, 0);
            int original_stdout = dup(1);

            // STDIN, STDOUT, STDERR
            for (int jj = 0; jj < 3; ++jj) {
                if (-1 == dup2(fd, jj)) {
                    ERROR("\t\tdup2(%d) failed: %d, %s\n", jj, errno, strerror(errno));
                    exit(1);
                }
            }

            if (METHOD_AT_DESCRIPTOR == args->method) {
                char resolved_path[PATH_MAX+1] = {0};

                memcpy(resolved_path, expanded_argv[0], strlen(expanded_argv[0]));

                int exec_fd = open(resolved_path, O_PATH);
                if (-1 == exec_fd) {
                    if (errno != ENOENT) {
                        ERROR("\t\topen(%s) failed: %d, %s\n",
                            resolved_path, errno, strerror(errno));
                        exit(1);
                    } else {
                        const char* ro_path = getenv("PATH");
                        if (NULL == ro_path) {
                            ERROR("\t\topen(%s) failed: %d, %s\n",
                                resolved_path, ENOENT, strerror(ENOENT));
                            exit(127);
                        }

                        // fexecve doesn't support path traversal, so we do it
                        // ourselves!
                        char* path = strdup(ro_path);
                        char* current_sep = path;
                        const char* current_path = NULL;
                        while ((current_path = strsep(&current_sep, ":"))) {
                            // Double :: in a shell path means the current directory.
                            if ('\0' == *current_path) {
                                current_path = ".";
                            }

                            snprintf(resolved_path, sizeof(resolved_path),
                                "%s/%s", current_path, expanded_argv[0]);

                            exec_fd = open(resolved_path, O_PATH);
                            if (-1 != exec_fd) {
                                execveat(exec_fd, "", expanded_argv, environ, AT_EMPTY_PATH);
                            }
                        }
                    }
                } else {
                    execveat(exec_fd, "", expanded_argv, environ, AT_EMPTY_PATH);
                }
            } else {
                execvp(expanded_argv[0], expanded_argv);
            }

            dup2(original_stdout, 2);
            ERROR("\t\t%s(%s) failed: %d, %s\n",
                METHOD_PATH == args->method ? "execvp" : "execveat",
                expanded_argv[0], errno, strerror(errno));
            if (errno == ENOENT) {
                exit(127);
            }
            exit(0);
            break;
        }
        default:
        {
            int status = 0;
            if (-1 == waitpid(pid, &status, 0)) {
                ERROR("\t\twaitpid(%d) failed: %d, %s\n", pid, errno, strerror(errno));
                goto Exit;
            }

            if (127 == WEXITSTATUS(status)) {
                ERROR("\t\t\"");
                for (int jj = 0; jj < argc; ++jj) {
                    ERROR("%s ", expanded_argv[jj]);
                }
                ERROR("\b\" one of the dependencies is missing from this system\n");
                err = -1;
                goto Exit;
            }

            break;
        }
        case -1:
            ERROR("\t\tfork failed: %d, %s\n", errno, strerror(errno));
            goto Exit;
            break;
    }

    err = 0;
Exit:

    if (expanded_argv) {
        for (int jj = 0; jj < argc; ++jj) {
            free(expanded_argv[jj]);
        }
        free(expanded_argv);
    }

    wordfree(&expanded_arg);
    return err;
}

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

static pid_t do_fork(int method)
{
    if (method == FORK_METHOD_LIBC) {
        return fork();
    }

    return x86_int0x80(2, 0, 0, 0, 0, 0);
}

void quark_fork_and_rename(pfork_and_rename_t args, int in_fork_and_rename)
{
    pid_t pid;
    char new_path[PATH_MAX] = {0};
    char** argv = make_argv(args->argv);

    if (NULL == argv) {
        ERROR("\tmake_argv failed: %d, %s\n", errno, strerror(errno));
        exit(1);
    }

    LOGY("\tquark: fork-and-rename(\"");
    char** arg = argv;
    while (*arg) {
        LOGY("%s ", *arg);
        arg++;
    }
    LOGY("\b\")\n");

    if (-1 == make_temp_dir(new_path)) {
        ERROR("\t\tmake_temp_dir failed: %d, %s\n", errno, strerror(errno));
        exit(1);
    }

    if ('/' != new_path[strlen(new_path)-1]) {
        strlcat(new_path, "/", sizeof(new_path));
    }

    strlcat(new_path, argv[0], sizeof(new_path));

    switch ((pid = do_fork(args->method)))
    {
        case -1:
        {
            ERROR("\t\tfork failed: %d, %s\n", errno, strerror(errno));
            rm_rf(new_path);
            exit(0);
        }
        case 0:
        {
            setenv("CR_FORK", "1", 1);
            LOGB("\tfork-and-rename(%d) started\n", getpid());

            if (-1 == copy(getenv("CR_PATH"), new_path)) {
                ERROR("\t\tcopy(%s, %s) failed: %d, %s\n", getenv("CR_PATH"),
                    new_path, errno, strerror(errno));
                exit(1);
            }

            execve(new_path, argv, environ);
            exit(1);
            break;
        }
        default:
        {
            int status = 0;
            waitpid(pid, &status, 0);

            rm_rf(dirname(new_path));

            if (in_fork_and_rename) {
                LOGB("\tfork-and-rename(%d) exiting\n", getpid());
                exit(0);
            }
        }
    }

    free_argv(argv);
}

void quark_rm_rf(prm_rf_t args)
{
    char** argv = make_argv(args->argv);

    if (NULL == argv) {
        ERROR("\tmake_argv failed: %d, %s\n", errno, strerror(errno));
        exit(1);
    }

    LOGY("\tquark: remove(\"");
    char** arg = argv;
    while (*arg) {
        LOGY("%s ", *arg);
        rm_rf(*arg);
        arg++;
    }
    LOGY("\b\")\n");

    free_argv(argv);
}

int quark_copy(pcopy_t args)
{
    char** argv = make_argv(args->argv);

    if (NULL == argv) {
        ERROR("\tmake_argv failed: %d, %s\n", errno, strerror(errno));
        exit(1);
    }

    LOGY("\tquark: copy src=\"%s\" dst=\"%s\"\n", argv[0], argv[1]);

    int err = copy(argv[0], argv[1]);

    free_argv(argv);

    if (-1 == err) {
        ERROR("\t\tcopy failed: %d, %s\n", errno, strerror(errno));
    }

    return err;
}

static const char* __chmod_method(int method)
{
    switch (method)
    {
        case METHOD_PATH: return "chmod";
        case METHOD_DESCRIPTOR: return "fchmod";
        case METHOD_AT_DESCRIPTOR: return "fchmodat";
        default: break;
    }
    return "unknown";
}

int quark_chmod(pchmod_t args)
{
    int err = -1;
    int fd = -1;

    LOGY("\tquark: %s mode=%o target=\"%s\"\n",
        __chmod_method(args->method), args->mode, args->path);

    switch (args->method)
    {
        case METHOD_PATH:
        case METHOD_AT_DESCRIPTOR:
            if (METHOD_PATH == args->method) {
                err = chmod(args->path, args->mode);
            } else {
                err = fchmodat(-1, args->path, args->mode, 0);
            }
            break;
        case METHOD_DESCRIPTOR:
            fd = open(args->path, O_PATH);
            if (-1 == fd) {
                ERROR("\t\topen failed: %d, %s\n", errno, strerror(errno));
                goto Exit;
            }

            err = fchmod(fd, args->mode);
            break;

        default:
            break;
    }

    if (-1 == err) {
        ERROR("\t\t%s failed: %d, %s\n",
            __chmod_method(args->method), errno, strerror(errno));
    }

Exit:
    if (-1 != fd) {
        close(fd);
    }

    return err;
}

static const char* __chown_method(int method)
{
    switch (method)
    {
        case METHOD_PATH: return "chown";
        case METHOD_DESCRIPTOR: return "fchown";
        case METHOD_AT_DESCRIPTOR: return "fchownat";
        case METHOD_DONT_FOLLOW: return "lchown";
        default: break;
    }
    return "unknown";
}

int quark_chown(pchown_t args)
{
    int err = -1;
    int fd = -1;
    struct stat target_stat = {0};

    LOGY("\tquark: %s user=%s, group=%s, target=\"%s\"\n",
        __chown_method(args->method), args->user, args->group, args->path);

    if (-1 == stat(args->path, &target_stat)) {
        ERROR("\t\tstat failed: %d, %s\n", errno, strerror(errno));
        goto Exit;
    }

    if (strlen(args->user)) {
        struct passwd* pwd = getpwnam(args->user);
        // If no passwd was found, try converting to an integer
        if (!pwd) {
            target_stat.st_uid = (uid_t)strtol(args->user, NULL, 10);
        } else {
            target_stat.st_uid = pwd->pw_uid;
        }
    }

    if (strlen(args->group)) {
        struct group* group = getgrnam(args->group);
        // If no group was found, try converting to an integer
        if (!group) {
            target_stat.st_gid = (gid_t)strtol(args->group, NULL, 10);
        } else {
            target_stat.st_gid = group->gr_gid;
        }
    }

    if (METHOD_DESCRIPTOR == args->method) {
        fd = open(args->path, O_PATH);
        if (-1 == fd) {
            ERROR("\t\topen failed: %d, %s\n", errno, strerror(errno));
            goto Exit;
        }

        err = fchown(fd, target_stat.st_uid, target_stat.st_gid);
    } else if (METHOD_PATH == args->method) {
        err = chown(args->path, target_stat.st_uid, target_stat.st_gid);
    } else if (METHOD_DONT_FOLLOW == args->method) {
        err = lchown(args->path, target_stat.st_uid, target_stat.st_gid);
    } else {
        err = fchownat(-1, args->path, target_stat.st_uid, target_stat.st_gid, 0);
    }

    if (err) {
        ERROR("\t\t%s failed: %d, %s\n",
            __chown_method(args->method), errno, strerror(errno));
        goto Exit;
    }

Exit:

    if (-1 != fd) {
        close(fd);
    }

    return err;
}

static const char* __file_op_method(int flags)
{
    // Mask off the flags that don't relate to method types
    flags &= ~(FILE_OP_FLAG_BACKUP_AND_REVERT | FILE_OP_NO_DATA);
    switch (flags)
    {
        case 0: return "file-prepend";
        case FILE_OP_FLAG_CREATE: return "file-touch";
        case FILE_OP_FLAG_APPEND: return "file-append";
        case (FILE_OP_FLAG_CREATE | FILE_OP_FLAG_TRUNCATE): return "file-create";
        default: break;
    }
    return "unknown";
}

static const int __file_op_flags(int flags)
{
    int __flags = O_CLOEXEC | O_RDWR;

    if (FILE_OP_FLAG_CREATE & flags) {
        __flags |= O_CREAT;
    }

    if (FILE_OP_FLAG_APPEND & flags) {
        __flags |= O_APPEND;
    }

    if (FILE_OP_FLAG_CREATE & flags) {
        __flags |= O_CREAT;
    }

    if (FILE_OP_FLAG_TRUNCATE & flags) {
        __flags |= O_TRUNC;
    }

    return __flags;
}

int quark_file_op(pfile_op_t args)
{
    int err = -1;
    int fd = -1;
    char backup_path[PATH_MAX] = {0};

    LOGY("\tquark: %s backup-and-revert=%s data-len=%u target=\"%s\"\n",
        __file_op_method(args->flags),
        args->flags & FILE_OP_FLAG_BACKUP_AND_REVERT ? "yes" : "no",
        args->cb_bytes,
        args->path);

    if (args->flags & FILE_OP_FLAG_BACKUP_AND_REVERT) {
        strcpy(backup_path, args->path);
        strcat(backup_path, ".cr.backup");
        copy(args->path, backup_path);
    }

    fd = open(args->path, __file_op_flags(args->flags));
    if (-1 == fd) {
        ERROR("\t\topen failed: %d, %s\n", errno, strerror(errno));
        goto Exit;
    }
    fchmod(fd, 0700);

    if (!(args->flags & FILE_OP_NO_DATA) && args->cb_bytes) {
        if (-1 == write(fd, args->bytes, args->cb_bytes)) {
            ERROR("\t\twrite failed: %d, %s\n", errno, strerror(errno));
            goto Exit;
        }
    }

    err = 0;
Exit:

    if (-1 != fd) {
        close(fd);
    }

    if (args->flags & FILE_OP_FLAG_BACKUP_AND_REVERT) {
        copy(backup_path, args->path);
        unlink(backup_path);
    }

    return err;
}

void quark_sleep(psleep_t args)
{
    unsigned int seconds = 0;

    seconds = args->seconds;
    LOGY("\tquark: sleep seconds=%d\n", seconds);
    sleep(seconds);
}
