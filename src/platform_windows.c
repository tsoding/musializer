#include <stdbool.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "platform.h"
#include "win32_utf8.h"

struct Platform_Mutex {
    CRITICAL_SECTION value;
};

struct Platform_Condition {
    CONDITION_VARIABLE value;
};

struct Platform_Thread {
    HANDLE handle;
    Platform_Thread_Function function;
    void *arg;
};

Platform_Mutex *platform_mutex_create(void)
{
    Platform_Mutex *mutex = malloc(sizeof(*mutex));
    if (mutex == NULL) return NULL;
    InitializeCriticalSection(&mutex->value);
    return mutex;
}

void platform_mutex_destroy(Platform_Mutex *mutex)
{
    if (mutex == NULL) return;
    DeleteCriticalSection(&mutex->value);
    free(mutex);
}

void platform_mutex_lock(Platform_Mutex *mutex)
{
    EnterCriticalSection(&mutex->value);
}

bool platform_mutex_try_lock(Platform_Mutex *mutex)
{
    return TryEnterCriticalSection(&mutex->value) != 0;
}

void platform_mutex_unlock(Platform_Mutex *mutex)
{
    LeaveCriticalSection(&mutex->value);
}

Platform_Condition *platform_condition_create(void)
{
    Platform_Condition *condition = malloc(sizeof(*condition));
    if (condition == NULL) return NULL;
    InitializeConditionVariable(&condition->value);
    return condition;
}

void platform_condition_destroy(Platform_Condition *condition)
{
    free(condition);
}

void platform_condition_wait(Platform_Condition *condition, Platform_Mutex *mutex)
{
    SleepConditionVariableCS(&condition->value, &mutex->value, INFINITE);
}

void platform_condition_signal(Platform_Condition *condition)
{
    WakeConditionVariable(&condition->value);
}

void platform_condition_broadcast(Platform_Condition *condition)
{
    WakeAllConditionVariable(&condition->value);
}

static unsigned __stdcall platform_thread_entry(void *data)
{
    Platform_Thread *thread = data;
    thread->function(thread->arg);
    return 0;
}

Platform_Thread *platform_thread_start(Platform_Thread_Function function, void *arg)
{
    if (function == NULL) return NULL;
    Platform_Thread *thread = malloc(sizeof(*thread));
    if (thread == NULL) return NULL;
    thread->function = function;
    thread->arg = arg;
    thread->handle = (HANDLE)_beginthreadex(NULL, 0, platform_thread_entry, thread, 0, NULL);
    if (thread->handle == NULL) {
        free(thread);
        return NULL;
    }
    return thread;
}

void platform_thread_join(Platform_Thread *thread)
{
    if (thread == NULL) return;
    WaitForSingleObject(thread->handle, INFINITE);
    CloseHandle(thread->handle);
    free(thread);
}

bool platform_make_temp_file(char *path, size_t path_capacity, const char *prefix, const char *suffix)
{
    if (path == NULL || path_capacity == 0) return false;
    path[0] = '\0';
    if (suffix == NULL) suffix = "";

    wchar_t temp_directory[MAX_PATH + 1];
    DWORD directory_length = GetTempPathW(MAX_PATH + 1, temp_directory);
    if (directory_length == 0 || directory_length > MAX_PATH) return false;

    wchar_t wide_prefix[4] = L"mus";
    if (prefix != NULL && prefix[0] != '\0') {
        wchar_t *converted_prefix = win32_utf8_to_utf16(prefix);
        if (converted_prefix != NULL) {
            size_t i = 0;
            for (; i < 3 && converted_prefix[i] != L'\0'; ++i) wide_prefix[i] = converted_prefix[i];
            wide_prefix[i] = L'\0';
            free(converted_prefix);
        }
    }

    wchar_t base_path[MAX_PATH + 1];
    if (GetTempFileNameW(temp_directory, wide_prefix, 0, base_path) == 0) return false;

    wchar_t *wide_suffix = win32_utf8_to_utf16(suffix);
    if (wide_suffix == NULL) {
        DeleteFileW(base_path);
        return false;
    }

    wchar_t final_path[MAX_PATH + 1];
    int length = swprintf(final_path, MAX_PATH + 1, L"%ls%ls", base_path, wide_suffix);
    free(wide_suffix);
    if (length < 0 || length > MAX_PATH) {
        DeleteFileW(base_path);
        return false;
    }

    if (suffix[0] != '\0') {
        if (!MoveFileW(base_path, final_path)) {
            DeleteFileW(base_path);
            return false;
        }
    }

    if (!win32_utf16_to_utf8(final_path, path, path_capacity)) {
        DeleteFileW(final_path);
        path[0] = '\0';
        return false;
    }
    return true;
}

bool platform_remove_file(const char *path)
{
    if (path == NULL || path[0] == '\0') return true;
    wchar_t *wide_path = win32_utf8_to_utf16(path);
    if (wide_path == NULL) return false;
    bool result = DeleteFileW(wide_path) != 0 || GetLastError() == ERROR_FILE_NOT_FOUND;
    free(wide_path);
    return result;
}

FILE *platform_fopen(const char *path, const char *mode)
{
    wchar_t *wide_path = win32_utf8_to_utf16(path);
    wchar_t *wide_mode = win32_utf8_to_utf16(mode);
    if (wide_path == NULL || wide_mode == NULL) {
        free(wide_path);
        free(wide_mode);
        return NULL;
    }
    FILE *file = _wfopen(wide_path, wide_mode);
    free(wide_path);
    free(wide_mode);
    return file;
}

bool platform_read_entire_file(const char *path, unsigned char **data, size_t *size)
{
    if (data == NULL || size == NULL) return false;
    *data = NULL;
    *size = 0;

    FILE *file = platform_fopen(path, "rb");
    if (file == NULL) return false;
    if (_fseeki64(file, 0, SEEK_END) != 0) goto fail;
    __int64 file_size = _ftelli64(file);
    if (file_size < 0 || (uint64_t)file_size > SIZE_MAX || _fseeki64(file, 0, SEEK_SET) != 0) goto fail;

    unsigned char *contents = malloc(file_size > 0 ? (size_t)file_size : 1);
    if (contents == NULL) goto fail;
    if (file_size > 0 && fread(contents, 1, (size_t)file_size, file) != (size_t)file_size) {
        free(contents);
        goto fail;
    }

    fclose(file);
    *data = contents;
    *size = (size_t)file_size;
    return true;

fail:
    fclose(file);
    return false;
}

bool platform_run_command(const char *const argv[], bool quiet)
{
    wchar_t *command_line = win32_command_line_from_utf8_argv(argv);
    if (command_line == NULL) return false;

    SECURITY_ATTRIBUTES security = {0};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;

    HANDLE null_input = INVALID_HANDLE_VALUE;
    HANDLE null_output = INVALID_HANDLE_VALUE;
    STARTUPINFOW startup = {0};
    startup.cb = sizeof(startup);
    BOOL inherit_handles = FALSE;
    if (quiet) {
        null_input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        null_output = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (null_input == INVALID_HANDLE_VALUE || null_output == INVALID_HANDLE_VALUE) goto fail;
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = null_input;
        startup.hStdOutput = null_output;
        startup.hStdError = null_output;
        inherit_handles = TRUE;
    }

    PROCESS_INFORMATION process = {0};
    if (!CreateProcessW(NULL, command_line, NULL, NULL, inherit_handles, CREATE_NO_WINDOW,
                        NULL, NULL, &startup, &process)) goto fail;

    CloseHandle(process.hThread);
    DWORD wait_result = WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exit_code = 1;
    bool result = wait_result == WAIT_OBJECT_0 &&
                  GetExitCodeProcess(process.hProcess, &exit_code) && exit_code == 0;
    CloseHandle(process.hProcess);
    if (null_input != INVALID_HANDLE_VALUE) CloseHandle(null_input);
    if (null_output != INVALID_HANDLE_VALUE) CloseHandle(null_output);
    free(command_line);
    return result;

fail:
    if (null_input != INVALID_HANDLE_VALUE) CloseHandle(null_input);
    if (null_output != INVALID_HANDLE_VALUE) CloseHandle(null_output);
    free(command_line);
    return false;
}
