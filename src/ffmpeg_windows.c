#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#define WIN32_LEAN_AND_MEAN
#define _WINUSER_
#define _WINGDI_
#define _IMM_
#define _WINCON_
#include <windows.h>

#include <raylib.h>

#include "ffmpeg.h"
#include "win32_utf8.h"

struct FFMPEG {
    HANDLE hProcess;
    HANDLE hPipeWrite;
};

FFMPEG *ffmpeg_start_rendering(const char *output_path, size_t width, size_t height, size_t fps, const char *sound_file_path)
{
    HANDLE pipe_read = INVALID_HANDLE_VALUE;
    HANDLE pipe_write = INVALID_HANDLE_VALUE;
    HANDLE null_output = INVALID_HANDLE_VALUE;

    SECURITY_ATTRIBUTES saAttr = {0};
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;

    if (!CreatePipe(&pipe_read, &pipe_write, &saAttr, 0)) {
        TraceLog(LOG_ERROR, "FFMPEG: Could not create pipe. System Error Code: %d", GetLastError());
        return NULL;
    }

    if (!SetHandleInformation(pipe_write, HANDLE_FLAG_INHERIT, 0)) {
        TraceLog(LOG_ERROR, "FFMPEG: Could not mark write pipe as non-inheritable. System Error Code: %d", GetLastError());
        CloseHandle(pipe_write);
        CloseHandle(pipe_read);
        return NULL;
    }

    // https://docs.microsoft.com/en-us/windows/win32/procthread/creating-a-child-process-with-redirected-input-and-output

    STARTUPINFOW siStartInfo;
    ZeroMemory(&siStartInfo, sizeof(siStartInfo));
    siStartInfo.cb = sizeof(siStartInfo);
    siStartInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    siStartInfo.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    if (siStartInfo.hStdError == NULL || siStartInfo.hStdError == INVALID_HANDLE_VALUE ||
        siStartInfo.hStdOutput == NULL || siStartInfo.hStdOutput == INVALID_HANDLE_VALUE) {
        null_output = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  &saAttr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (null_output == INVALID_HANDLE_VALUE) {
            TraceLog(LOG_ERROR, "FFMPEG: Could not open NUL for child output. System Error Code: %d", GetLastError());
            CloseHandle(pipe_write);
            CloseHandle(pipe_read);
            return NULL;
        }
        siStartInfo.hStdError = null_output;
        siStartInfo.hStdOutput = null_output;
    }
    siStartInfo.hStdInput = pipe_read;
    siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

    PROCESS_INFORMATION piProcInfo;
    ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));

    char resolution[64];
    char framerate[64];
    snprintf(resolution, sizeof(resolution), "%zux%zu", width, height);
    snprintf(framerate, sizeof(framerate), "%zu", fps);
    const char *const argv[] = {
        "ffmpeg.exe",
        "-loglevel", "verbose", "-y",
        "-f", "rawvideo", "-pix_fmt", "rgba",
        "-s", resolution, "-r", framerate, "-i", "-",
        "-i", sound_file_path,
        "-c:v", "libx264", "-vb", "2500k",
        "-c:a", "aac", "-ab", "200k",
        "-pix_fmt", "yuv420p", output_path,
        NULL,
    };
    wchar_t *command_line = win32_command_line_from_utf8_argv(argv);
    if (command_line == NULL) {
        TraceLog(LOG_ERROR, "FFMPEG: Could not construct the child process command line");
        if (null_output != INVALID_HANDLE_VALUE) CloseHandle(null_output);
        CloseHandle(pipe_write);
        CloseHandle(pipe_read);
        return NULL;
    }

    if (!CreateProcessW(NULL, command_line, NULL, NULL, TRUE, 0, NULL, NULL, &siStartInfo, &piProcInfo)) {
        TraceLog(LOG_ERROR, "FFMPEG: Could not create child process. System Error Code: %d", GetLastError());

        free(command_line);
        if (null_output != INVALID_HANDLE_VALUE) CloseHandle(null_output);
        CloseHandle(pipe_write);
        CloseHandle(pipe_read);

        return NULL;
    }

    free(command_line);
    if (null_output != INVALID_HANDLE_VALUE) CloseHandle(null_output);
    CloseHandle(pipe_read);
    CloseHandle(piProcInfo.hThread);

    FFMPEG *ffmpeg = malloc(sizeof(FFMPEG));
    assert(ffmpeg != NULL && "Buy MORE RAM lol!!");
    ffmpeg->hProcess = piProcInfo.hProcess;
    ffmpeg->hPipeWrite = pipe_write;
    return ffmpeg;
}

bool ffmpeg_send_frame_flipped(FFMPEG *ffmpeg, void *data, size_t width, size_t height)
{
    if (width > MAXDWORD/sizeof(uint32_t)) return false;
    DWORD row_size = (DWORD)(sizeof(uint32_t)*width);

    for (size_t y = height; y > 0; --y) {
        const unsigned char *row = (const unsigned char *)data + (y - 1)*row_size;
        DWORD remaining = row_size;
        while (remaining > 0) {
            DWORD written = 0;
            if (!WriteFile(ffmpeg->hPipeWrite, row, remaining, &written, NULL) || written == 0) {
                TraceLog(LOG_ERROR, "FFMPEG: failed to write into ffmpeg pipe. System Error Code: %d", GetLastError());
                return false;
            }
            row += written;
            remaining -= written;
        }
    }
    return true;
}

bool ffmpeg_end_rendering(FFMPEG *ffmpeg, bool cancel)
{
    HANDLE hPipeWrite = ffmpeg->hPipeWrite;
    HANDLE hProcess = ffmpeg->hProcess;
    free(ffmpeg);

    FlushFileBuffers(hPipeWrite);
    CloseHandle(hPipeWrite);

    if (cancel) TerminateProcess(hProcess, 69);

    if (WaitForSingleObject(hProcess, INFINITE) == WAIT_FAILED) {
        TraceLog(LOG_ERROR, "FFMPEG: could not wait on child process. System Error Code: %d", GetLastError());
        CloseHandle(hProcess);
        return false;
    }

    DWORD exit_status;
    if (GetExitCodeProcess(hProcess, &exit_status) == 0) {
        TraceLog(LOG_ERROR, "FFMPEG: could not get process exit code. System Error Code: %d", GetLastError());
        CloseHandle(hProcess);
        return false;
    }

    if (exit_status != 0) {
        TraceLog(LOG_ERROR, "FFMPEG: command exited with exit code %lu", exit_status);
        CloseHandle(hProcess);
        return false;
    }

    CloseHandle(hProcess);

    return true;
}

// TODO: where can we find this symbol for the Windows build?
void __imp__wassert() {}
