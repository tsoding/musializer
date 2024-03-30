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
#include <ctype.h>
#define READ_END 0
#define WRITE_END 1
#define PATH_MAX 1024

typedef struct {
    int pipe;
    pid_t pid;
} FFMPEG;

char *get_file_name_as_mp4(const char *sound_file_path) {

    // NOTE: This function generates output names that can be used by ffmpeg tool.
    // At least the spaces and punctuations are removed from the path.. 
    // example:
    // input  -> 'hello)!world.mp3' 
    // output -> 'helloworld.mp3'
    
    size_t path_tail_index = 0;
    char *output_mp4  = NULL; 
    char *extension = ".mp4";

    if (!sound_file_path)
        return strdup("output.mp4");

    output_mp4 = malloc(PATH_MAX);
    path_tail_index = strlen(sound_file_path) - 1;
    
    // Consume the extension.
    while (true) {
        path_tail_index--;
        if (path_tail_index == 0 || sound_file_path[path_tail_index + 1] == '.') break;
    }

    // Populate the output_mp4 for ffmpeg
    strcat(output_mp4 + PATH_MAX - strlen(extension), extension);
    size_t i = PATH_MAX - strlen(extension) - 1;

    while (sound_file_path[path_tail_index] != '/') {
 
        // Skip the spaces since ffmpeg does not support output paths that include spaces.
        while (isspace(sound_file_path[path_tail_index]) || ispunct(sound_file_path[path_tail_index])) 
        {
            if (path_tail_index == 0)
                break;
            path_tail_index--;
        }
        
        // We reached the first character and it is an invalid ffmpeg path character.
        if (isspace(sound_file_path[path_tail_index]) || ispunct(sound_file_path[path_tail_index])) break;
        
        output_mp4[i] = sound_file_path[path_tail_index];    

        // Consume the current character
        path_tail_index--;
        if (path_tail_index == 0 || i == 0) break;
        if (sound_file_path[path_tail_index] == '/') break;
        i--;
    }

    // Move the path into the first columns of the buffer so free does not yell att us when we try freeing.
    memmove(output_mp4, 
        output_mp4 + i,
        PATH_MAX   - i
    );

    return output_mp4;
}

FFMPEG *ffmpeg_start_rendering(size_t width, size_t height, size_t fps, const char *sound_file_path)
{
    int pipefd[2];
    // OutPut file Path.
    char *out = NULL;
    
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
        out = get_file_name_as_mp4(sound_file_path);
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
            out,
            NULL
        );
        if (ret < 0) {
            free(out);
            TraceLog(LOG_ERROR, "FFMPEG CHILD: could not run ffmpeg as a child process: %s", strerror(errno));
            exit(1);
        }
        
        assert(0 && "unreachable");
        free(out);
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

bool ffmpeg_end_rendering(FFMPEG *ffmpeg)
{
    int pipe = ffmpeg->pipe;
    pid_t pid = ffmpeg->pid;

    free(ffmpeg);

    if (close(pipe) < 0) {
        TraceLog(LOG_WARNING, "FFMPEG: could not close write end of the pipe on the parent's end: %s", strerror(errno));
    }

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


void _get_file_name_tests() {
    // TEST: get_file_name_as_mp4 tests.
    {
        char *out = get_file_name_as_mp4(NULL); 
        printf("%s\n", out); // should return output.mp4
        free(out);
    }
    
    {
        char *out = get_file_name_as_mp4("./hello.mp3"); 
        printf("%s\n", out); // should return hello.mp4
        free(out);
    }
    
    {
        char *out = get_file_name_as_mp4("C:/x/y/z/hello.mp3"); 
        printf("%s\n", out); // should return hello.mp4
        free(out);
    }

    {
        char *out = get_file_name_as_mp4("/home/usr0/x/y/z/hello.mp3"); 
        printf("%s\n", out); // should return hello.mp4
        free(out);
    }
 
    {
        char *out = get_file_name_as_mp4("/Some_vidoe01239238.mp3"); 
        printf("%s\n", out); // should return hello.mp4
        free(out);
    }
}
