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
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "atoms.h"
#include "util.h"

int quark_listen(plisten_t args);
int quark_connect(pconnect_t args, int silent);
int quark_exec(pexec_t args);
void quark_fork_and_rename(pfork_and_rename_t args, int in_fork_and_rename);
void quark_rm_rf(prm_rf_t args);
int quark_copy(pcopy_t args);
int quark_chmod(pchmod_t args);
int quark_chown(pchown_t args);
int quark_file_op(pfile_op_t args);
void quark_sleep(psleep_t args);


int split_atom(patom_t atom, int in_fork_and_rename)
{
    int err = -1;
    char transform[33] = {0};
    int start_quark_index = 0;
    pquark_t quark = NULL;

    if (!atom) {
        errno = EINVAL;
        goto Exit;
    }

    if (in_fork_and_rename) {
        start_quark_index = atoi(getenv("CR_QUARK_INDEX")) + 1;
    }

    quark = &atom->quarks[0];

    for (int ii = 0;
         ii < atom->num_quarks;
         ++ii, quark = (pquark_t)(((char*)quark) + quark->cb)) {

        void* quark_body = (void*)(((char*)quark) + sizeof(*quark));
        sprintf(transform, "%d", ii);
        setenv("CR_QUARK_INDEX", transform, 1);

        // If we are in a fork and rename, we want to fast forward to where
        // we left off.
        if (start_quark_index > ii) {
            continue;
        }

        if (!strcasecmp("exec", quark->type)) {
            if (-1 == quark_exec((pexec_t)quark_body)) {
                goto Exit;
            }
        } else if (!strcasecmp("fork-and-rename", quark->type)) {
            quark_fork_and_rename((pfork_and_rename_t)quark_body, in_fork_and_rename);
            err = 0;
            goto Exit;
        } else if (!strcasecmp("connect", quark->type)) {
            if (-1 == quark_connect((pconnect_t)quark_body, 0)) {
                goto Exit;
            }
        } else if (!strcasecmp("listen", quark->type)) {
            if (-1 == quark_listen((plisten_t)quark_body)) {
                goto Exit;
            }
        } else if (!strcasecmp("remove", quark->type)) {
            quark_rm_rf((prm_rf_t)quark_body);
        } else if (!strcasecmp("copy", quark->type)) {
            if (-1 == quark_copy((pcopy_t)quark_body)) {
                goto Exit;
            }
        } else if (!strcasecmp("file-op", quark->type)) {
            if (-1 == quark_file_op((pfile_op_t)quark_body)) {
                goto Exit;
            }
        } else if (!strcasecmp("chmod", quark->type)) {
            if (-1 == quark_chmod((pchmod_t)quark_body)) {
                goto Exit;
            }
        } else if (!strcasecmp("chown", quark->type)) {
            if (-1 == quark_chown((pchown_t)quark_body)) {
                goto Exit;
            }
        } else if (!strcasecmp("sleep", quark->type)) {
            quark_sleep((psleep_t)quark_body);
        } else {
            ERROR("unknown quark type %s\n", quark->type);
            errno = EINVAL;
            goto Exit;
        }
    }

    err = 0;

Exit:

    if (in_fork_and_rename) {
        LOGB("\tfork-and-rename(%d) exiting\n", getpid());
        exit(0);
    }

    return err;
}
