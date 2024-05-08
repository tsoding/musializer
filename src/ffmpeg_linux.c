#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <raylib.h>

#include "ffmpeg.h"

#define READ_END 0
#define WRITE_END 1

struct FFMPEG {
    int pipe;
    pid_t pid;
};

FFMPEG *ffmpeg_start_rendering(size_t width, size_t height, size_t fps, const char *sound_file_path)
{
    int pipefd[2];

    if (pipe(pipefd) < 0) {
        TraceLog(LOG_ERROR, "FFMPEG: Could not create a pipe: %s", strerror(errno));
        return NULL;
    }

    pid_t child = fork();
    if (child < 0) {
        TraceLog(LOG_ERROR, "FFMPEG: could not fork a child: %s", strerror(errno));
        return NULL;
    }

    if (child == 0) {
        if (dup2(pipefd[READ_END], STDIN_FILENO) < 0) {
            TraceLog(LOG_ERROR, "FFMPEG CHILD: could not reopen read end of pipe as stdin: %s", strerror(errno));
            exit(1);
        }
        close(pipefd[WRITE_END]);

        char resolution[64];
        snprintf(resolution, sizeof(resolution), "%zux%zu", width, height);
        char framerate[64];
        snprintf(framerate, sizeof(framerate), "%zu", fps);

        int ret = execlp("ffmpeg",
            "ffmpeg",
            "-loglevel", "verbose",
            "-y",

            "-f", "rawvideo",
            "-pix_fmt", "rgba",
            "-s", resolution,
            "-r", framerate,
            "-i", "-",
            "-i", sound_file_path,

            "-c:v", "libx264",
            "-vb", "2500k",
            "-c:a", "aac",
            "-ab", "200k",
            "-pix_fmt", "yuv420p",
            "output.mp4",

            NULL
        );
        if (ret < 0) {
            TraceLog(LOG_ERROR, "FFMPEG CHILD: could not run ffmpeg as a child process: %s", strerror(errno));
            exit(1);
        }
        assert(0 && "unreachable");
        exit(1);
    }

    if (close(pipefd[READ_END]) < 0) {
        TraceLog(LOG_WARNING, "FFMPEG: could not close read end of the pipe on the parent's end: %s", strerror(errno));
    }

    FFMPEG *ffmpeg = malloc(sizeof(FFMPEG));
    assert(ffmpeg != NULL && "Buy MORE RAM lol!!");
    ffmpeg->pid = child;
    ffmpeg->pipe = pipefd[WRITE_END];
    return ffmpeg;
}

bool ffmpeg_end_rendering(FFMPEG *ffmpeg, bool cancel)
{
    int pipe = ffmpeg->pipe;
    pid_t pid = ffmpeg->pid;

    free(ffmpeg);

    if (close(pipe) < 0) {
        TraceLog(LOG_WARNING, "FFMPEG: could not close write end of the pipe on the parent's end: %s", strerror(errno));
    }

    if (cancel) kill(pid, SIGKILL);

    for (;;) {
        int wstatus = 0;
        if (waitpid(pid, &wstatus, 0) < 0) {
            TraceLog(LOG_ERROR, "FFMPEG: could not wait for ffmpeg child process to finish: %s", strerror(errno));
            return false;
        }

        if (WIFEXITED(wstatus)) {
            int exit_status = WEXITSTATUS(wstatus);
            if (exit_status != 0) {
                TraceLog(LOG_ERROR, "FFMPEG: ffmpeg exited with code %d", exit_status);
                return false;
            }

            return true;
        }

        if (WIFSIGNALED(wstatus)) {
            TraceLog(LOG_ERROR, "FFMPEG: ffmpeg got terminated by %s", strsignal(WTERMSIG(wstatus)));
            return false;
        }
    }

    assert(0 && "unreachable");
}

bool ffmpeg_send_frame_flipped(FFMPEG *ffmpeg, void *data, size_t width, size_t height)
{
    for (size_t y = height; y > 0; --y) {
        // TODO: write() may not necessarily write the entire row. We may want to repeat the call.
        if (write(ffmpeg->pipe, (uint32_t*)data + (y - 1)*width, sizeof(uint32_t)*width) < 0) {
            TraceLog(LOG_ERROR, "FFMPEG: failed to write into ffmpeg pipe: %s", strerror(errno));
            return false;
        }
    }
    return true;
}
