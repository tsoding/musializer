// This is a complete backward incompatible rewrite of https://github.com/tsoding/nobuild
// because I'm really unhappy with the direction it is going. It's gonna sit in this repo
// until it's matured enough and then I'll probably extract it to its own repo.

// Copyright 2023 Alexey Kutepov <reximkut@gmail.com>
//
// Permission is hereby granted, free of charge, to any person obtaining
// a copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
//
// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#ifndef NOB_H_
#define NOB_H_

#define NOB_ASSERT assert
#define NOB_REALLOC realloc
#define NOB_FREE free

#ifdef _WIN32
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#    include <direct.h>
#    include <shellapi.h>
#else
#    include <sys/types.h>
#    include <sys/wait.h>
#    include <sys/stat.h>
#    include <unistd.h>
#    include <fcntl.h>
#endif

#define NOB_ARRAY_LEN(array) (sizeof(array)/sizeof(array[0]))
#define NOB_ARRAY_GET(array, index) (NOB_ASSERT(index >= 0), NOB_ASSERT(index < NOB_ARRAY_LEN(array)), array[index])

typedef enum {
    NOB_INFO,
    NOB_WARNING,
    NOB_ERROR,
} Nob_Log_Level;

void nob_log(Nob_Log_Level level, const char *fmt, ...);

// It is an equivalent of shift command from bash. It basically pops a command line
// argument from the beginning.
char *nob_shift_args(int *argc, char ***argv);

typedef struct {
    const char **items;
    size_t count;
    size_t capacity;
} Nob_File_Paths;

typedef enum {
    NOB_FILE_REGULAR = 0,
    NOB_FILE_DIRECTORY,
    NOB_FILE_SYMLINK,
    NOB_FILE_OTHER,
} Nob_File_Type;

bool nob_mkdir_if_not_exists(const char *path);
bool nob_copy_file(const char *src_path, const char *dst_path);
bool nob_copy_directory_recursively(const char *src_path, const char *dst_path);
bool nob_read_entire_dir(const char *parent, Nob_File_Paths *children);
Nob_File_Type nob_get_file_type(const char *path);

#define nob_return_defer(value) do { result = (value); goto defer; } while(0)

// Initial capacity of a dynamic array
#define NOB_DA_INIT_CAP 256

// Append an item to a dynamic array
#define nob_da_append(da, item)                                                          \
    do {                                                                                 \
        if ((da)->count >= (da)->capacity) {                                             \
            (da)->capacity = (da)->capacity == 0 ? NOB_DA_INIT_CAP : (da)->capacity*2;   \
            (da)->items = NOB_REALLOC((da)->items, (da)->capacity*sizeof(*(da)->items)); \
            NOB_ASSERT((da)->items != NULL && "Buy more RAM lol");                       \
        }                                                                                \
                                                                                         \
        (da)->items[(da)->count++] = (item);                                             \
    } while (0)

#define nob_da_free(da) NOB_FREE((da).items)

// Append several items to a dynamic array
#define nob_da_append_many(da, new_items, new_items_count)                                  \
    do {                                                                                    \
        if ((da)->count + new_items_count > (da)->capacity) {                               \
            if ((da)->capacity == 0) {                                                      \
                (da)->capacity = NOB_DA_INIT_CAP;                                           \
            }                                                                               \
            while ((da)->count + new_items_count > (da)->capacity) {                        \
                (da)->capacity *= 2;                                                        \
            }                                                                               \
            (da)->items = NOB_REALLOC((da)->items, (da)->capacity*sizeof(*(da)->items));    \
            NOB_ASSERT((da)->items != NULL && "Buy more RAM lol");                          \
        }                                                                                   \
        memcpy((da)->items + (da)->count, new_items, new_items_count*sizeof(*(da)->items)); \
        (da)->count += new_items_count;                                                     \
    } while (0)

typedef struct {
    char *items;
    size_t count;
    size_t capacity;
} Nob_String_Builder;

// Append a sized buffer to a string builder
#define nob_sb_append_buf(sb, buf, size) nob_da_append_many(sb, buf, size)

// Append a NULL-terminated string to a string builder
#define nob_sb_append_cstr(sb, cstr)  \
    do {                              \
        const char *s = (cstr);       \
        size_t n = strlen(s);         \
        nob_da_append_many(sb, s, n); \
    } while (0)

// Append a single NULL character at the end of a string builder. So then you can
// use it a NULL-terminated C string
#define nob_sb_append_null(sb) nob_da_append_many(sb, "", 1)

// Free the memory allocated by a string builder
#define nob_sb_free(sb) NOB_FREE((sb).items)

// Process handle
#ifdef _WIN32
typedef HANDLE Nob_Proc;
#define NOB_INVALID_PROC NULL
#else
typedef int Nob_Proc;
#define NOB_INVALID_PROC -1
#endif // _WIN32

// Wait until the process has finished
bool nob_proc_wait(Nob_Proc proc);

// A command - the main workhorse of Nob. Nob is all about building commands an running them
typedef struct {
    const char **items;
    size_t count;
    size_t capacity;
} Nob_Cmd;

// Render a string representation of a command into a string builder. Keep in mind the the
// string builder is not NULL-terminated by default. Use nob_sb_append_null if you plan to
// use it as a C string.
void nob_cmd_render(Nob_Cmd cmd, Nob_String_Builder *render);

// Append several arguments to the command. The last argument must be NULL, because this is
// C variadics. They can't tell when the arguments stop, so we have to indicate that with
// NULL. You should probably not use this function. Use nob_cmd_append macro instead.
void nob_cmd_append_null(Nob_Cmd *cmd, ...);

// Wrapper around nob_cmd_append_null that does not require NULL at the end.
#define nob_cmd_append(cmd, ...) nob_cmd_append_null(cmd, __VA_ARGS__, NULL)

Nob_Cmd nob_cmd_inline_null(void *first, ...);
#define nob_cmd_inline(...) nob_cmd_inline_null(NULL, __VA_ARGS__, NULL)
// TODO: NOB_CMD leaks the command
#define NOB_CMD(...) nob_cmd_run_sync(nob_cmd_inline(__VA_ARGS__))

// Free all the memory allocated by command arguments
#define nob_cmd_free(cmd) NOB_FREE(cmd.items)

// Run command asynchronously
Nob_Proc nob_cmd_run_async(Nob_Cmd cmd);

// Run command synchronously
bool nob_cmd_run_sync(Nob_Cmd cmd);

#ifndef NOB_TEMP_CAPACITY
#define NOB_TEMP_CAPACITY (8*1024*1024)
#endif // NOB_TEMP_CAPACITY
char *nob_temp_strdup(const char *cstr);
void *nob_temp_alloc(size_t size);
void nob_temp_reset(void);
size_t nob_temp_save(void);
void nob_temp_rewind(size_t checkpoint);

int is_path1_modified_after_path2(const char *path1, const char *path2);
bool nob_rename(const char *old_path, const char *new_path);

// TODO: add MinGW support for Go Rebuild Urself™ Technology
#ifndef NOB_REBUILD_URSELF
#  if _WIN32
#    if defined(__GNUC__)
#       define NOB_REBUILD_URSELF(binary_path, source_path) NOB_CMD("gcc", "-o", binary_path, source_path)
#    elif defined(__clang__)
#       define NOB_REBUILD_URSELF(binary_path, source_path) NOB_CMD("clang", "-o", binary_path, source_path)
#    elif defined(_MSC_VER)
#       define NOB_REBUILD_URSELF(binary_path, source_path) NOB_CMD("cl.exe", source_path)
#    endif
#  else
#    define NOB_REBUILD_URSELF(binary_path, source_path) NOB_CMD("cc", "-o", binary_path, source_path)
#  endif
#endif

// Go Rebuild Urself™ Technology
//
//   How to use it:
//     int main(int argc, char** argv) {
//         GO_REBUILD_URSELF(argc, argv);
//         // actual work
//         return 0;
//     }
//
//   After your added this macro every time you run ./nobuild it will detect
//   that you modified its original source code and will try to rebuild itself
//   before doing any actual work. So you only need to bootstrap your build system
//   once.
//
//   The modification is detected by comparing the last modified times of the executable
//   and its source code. The same way the make utility usually does it.
//
//   The rebuilding is done by using the REBUILD_URSELF macro which you can redefine
//   if you need a special way of bootstraping your build system. (which I personally
//   do not recommend since the whole idea of nobuild is to keep the process of bootstrapping
//   as simple as possible and doing all of the actual work inside of the nobuild)
//
#define NOB_GO_REBUILD_URSELF(argc, argv)                                                    \
    do {                                                                                     \
        const char *source_path = __FILE__;                                                  \
        assert(argc >= 1);                                                                   \
        const char *binary_path = argv[0];                                                   \
                                                                                             \
        int rebuild_is_needed = nob_is_path1_modified_after_path2(source_path, binary_path); \
        if (rebuild_is_needed < 0) exit(1);                                                  \
        if (rebuild_is_needed) {                                                             \
            Nob_String_Builder sb = {0};                                                     \
            nob_sb_append_cstr(&sb, binary_path);                                            \
            nob_sb_append_cstr(&sb, ".old");                                                 \
            nob_sb_append_null(&sb);                                                         \
                                                                                             \
            if (!nob_rename(binary_path, sb.items)) exit(1);                                 \
            if (!NOB_REBUILD_URSELF(binary_path, source_path)) {                             \
                nob_rename(sb.items, binary_path);                                           \
                exit(1);                                                                     \
            }                                                                                \
                                                                                             \
            Nob_Cmd cmd = {0};                                                               \
            nob_da_append_many(&cmd, argv, argc);                                            \
            if (!nob_cmd_run_sync(cmd)) {                                                    \
                nob_rename(sb.items, binary_path);                                           \
                exit(1);                                                                     \
            }                                                                                \
                                                                                             \
            exit(0);                                                                         \
        }                                                                                    \
    } while(0)
// The implementation idea is stolen from https://github.com/zhiayang/nabs


// minirent.h HEADER BEGIN ////////////////////////////////////////
// Copyright 2021 Alexey Kutepov <reximkut@gmail.com>
//
// Permission is hereby granted, free of charge, to any person obtaining
// a copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
//
// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
// ============================================================
//
// minirent — 0.0.1 — A subset of dirent interface for Windows.
//
// https://github.com/tsoding/minirent
//
// ============================================================
//
// ChangeLog (https://semver.org/ is implied)
//
//    0.0.2 Automatically include dirent.h on non-Windows
//          platforms
//    0.0.1 First Official Release

#ifndef _WIN32
#include <dirent.h>
#else // _WIN32

#define WIN32_LEAN_AND_MEAN
#include "windows.h"

struct dirent
{
    char d_name[MAX_PATH+1];
};

typedef struct DIR DIR;

DIR *opendir(const char *dirpath);
struct dirent *readdir(DIR *dirp);
int closedir(DIR *dirp);
#endif // _WIN32
// minirent.h HEADER END ////////////////////////////////////////

#endif // NOB_H_

#ifdef NOB_IMPLEMENTATION

static size_t nob_temp_size = 0;
static char nob_temp[NOB_TEMP_CAPACITY] = {0};

bool nob_mkdir_if_not_exists(const char *path)
{
#ifdef _WIN32
    int result = mkdir(path);
#else
    int result = mkdir(path, 0755);
#endif
    if (result < 0) {
        if (errno == EEXIST) {
            nob_log(NOB_INFO, "directory `%s` already exists", path);
            return true;
        }
        nob_log(NOB_ERROR, "could not create directory `%s`: %s", path, strerror(errno));
        return false;
    }

    nob_log(NOB_INFO, "created directory `%s`", path);
    return true;
}

bool nob_copy_file(const char *src_path, const char *dst_path)
{
    nob_log(NOB_INFO, "copying %s -> %s", src_path, dst_path);
#ifdef _WIN32
    if (!CopyFile(src_path, dst_path, FALSE)) {
        nob_log(NOB_ERROR, "Could not copy file: %lu", GetLastError());
        return false;
    }
    return true;
#else
    int src_fd = -1;
    int dst_fd = -1;
    size_t buf_size = 32*1024;
    char *buf = NOB_REALLOC(NULL, buf_size);
    NOB_ASSERT(buf != NULL && "Buy more RAM lol!!");
    bool result = true;

    src_fd = open(src_path, O_RDONLY);
    if (src_fd < 0) {
        nob_log(NOB_ERROR, "Could not open file %s: %s", src_path, strerror(errno));
        nob_return_defer(false);
    }

    struct stat src_stat;
    if (fstat(src_fd, &src_stat) < 0) {
        nob_log(NOB_ERROR, "Could not get mode of file %s: %s", src_path, strerror(errno));
        nob_return_defer(false);
    }

    dst_fd = open(dst_path, O_CREAT | O_TRUNC | O_WRONLY, src_stat.st_mode);
    if (dst_fd < 0) {
        nob_log(NOB_ERROR, "Could not create file %s: %s", dst_path, strerror(errno));
        nob_return_defer(false);
    }

    for (;;) {
        ssize_t n = read(src_fd, buf, buf_size);
        if (n == 0) break;
        if (n < 0) {
            nob_log(NOB_ERROR, "Could not read from file %s: %s", src_path, strerror(errno));
            nob_return_defer(false);
        }
        char *buf2 = buf;
        while (n > 0) {
            ssize_t m = write(dst_fd, buf2, n);
            if (m < 0) {
                nob_log(NOB_ERROR, "Could not write to file %s: %s", dst_path, strerror(errno));
                nob_return_defer(false);
            }
            n    -= m;
            buf2 += m;
        }
    }

defer:
    free(buf);
    close(src_fd);
    close(dst_fd);
    return result;
#endif
}

void nob_cmd_render(Nob_Cmd cmd, Nob_String_Builder *render)
{
    for (size_t i = 0; i < cmd.count; ++i) {
        const char *arg = cmd.items[i];
        if (i > 0) nob_sb_append_cstr(render, " ");
        if (!strchr(arg, ' ')) {
            nob_sb_append_cstr(render, arg);
        } else {
            nob_da_append(render, '\'');
            nob_sb_append_cstr(render, arg);
            nob_da_append(render, '\'');
        }
    }
}

void nob_cmd_append_null(Nob_Cmd *cmd, ...)
{
    va_list args;
    va_start(args, cmd);

    const char *arg = va_arg(args, const char*);
    while (arg != NULL) {
        nob_da_append(cmd, arg);
        arg = va_arg(args, const char*);
    }

    va_end(args);
}

Nob_Proc nob_cmd_run_async(Nob_Cmd cmd)
{
    Nob_String_Builder sb = {0};
    nob_cmd_render(cmd, &sb);
    nob_sb_append_null(&sb);
    nob_log(NOB_INFO, "CMD: %s", sb.items);
    nob_sb_free(sb);

#ifdef _WIN32
    // https://docs.microsoft.com/en-us/windows/win32/procthread/creating-a-child-process-with-redirected-input-and-output

    STARTUPINFO siStartInfo;
    ZeroMemory(&siStartInfo, sizeof(siStartInfo));
    siStartInfo.cb = sizeof(STARTUPINFO);
    // NOTE: theoretically setting NULL to std handles should not be a problem
    // https://docs.microsoft.com/en-us/windows/console/getstdhandle?redirectedfrom=MSDN#attachdetach-behavior
    // TODO: check for errors in GetStdHandle
    siStartInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    siStartInfo.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    siStartInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

    PROCESS_INFORMATION piProcInfo;
    ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));

    Nob_String_Builder sb = {0};
    nob_cmd_render(cmd, &sb);
    nob_sb_append_null(&sb);
    BOOL bSuccess = CreateProcess(NULL, sb.items, NULL, NULL, TRUE, 0, NULL, NULL, &siStartInfo, &piProcInfo);
    nob_sb_free(sb);

    if (!bSuccess) {
        nob_log(NOB_ERROR, "Could not create child process: %lu", GetLastError());
        return NOB_INVALID_PROC;
    }

    CloseHandle(piProcInfo.hThread);

    return piProcInfo.hProcess;
#else
    pid_t cpid = fork();
    if (cpid < 0) {
        nob_log(NOB_ERROR, "Could not fork child process: %s", strerror(errno));
        return NOB_INVALID_PROC;
    }

    if (cpid == 0) {
        if (execvp(cmd.items[0], (char * const*) cmd.items) < 0) {
            nob_log(NOB_ERROR, "Could not exec child process: %s", strerror(errno));
            exit(1);
        }
        NOB_ASSERT(0 && "unreachable");
    }

    return cpid;
#endif
}

bool nob_proc_wait(Nob_Proc proc)
{
#ifdef _WIN32
    DWORD result = WaitForSingleObject(
                       proc,    // HANDLE hHandle,
                       INFINITE // DWORD  dwMilliseconds
                   );

    if (result == WAIT_FAILED) {
        nob_log(NOB_ERROR, "could not wait on child process: %lu", GetLastError());
        return false;
    }

    DWORD exit_status;
    if (!GetExitCodeProcess(proc, &exit_status)) {
        nob_log(NOB_ERROR, "could not get process exit code: %lu", GetLastError());
        return false;
    }

    if (exit_status != 0) {
        nob_log(NOB_ERROR, "command exited with exit code %lu", exit_status);
        return false;
    }

    CloseHandle(proc);

    return false;
#else
    for (;;) {
        int wstatus = 0;
        if (waitpid(proc, &wstatus, 0) < 0) {
            nob_log(NOB_ERROR, "could not wait on command (pid %d): %s", proc, strerror(errno));
            return false;
        }

        if (WIFEXITED(wstatus)) {
            int exit_status = WEXITSTATUS(wstatus);
            if (exit_status != 0) {
                nob_log(NOB_ERROR, "command exited with exit code %d", exit_status);
                return false;
            }

            break;
        }

        if (WIFSIGNALED(wstatus)) {
            nob_log(NOB_ERROR, "command process was terminated by %s", strsignal(WTERMSIG(wstatus)));
            return false;
        }
    }

    return true;
#endif
}

bool nob_cmd_run_sync(Nob_Cmd cmd)
{
    Nob_Proc p = nob_cmd_run_async(cmd);
    if (p == NOB_INVALID_PROC) return false;
    return nob_proc_wait(p);
}

char *nob_shift_args(int *argc, char ***argv)
{
    NOB_ASSERT(*argc > 0);
    char *result = **argv;
    (*argv) += 1;
    (*argc) -= 1;
    return result;
}

void nob_log(Nob_Log_Level level, const char *fmt, ...)
{
    switch (level) {
    case NOB_INFO:
        fprintf(stderr, "[INFO] ");
        break;
    case NOB_WARNING:
        fprintf(stderr, "[WARNING] ");
        break;
    case NOB_ERROR:
        fprintf(stderr, "[ERROR] ");
        break;
    default:
        NOB_ASSERT(0 && "unreachable");
    }

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}

bool nob_read_entire_dir(const char *parent, Nob_File_Paths *children)
{
    bool result = true;
    DIR *dir = NULL;

    dir = opendir(parent);
    if (dir == NULL) {
        nob_log(NOB_ERROR, "Could not open directory %s: %s", parent, strerror(errno));
        nob_return_defer(false);
    }

    errno = 0;
    struct dirent *ent = readdir(dir);
    while (ent != NULL) {
        nob_da_append(children, nob_temp_strdup(ent->d_name));
        ent = readdir(dir);
    }

    if (errno != 0) {
        nob_log(NOB_ERROR, "Could not read directory %s: %s", parent, strerror(errno));
        nob_return_defer(false);
    }

defer:
    if (dir) closedir(dir);
    return result;
}

Nob_File_Type nob_get_file_type(const char *path)
{
#ifdef _WIN32
    DWORD attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        nob_log(NOB_ERROR, "Could not get file attributes of %s: %lu", path, GetLastError());
        return -1;
    }

    if (attr & FILE_ATTRIBUTE_DIRECTORY) return NOB_FILE_DIRECTORY;
    // TODO: detect symlinks on Windows (whatever that means on Windows anyway)
    return NOB_FILE_REGULAR;
#else // _WIN32
    struct stat statbuf;
    if (stat(path, &statbuf) < 0) {
        nob_log(NOB_ERROR, "Could not get stat of %s: %s", path, strerror(errno));
        return -1;
    }

    switch (statbuf.st_mode & S_IFMT) {
        case S_IFDIR:  return NOB_FILE_DIRECTORY;
        case S_IFREG:  return NOB_FILE_REGULAR;
        case S_IFLNK:  return NOB_FILE_SYMLINK;
        default:       return NOB_FILE_OTHER;
    }
#endif // _WIN32
}

bool nob_copy_directory_recursively(const char *src_path, const char *dst_path)
{
    bool result = true;
    Nob_File_Paths children = {0};
    Nob_String_Builder src_sb = {0};
    Nob_String_Builder dst_sb = {0};
    size_t temp_checkpoint = nob_temp_save();

    Nob_File_Type type = nob_get_file_type(src_path);
    if (type < 0) return false;

    switch (type) {
        case NOB_FILE_DIRECTORY: {
            if (!nob_mkdir_if_not_exists(dst_path)) nob_return_defer(false);
            if (!nob_read_entire_dir(src_path, &children)) nob_return_defer(false);

            for (size_t i = 0; i < children.count; ++i) {
                if (strcmp(children.items[i], ".") == 0) continue;
                if (strcmp(children.items[i], "..") == 0) continue;

                src_sb.count = 0;
                nob_sb_append_cstr(&src_sb, src_path);
                nob_sb_append_cstr(&src_sb, "/");
                nob_sb_append_cstr(&src_sb, children.items[i]);
                nob_sb_append_null(&src_sb);

                dst_sb.count = 0;
                nob_sb_append_cstr(&dst_sb, dst_path);
                nob_sb_append_cstr(&dst_sb, "/");
                nob_sb_append_cstr(&dst_sb, children.items[i]);
                nob_sb_append_null(&dst_sb);

                if (!nob_copy_directory_recursively(src_sb.items, dst_sb.items)) {
                    nob_return_defer(false);
                }
            }
        } break;

        case NOB_FILE_REGULAR: {
            if (!nob_copy_file(src_path, dst_path)) {
                nob_return_defer(false);
            }
        } break;

        case NOB_FILE_SYMLINK: {
            nob_log(NOB_WARNING, "TODO: Copying symlinks is not supported yet");
        } break;

        case NOB_FILE_OTHER: {
            nob_log(NOB_ERROR, "Unsupported type of file %s", src_path);
            nob_return_defer(false);
        } break;

        default: NOB_ASSERT(0 && "unreachable");
    }

defer:
    nob_temp_rewind(temp_checkpoint);
    nob_da_free(src_sb);
    nob_da_free(dst_sb);
    nob_da_free(children);
    return result;
}

char *nob_temp_strdup(const char *cstr)
{
    size_t n = strlen(cstr);
    char *result = nob_temp_alloc(n + 1);
    NOB_ASSERT(result != NULL && "Increase NOB_TEMP_CAPACITY");
    memcpy(result, cstr, n);
    result[n] = '\0';
    return result;
}

void *nob_temp_alloc(size_t size)
{
    if (nob_temp_size + size > NOB_TEMP_CAPACITY) return NULL;
    void *result = &nob_temp[nob_temp_size];
    nob_temp_size += size;
    return result;
}

void nob_temp_reset(void)
{
    nob_temp_size = 0;
}

size_t nob_temp_save(void)
{
    return nob_temp_size;
}

void nob_temp_rewind(size_t checkpoint)
{
    nob_temp_size = checkpoint;
}

int nob_is_path1_modified_after_path2(const char *path1, const char *path2)
{
#ifdef _WIN32
    FILETIME path1_time, path2_time;

    HANDLE path1_fd = CreateFile(path1, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_READONLY, NULL);
    if (path1_fd == INVALID_HANDLE_VALUE) {
        nob_log(NOB_ERROR, "Could not open file %s: %lu", path1, GetLastError());
        return -1;
    }
    if (!GetFileTime(path1_fd, NULL, NULL, &path1_time)) {
        nob_log(NOB_ERROR, "Could not get time of %s: %lu", path1, GetLastError());
        return -1;
    }
    CloseHandle(path1_fd);

    HANDLE path2_fd = CreateFile(path2, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_READONLY, NULL);
    if (path2_fd == INVALID_HANDLE_VALUE) {
        nob_log(NOB_ERROR, "Could not open file %s: %lu", path2, GetLastError());
        return -1;
    }
    if (!GetFileTime(path2_fd, NULL, NULL, &path2_time)) {
        nob_log(NOB_ERROR, "Could not get time of %s: %lu", path2, GetLastError());
        return -1;
    }
    CloseHandle(path2_fd);

    return CompareFileTime(&path1_time, &path2_time) == 1;
#else
    struct stat statbuf = {0};

    if (stat(path1, &statbuf) < 0) {
        nob_log(NOB_ERROR, "Could not stat %s: %s\n", path1, strerror(errno));
        return -1;
    }
    int path1_time = statbuf.st_mtime;

    if (stat(path2, &statbuf) < 0) {
        nob_log(NOB_ERROR, "could not stat %s: %s\n", path2, strerror(errno));
        return -1;
    }
    int path2_time = statbuf.st_mtime;

    return path1_time > path2_time;
#endif
}

bool nob_rename(const char *old_path, const char *new_path)
{
    nob_log(NOB_INFO, "renaming %s -> %s", old_path, new_path);
#ifdef _WIN32
    if (!MoveFileEx(old_path, new_path, MOVEFILE_REPLACE_EXISTING)) {
        nob_log(NOB_ERROR, "could not rename %s to %s: %lu", old_path, new_path, GetLastError());
        return false;
    }
#else
    if (rename(old_path, new_path) < 0) {
        nob_log(NOB_ERROR, "could not rename %s to %s: %s", old_path, new_path, strerror(errno));
        return false;
    }
#endif // _WIN32
    return true;
}

Nob_Cmd nob_cmd_inline_null(void *first, ...)
{
    Nob_Cmd cmd = {0};

    va_list args;
    va_start(args, first);

    const char *arg = va_arg(args, const char*);
    while (arg != NULL) {
        nob_da_append(&cmd, arg);
        arg = va_arg(args, const char*);
    }

    va_end(args);

    return cmd;
}

// minirent.h SOURCE BEGIN ////////////////////////////////////////
#ifdef _WIN32
struct DIR
{
    HANDLE hFind;
    WIN32_FIND_DATA data;
    struct dirent *dirent;
};

DIR *opendir(const char *dirpath)
{
    assert(dirpath);

    char buffer[MAX_PATH];
    snprintf(buffer, MAX_PATH, "%s\\*", dirpath);

    DIR *dir = (DIR*)calloc(1, sizeof(DIR));

    dir->hFind = FindFirstFile(buffer, &dir->data);
    if (dir->hFind == INVALID_HANDLE_VALUE) {
        // TODO: opendir should set errno accordingly on FindFirstFile fail
        // https://docs.microsoft.com/en-us/windows/win32/api/errhandlingapi/nf-errhandlingapi-getlasterror
        errno = ENOSYS;
        goto fail;
    }

    return dir;

fail:
    if (dir) {
        free(dir);
    }

    return NULL;
}

struct dirent *readdir(DIR *dirp)
{
    assert(dirp);

    if (dirp->dirent == NULL) {
        dirp->dirent = (struct dirent*)calloc(1, sizeof(struct dirent));
    } else {
        if(!FindNextFile(dirp->hFind, &dirp->data)) {
            if (GetLastError() != ERROR_NO_MORE_FILES) {
                // TODO: readdir should set errno accordingly on FindNextFile fail
                // https://docs.microsoft.com/en-us/windows/win32/api/errhandlingapi/nf-errhandlingapi-getlasterror
                errno = ENOSYS;
            }

            return NULL;
        }
    }

    memset(dirp->dirent->d_name, 0, sizeof(dirp->dirent->d_name));

    strncpy(
        dirp->dirent->d_name,
        dirp->data.cFileName,
        sizeof(dirp->dirent->d_name) - 1);

    return dirp->dirent;
}

int closedir(DIR *dirp)
{
    assert(dirp);

    if(!FindClose(dirp->hFind)) {
        // TODO: closedir should set errno accordingly on FindClose fail
        // https://docs.microsoft.com/en-us/windows/win32/api/errhandlingapi/nf-errhandlingapi-getlasterror
        errno = ENOSYS;
        return -1;
    }

    if (dirp->dirent) {
        free(dirp->dirent);
    }
    free(dirp);

    return 0;
}
#endif // _WIN32
// minirent.h SOURCE END ////////////////////////////////////////

#endif
