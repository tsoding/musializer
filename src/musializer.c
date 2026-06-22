#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <complex.h>

#include <raylib.h>

#ifndef _WIN32
#include <signal.h> // needed for sigaction()
#endif // _WIN32

#include "./hotreload.h"
#include "./thirdparty/nob.h"

int main(int argc, char **argv)
{
#ifndef _WIN32
    // NOTE: This is needed because if the pipe between Musializer and FFmpeg breaks
    // Musializer will receive SIGPIPE on trying to write into it. While such behavior
    // makes sense for command line utilities, Musializer is a relatively friendly GUI
    // application that is trying to recover from such situations.
    struct sigaction act = {0};
    act.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &act, NULL);
#endif // _WIN32

    if (!reload_libplug()) return 1;
    nob_shift(argv, argc);
    bool isCLI = argc > 0;
    char *input_path = NULL;
    char *render_path = NULL;
    bool cli_rendering = false;
    Nob_File_Paths input_paths = {0};
    if (isCLI)
    {
        const char *command_name = nob_shift(argv, argc);
        if (strcmp(command_name, "--render") == 0 || strcmp(command_name, "-r") == 0)
        {
            cli_rendering = true;
            input_path = argc > 0 ? nob_shift(argv, argc) : NULL;
            if (input_path == NULL)
            {
                nob_log(NOB_ERROR, "Error: No music file provided.");
                return 1;
            }
            render_path = argc > 0 ? nob_shift(argv, argc) : NULL;
            if (render_path == NULL)
            {
                char buffer[512];
                snprintf(buffer, sizeof(buffer), "%s", input_path);
                char *dot = strrchr(buffer, '.');
                if (dot != NULL)
                {
                    strcpy(dot, ".mp4");
                }
                else
                {
                    strcat(buffer, ".mp4");
                }
                render_path = strdup(buffer);
            }
            nob_log(NOB_INFO, "Music path: %s", input_path);
            nob_log(NOB_INFO, "Render path: %s", render_path);
        }
        else
        {
            nob_da_append(&input_paths, command_name);
            while (argc > 0)
            {
                input_path = nob_shift(argv, argc);
                nob_da_append(&input_paths, input_path);
            }
        }
    }
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_ALWAYS_RUN);
    size_t factor = 80;
    InitWindow(factor*16, factor*9, "Musializer");
    {
        const char *file_path = "./resources/logo/logo-256.png";
        size_t data_size;
        void *data = plug_load_resource(file_path, &data_size);
        Image logo = LoadImageFromMemory(GetFileExtension(file_path), data, data_size);
        SetWindowIcon(logo);
        plug_free_resource(data);
    }
    SetExitKey(KEY_NULL);
    InitAudioDevice();

    plug_init(&input_paths);
    if (cli_rendering) {
        plug_start_cli_rendering_track(input_path, render_path);
    } 
    else {
        while (!WindowShouldClose()) {
            if (IsKeyPressed(KEY_H)) {
                void *state = plug_pre_reload();
                if (!reload_libplug()) return 1;
                plug_post_reload(state);
            }
            plug_update();
        }
    }

    CloseAudioDevice();
    CloseWindow();

    return 0;
}
