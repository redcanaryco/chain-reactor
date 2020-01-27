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

#include <limits.h>

#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"

int make_temp_dir(char path[PATH_MAX]);

int copy(char* source, char* destination);

void rm_rf(char* target);

int urand(void* buffer, size_t cb_buffer);

char** make_argv(const char double_null_terminated_array[]);

void free_argv(char** argv);

const char* red_canary();

#define LOG(...) \
do \
{ \
    fprintf(stdout, __VA_ARGS__); \
    fprintf(stdout, ANSI_COLOR_RESET); \
} while(0)

#define LOGI(INDENT, ...) \
do \
{ \
    for (int ii = 0; ii < INDENT; ++ii) { \
        fprintf(stdout, "\t"); \
    } \
    LOG(__VA_ARGS__); \
} while(0)

#define ERROR(...) \
do \
{ \
    fprintf(stdout, ANSI_COLOR_RED __VA_ARGS__); \
    fprintf(stdout, ANSI_COLOR_RESET); \
} while(0)

#define LOGB(...) LOG(ANSI_COLOR_BLUE __VA_ARGS__)
#define LOGY(...) LOG(ANSI_COLOR_YELLOW __VA_ARGS__)
#define LOGG(...) LOG(ANSI_COLOR_GREEN __VA_ARGS__)
#define LOGM(...) LOG(ANSI_COLOR_MAGENTA __VA_ARGS__)
#define LOGC(...) LOG(ANSI_COLOR_CYAN __VA_ARGS__)
