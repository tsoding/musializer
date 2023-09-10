#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>

#define NOB_IMPLEMENTATION
#include "nob.h"

typedef enum {
    TARGET_POSIX,
    TARGET_WIN32,
    COUNT_TARGETS
} Target;

const char *target_names[] = {
    [TARGET_POSIX]     = "posix",
    [TARGET_WIN32]     = "win32",
};
static_assert(2 == COUNT_TARGETS, "Amount of targets have changed");

void cc(Nob_Cmd *cmd, Target target)
{
    switch (target) {
        case TARGET_POSIX:
            nob_cmd_append(cmd, "clang");
            break;
        case TARGET_WIN32:
            nob_cmd_append(cmd, "x86_64-w64-mingw32-gcc");
            break;
        default: NOB_ASSERT(0 && "unreachable");
    }
}

void common_cflags(Nob_Cmd *cmd, Target target)
{
    switch (target) {
        case TARGET_POSIX:
        case TARGET_WIN32:
            nob_cmd_append(cmd, "-Wall", "-Wextra", "-ggdb");
            break;
        default: NOB_ASSERT(0 && "unreachable");
    }
}

void raylib_cflags(Nob_Cmd *cmd, Target target)
{
    switch (target) {
        case TARGET_POSIX:
            nob_cmd_append(cmd, "-I./raylib/raylib-4.5.0_linux_amd64/include/");
            break;
        case TARGET_WIN32:
            nob_cmd_append(cmd, "-I./raylib/raylib-4.5.0_win64_mingw-w64/include");
            break;
        default: NOB_ASSERT(0 && "unreachable");
    }
}

void full_musializer_source(Nob_Cmd *cmd, Target target)
{
    nob_cmd_append(cmd, "./src/musializer.c");
    nob_cmd_append(cmd, "./src/plug.c");
    nob_cmd_append(cmd, "./src/separate_translation_unit_for_miniaudio.c");
    switch (target) {
        case TARGET_POSIX:
            nob_cmd_append(cmd, "./src/ffmpeg_linux.c");
            break;
        case TARGET_WIN32:
            nob_cmd_append(cmd, "./src/ffmpeg_windows.c");
            break;
        default: NOB_ASSERT(0 && "unreachable");
    }
}

void hotreloaded_musializer_source(Nob_Cmd *cmd, Target target)
{
    nob_cmd_append(cmd, "./src/musializer.c");
    switch (target) {
        case TARGET_POSIX:
            nob_cmd_append(cmd, "./src/hotreload_linux.c");
            break;
        case TARGET_WIN32:
            NOB_ASSERT(0 && "Unreachable. Hotreloading on Windows is not supported yet");
            break;
        default: NOB_ASSERT(0 && "unreachable");
    }
}

void plug_dll_source(Nob_Cmd *cmd, Target target)
{
    nob_cmd_append(cmd, "./src/plug.c");
    nob_cmd_append(cmd, "./src/separate_translation_unit_for_miniaudio.c");
    switch (target) {
        case TARGET_POSIX:
            nob_cmd_append(cmd, "./src/ffmpeg_linux.c");
            break;
        case TARGET_WIN32:
            nob_cmd_append(cmd, "./src/ffmpeg_windows.c");
            break;
        default: NOB_ASSERT(0 && "unreachable");
    }
}

void link_libraries_static(Nob_Cmd *cmd, Target target)
{
    switch (target) {
        case TARGET_POSIX:
            nob_cmd_append(cmd, "-L./raylib/raylib-4.5.0_linux_amd64/lib/", "-l:libraylib.a");
            nob_cmd_append(cmd, "-lm", "-ldl", "-lpthread");
            break;
        case TARGET_WIN32:
            nob_cmd_append(cmd, "-L./raylib/raylib-4.5.0_win64_mingw-w64/lib/", "-lraylib");
            nob_cmd_append(cmd, "-lwinmm", "-lgdi32", "-static");
            break;
        default: NOB_ASSERT(0 && "unreachable");
    }
}

void link_libraries_dynamic(Nob_Cmd *cmd, Target target)
{
    switch (target) {
        case TARGET_POSIX:
            nob_cmd_append(cmd, "-L./raylib/raylib-4.5.0_linux_amd64/lib/", "-l:libraylib.so");
            nob_cmd_append(cmd, "-lm", "-ldl", "-lpthread");
            break;
        case TARGET_WIN32:
            NOB_ASSERT(0 && "Unreachable. Hotreloading on Windows is not supported yet");
            break;
        default: NOB_ASSERT(0 && "unreachable");
    }
}

bool build_musializer_executable(const char *output_path, Target target, bool hotreload)
{
    bool result = true;
    Nob_Cmd cmd = {0};

    switch (target) {
        case TARGET_POSIX: {
            if (hotreload) {
                cc(&cmd, target);
                common_cflags(&cmd, target);
                raylib_cflags(&cmd, target);
                nob_cmd_append(&cmd, "-fPIC", "-shared");
                nob_cmd_append(&cmd, "-o", "./build/libplug.so");
                plug_dll_source(&cmd, target);
                link_libraries_dynamic(&cmd, target);
                if (!nob_cmd_run_sync(cmd)) nob_return_defer(false);

                cmd.count = 0;

                cc(&cmd, target);
                common_cflags(&cmd, target);
                raylib_cflags(&cmd, target);
                nob_cmd_append(&cmd, "-DHOTRELOAD");
                nob_cmd_append(&cmd, "-o", "./build/musializer");
                hotreloaded_musializer_source(&cmd, target);
                link_libraries_dynamic(&cmd, target);
                if (!nob_cmd_run_sync(cmd)) nob_return_defer(false);
            } else {
                cc(&cmd, target);
                common_cflags(&cmd, target);
                raylib_cflags(&cmd, target);
                nob_cmd_append(&cmd, "-o", "./build/musializer");
                full_musializer_source(&cmd, target);
                link_libraries_static(&cmd, target);
                if (!nob_cmd_run_sync(cmd)) nob_return_defer(false);
            }
        } break;

        case TARGET_WIN32: {
            if (hotreload) {
                nob_log(NOB_ERROR, "TODO: hotreloading is not supported on %s yet", NOB_ARRAY_GET(target_names, target));
                return false;
            }

            cc(&cmd, target);
            common_cflags(&cmd, target);
            raylib_cflags(&cmd, target);
            nob_cmd_append(&cmd, "-o", "./build/musializer");
            full_musializer_source(&cmd, target);
            link_libraries_static(&cmd, target);
            if (!nob_cmd_run_sync(cmd)) nob_return_defer(false);
        } break;

        default: NOB_ASSERT(0 && "unreachable");
    }

defer:
    nob_cmd_free(cmd);
    return result;
}

void log_available_targets(Nob_Log_Level level)
{
    nob_log(level, "Available targets:");
    for (size_t i = 0; i < COUNT_TARGETS; ++i) {
        nob_log(level, "    %s", target_names[i]);
    }
}

int main(int argc, char **argv)
{
    NOB_GO_REBUILD_URSELF(argc, argv);

    const char *program = nob_shift_args(&argc, &argv);

    if (argc <= 0) {
        nob_log(NOB_ERROR, "No subcommand is provided");
        nob_log(NOB_ERROR, "Usage: %s <subcommand>", program);
        nob_log(NOB_ERROR, "Subcommands:");
        nob_log(NOB_ERROR, "    build");
        nob_log(NOB_ERROR, "    logo");
        return 1;
    }

    const char *subcommand = nob_shift_args(&argc, &argv);

    if (strcmp(subcommand, "build") == 0) {
#ifdef _WIN32
        Target target = TARGET_WIN32;
#else
        Target target = TARGET_POSIX;
#endif
        bool hotreload = false;

        while (argc > 0) {
            const char *flag = nob_shift_args(&argc, &argv);
            if (strcmp(flag, "-t") == 0) {
                if (argc <= 0) {
                    nob_log(NOB_ERROR, "No value is provided for flag %s", flag);
                    log_available_targets(NOB_ERROR);
                    return 1;
                }

                const char *value = nob_shift_args(&argc, &argv);

                bool found = false;
                for (size_t i = 0; !found && i < COUNT_TARGETS; ++i) {
                    if (strcmp(target_names[i], value) == 0) {
                        target = i;
                        found = true;
                    }
                }

                if (!found) {
                    nob_log(NOB_ERROR, "Unknown target %s", value);
                    log_available_targets(NOB_ERROR);
                    return 1;
                }
            } else if (strcmp("-h", flag) == 0) {
                hotreload = true;
            } else {
                nob_log(NOB_ERROR, "Unknown flag %s", flag);
                return 1;
            }
        }

        nob_log(NOB_INFO, "------------------------------");
        nob_log(NOB_INFO, "Target: %s", NOB_ARRAY_GET(target_names, target));
        nob_log(NOB_INFO, "Hotreload: %s", hotreload ? "ENABLED" : "DISABLED");
        nob_log(NOB_INFO, "------------------------------");
        if (!nob_mkdir_if_not_exists("build")) return 1;
        if (!build_musializer_executable("./build/musializer", target, hotreload)) return 1;
        if (target == TARGET_WIN32) {
            if (!nob_copy_file("musializer-logged.bat", "build/musializer-logged.bat")) return 1;
        }
        if (!nob_copy_directory_recursively("./resources/", "./build/resources/")) return 1;
    } else if (strcmp(subcommand, "logo") == 0) {
        Nob_Cmd cmd = {0};
        nob_cmd_append(&cmd, "convert");
        nob_cmd_append(&cmd, "-background", "None");
        nob_cmd_append(&cmd, "./resources/logo/logo.svg");
        nob_cmd_append(&cmd, "-resize", "256");

        nob_cmd_append(&cmd, "./resources/logo/logo-256.ico");
        if (!nob_cmd_run_sync(cmd)) return 1;

        cmd.count -= 1;

        nob_cmd_append(&cmd, "./resources/logo/logo-256.png");
        if (!nob_cmd_run_sync(cmd)) return 1;
    }
    return 0;
}
