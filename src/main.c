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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

#include "util.h"
#include "atoms.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*a))

typedef struct {
    char name[64];
    unsigned int num_reactions;
    char reactions[];
} reactions_t, *preactions_t;

typedef struct {
    unsigned int cb;
    unsigned int num_atoms;
    atom_t atoms[];
} atoms_t, *patoms_t;

static patoms_t g_atoms = NULL;
static preactions_t g_reactions = NULL;

static unsigned int g_elf_size __attribute__ ((section(".elf_size")));

static void initialize()
{
    void* self_exe = NULL;
    struct stat self_stat = {0};

    int self_fd = open("/proc/self/exe", O_RDONLY | O_CLOEXEC);
    if (-1 == self_fd) {
        ERROR("open(/proc/self/exe) failed: %d, %s\n", errno, strerror(errno));
        exit(1);
    }

    if (-1 == fstat(self_fd, &self_stat)) {
        ERROR("fstat failed: %d, %s\n", errno, strerror(errno));
        exit(1);
    }

    self_exe = mmap(NULL, self_stat.st_size, PROT_READ, MAP_SHARED, self_fd, 0);
    if (MAP_FAILED == self_exe) {
        ERROR("mmap failed: %d, %s\n", errno, strerror(errno));
        exit(1);
    }

    // The atoms and reaction are appended to the end of the ELF executable.
    g_atoms = (patoms_t)((char*)self_exe + g_elf_size);
    g_reactions = (preactions_t)((char*)g_atoms + g_atoms->cb);
}

void main(int argc, char** argv)
{
    char transform[33] = {0};
    int reaction_index = 0;
    int in_fork_and_rename = 0;

    if (getenv("CR_FORK")) {
        reaction_index = atoi(getenv("CR_REACTION_INDEX"));
        in_fork_and_rename = 1;
        unsetenv("CR_FORK");
    } else if (argc > 1 && 0 == strcmp(argv[1], "exit")) {
        exit(0);
    }

    sprintf(transform, "%d", getpid());

    setenv("PPID", transform, 1);
    setenv("CR_PATH", argv[0], 0);

    initialize();

    if (!in_fork_and_rename) {
        LOG("%s", red_canary());
        LOGB("chain reaction" ANSI_COLOR_CYAN " \"%s\"" ANSI_COLOR_RESET " %d %d\n",
            g_reactions->name, getuid(), geteuid());
    }

    char* current_reaction = &g_reactions->reactions[0];

    for (int ii = 0; ii < reaction_index; ++ii) {
        current_reaction += strlen(current_reaction) + 1;
    }

    while(*current_reaction) {
        sprintf(transform, "%d", reaction_index);
        setenv("CR_REACTION_INDEX", transform, 1);

        int found = 0;
        patom_t atom = &g_atoms->atoms[0];
        for (int ii = 0; ii < g_atoms->num_atoms; ++ii, atom = (patom_t)((char*)atom + atom->cb)) {
            if (0 == strcasecmp(current_reaction, atom->name)) {
                found = 1;
                if (!in_fork_and_rename) {
                    LOGG("atom: " ANSI_COLOR_MAGENTA "%s\n", current_reaction);
                }

                if (-1 == split_atom(atom, in_fork_and_rename)) {
                    if (!in_fork_and_rename) {
                        ERROR("\tfailed\n");
                    }
                }

                break;
            }
        }

        if (!found) {
            ERROR("\tunknown atom: %s\n", current_reaction);
        }

        current_reaction += strlen(current_reaction) + 1;
        reaction_index++;
    }

    LOGB("chain reaction" ANSI_COLOR_RESET " complete\n");
}
