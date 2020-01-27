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
#include <limits.h>
#include <dirent.h>

char** make_argv(const char double_null_terminated_array[])
{
    size_t c_cb;
    size_t c_args = 0;
    char** argv = NULL;
    const char* iter = double_null_terminated_array;

    while((c_cb = strlen(iter))) {
        c_args++;
        iter += c_cb + 1;
    }

    // Allocate an extra so its null terminated.
    argv = calloc(c_args + 1, sizeof(*argv));
    if (!argv) {
        return NULL;
    }

    iter = double_null_terminated_array;
    for (int ii = 0; ii < c_args; ++ii, iter += c_cb + 1) {
        argv[ii] = strdup(iter);
        c_cb = strlen(argv[ii]);
    }

    return argv;
}

void free_argv(char** argv)
{
    char** ii = argv;

    while (ii[0]) {
        free(ii[0]);
        ii++;
    }

    free(argv);
}

int urand(void* buffer, size_t cb_buffer)
{
    int urand_fd = open("/dev/urandom", O_RDONLY);
    read(urand_fd, buffer, cb_buffer);
    close(urand_fd);
}

int make_temp_dir(char path[PATH_MAX])
{
    unsigned int id = 0;
    urand(&id, sizeof(id));
    snprintf(path, PATH_MAX, "/tmp/cr-%u", id);
    return mkdir(path, 0777);
}

void rm_rf(char* target)
{
    struct stat target_stat = {0};

    if (-1 == stat(target, &target_stat)) {
        return;
    }

    if (S_ISDIR(target_stat.st_mode)) {
        DIR* dir = opendir(target);

        for (struct dirent* de = readdir(dir); de; de = readdir(dir)) {
            char new_target[PATH_MAX];

            if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) {
                continue;
            }

            if ('/' == target[strlen(target)-1]) {
                snprintf(new_target, PATH_MAX, "%s%s", target, de->d_name);
            } else {
                snprintf(new_target, PATH_MAX, "%s/%s", target, de->d_name);
            }

            rm_rf(new_target);
        }

        closedir(dir);
        rmdir(target);
    } else {
        unlink(target);
    }
}

int copy(char* source, char* destination)
{
    int err = -1;
    int source_fd = -1;
    int destination_fd = -1;
    ssize_t cb_op = 0;
    char transfer[1024*16];
    struct stat source_stat = {0};

    source_fd = open(source, O_RDONLY | O_CLOEXEC);
    if (-1 == source_fd) {
        goto Exit;
    }

    if (-1 == fstat(source_fd, &source_stat)) {
        goto Exit;
    }

    destination_fd = open(destination,
        O_WRONLY | O_CREAT | O_CLOEXEC | O_TRUNC, source_stat.st_mode & ~S_IFMT);
    if (-1 == destination_fd) {
        goto Exit;
    }

    while(0 < (cb_op = read(source_fd, transfer, sizeof(transfer)))) {
        cb_op = write(destination_fd, transfer, cb_op);
        if (-1 == cb_op) {
            goto Exit;
        }
    }

    if (-1 == cb_op) {
        goto Exit;
    }

    err = 0;

Exit:

    if (-1 != source_fd) {
        close(source_fd);
    }

    if (-1 != destination_fd) {
        close(destination_fd);
    }

    return err;
}

const char* red_canary()
{
    static const char* header =
"\x1b[31m"
"              .S_sSSs      sSSs \n"
"              .SS~YS%%b    d%%SP\n"
"              S%S   `S%b  d%S'  \n"
"              S%S    S%S  S%S   \n"
"              S%S    d*S  S&S   \n"
"              S&S   .S*S  S&S   \n"
"              S&S_sdSSS   S&S   \n"
"              S&S~YSY%b   S&S   \n"
"              S*S   `S%b  S*b   \n"
"              S*S    S%S  S*S.  \n"
"              S*S    S&S   SSSbs\n"
"              S*S    SSS    YSSP\n"
"              SP                \n"
"              Y                 \n\x1b[0m"
" `+ymmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmy+` \n"
":dmmhsssssssssssssssssssssssssssssssssssssssshmmd/\n"
"dmm+`                                        `+mmm\n"
"mmm:      \x1b[31m`:+oss+:`\x1b[0m                           :mmm\n"
"mmm:   \x1b[31m`:oyyyyyyyys/`\x1b[0m                         :mmm\n"
"mmm:    \x1b[31m.:oyyyyyyyyys:`\x1b[0m                       :mmm\n"
"mmm:      \x1b[31m-syyyyyyyyyyo:`\x1b[0m                     :mmm\n"
"mmm:       \x1b[31m-yyyyyyyyyyyyy+.\x1b[0m                   :mmm\n"
"mmm:       \x1b[31m.syyyyyyyyyyyyyy/`\x1b[0m                 :mmm\n"
"mmm:       \x1b[31m`oyyyyyyyyyyyyyyys.\x1b[0m                :mmm\n"
"mmm:        \x1b[31m:yyyyyyyyyyyyyyyys-\x1b[0m               :mmm\n"
"mmm:        \x1b[31m`+yyyyyyyyyyyyyyyyy:\x1b[0m              :mmm\n"
"mmm:         \x1b[31m`oyyyyyyyyyyyyyyyyy-\x1b[0m             :mmm\n"
"mmm:          \x1b[31m`/yyyyyyyyyyyyyyyys-\x1b[0m            :mmm\n"
"mmm:            \x1b[31m./yyyyyyyyyyyyyyys`\x1b[0m          `/mmm\n"
"mmm:              \x1b[31m`/yysyyyyyyyyyyy+`\x1b[0m  `.:+oydmmmmm\n"
"mmm:               \x1b[31mç-o- ``...osyyyyyo\x1b[0mydmmmmmmdyymmm\n"
"mmm:              \x1b[31m::\x1b[0m    \x1b[31m`-:syhdh\x1b[31myyyyy\x1b[0mhyo+:.`  :mmm\n"
"mmm:            -o//+shdmmhhmdhyo\x1b[31m/syyyo.\x1b[0m      :mmm\n"
"mmm:     `.-/oyyyddmmmdhyo/-.`    \x1b[31m`/yyys.\x1b[0m     :mmm\n"
"mmm+:/oyhdmmmmmhys+/-.`             \x1b[31m-syys-\x1b[0m    :mmm\n"
"mmmmmmmmdhs+:-``                     \x1b[31m.sy+-`\x1b[0m   :mmm\n"
"dmmd+:-`                              \x1b[31m.ss.\x1b[0m   `+mmm\n"
":dmmhssssssssssssssssssssssssssssssssssyssssshmmd/\n"
" `+hmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmh+` \n";

    return header;
}
