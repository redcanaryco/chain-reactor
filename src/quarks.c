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

#include "atoms.h"
#include "util.h"

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
    LOGY("\tquark: %s(\"", EXEC_METHOD_PATH == args->method ? "execve" : "execveat");
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

    for (int jj = 0; jj < argc; ++jj) {
        if (wordexp(argv[jj], &expanded_arg, WRDE_REUSE)) {
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

            if (EXEC_METHOD_DESCRIPTOR == args->method) {
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
                EXEC_METHOD_PATH == args->method ? "execvp" : "execveat",
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

    switch ((pid = fork()))
    {
        case -1:
        {
            ERROR("\tfork failed: %d, %s\n", errno, strerror(errno));
            rm_rf(new_path);
            exit(0);
        }
        case 0:
        {
            setenv("CR_FORK", "1", 1);
            LOGB("\tfork-and-rename(%d) started\n", getpid());

            if (-1 == copy(getenv("CR_PATH"), new_path)) {
                ERROR("\tcopy(%s, %s) failed: %d, %s\n", getenv("CR_PATH"),
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
        ERROR("\tcopy failed: %d, %s\n", errno, strerror(errno));
    }

    return err;
}
