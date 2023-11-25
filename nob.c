#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>

#define NOB_IMPLEMENTATION
#include "./src/nob.h"

#define CONFIG_PATH "./build/config.h"

#ifdef CONFIGURED

#include CONFIG_PATH

#define RAYLIB_VERSION "5.0"

#define TARGET_LINUX 0
#define TARGET_WIN64_MINGW 1
#define TARGET_WIN64_MSVC 2
#define TARGET_MACOS 3

static const char *raylib_modules[] = {
    "rcore",
    "raudio",
    "rglfw",
    "rmodels",
    "rshapes",
    "rtext",
    "rtextures",
    "utils",
};

#if MUSIALIZER_TARGET == TARGET_LINUX
#include "nob_linux.c"
#elif MUSIALIZER_TARGET == TARGET_MACOS
#include "nob_macos.c"
#elif MUSIALIZER_TARGET == TARGET_WIN64_MINGW
#include "nob_win64_mingw.c"
#elif MUSIALIZER_TARGET == TARGET_WIN64_MSVC
#include "nob_win64_msvc.c"
#endif // MUSIALIZER_TARGET

#if 0
bool build_raylib(Config config)
{
    bool result = true;
    Nob_Cmd cmd = {0};
    Nob_File_Paths object_files = {0};

    if (!nob_mkdir_if_not_exists("./build/raylib")) {
        nob_return_defer(false);
    }

    Nob_Procs procs = {0};

    const char *build_path = nob_temp_sprintf("./build/raylib/%s", NOB_ARRAY_GET(target_names, config.target));

    if (!nob_mkdir_if_not_exists(build_path)) {
        nob_return_defer(false);
    }

    for (size_t i = 0; i < NOB_ARRAY_LEN(raylib_modules); ++i) {
        const char *input_path = nob_temp_sprintf("./raylib/raylib-"RAYLIB_VERSION"/src/%s.c", raylib_modules[i]);
        const char *output_path = nob_temp_sprintf("%s/%s.o", build_path, raylib_modules[i]);
        switch (config.target) {
        case TARGET_LINUX:
        case TARGET_MACOS:
        case TARGET_WIN64_MINGW:
            output_path = nob_temp_sprintf("%s/%s.o", build_path, raylib_modules[i]);
            break;
        case TARGET_WIN64_MSVC:
            output_path = nob_temp_sprintf("%s/%s.obj", build_path, raylib_modules[i]);
            break;
        default: NOB_ASSERT(0 && "unreachable");
        }

        nob_da_append(&object_files, output_path);

        if (nob_needs_rebuild(output_path, &input_path, 1)) {
            cmd.count = 0;
            switch (config.target) {
                case TARGET_LINUX:
                    nob_cmd_append(&cmd, "cc");
                    nob_cmd_append(&cmd, "-ggdb", "-DPLATFORM_DESKTOP", "-fPIC");
                    nob_cmd_append(&cmd, "-I./raylib/raylib-"RAYLIB_VERSION"/src/external/glfw/include");
                    nob_cmd_append(&cmd, "-c", input_path);
                    nob_cmd_append(&cmd, "-o", output_path);
                    break;
                case TARGET_MACOS:
                    nob_cmd_append(&cmd, "clang");
                    nob_cmd_append(&cmd, "-g", "-DPLATFORM_DESKTOP", "-fPIC");
                    nob_cmd_append(&cmd, "-I./raylib/raylib-"RAYLIB_VERSION"/src/external/glfw/include");
                    nob_cmd_append(&cmd, "-Iexternal/glfw/deps/ming");
                    nob_cmd_append(&cmd, "-DGRAPHICS_API_OPENGL_33");
                    if(strcmp(raylib_modules[i], "rglfw") == 0) {
                        nob_cmd_append(&cmd, "-x", "objective-c");
                    }
                    nob_cmd_append(&cmd, "-c", input_path);
                    nob_cmd_append(&cmd, "-o", output_path);
                    break;
                case TARGET_WIN64_MINGW:
                    nob_cmd_append(&cmd, "x86_64-w64-mingw32-gcc");
                    nob_cmd_append(&cmd, "-ggdb", "-DPLATFORM_DESKTOP", "-fPIC");
                    nob_cmd_append(&cmd, "-DPLATFORM_DESKTOP");
                    nob_cmd_append(&cmd, "-fPIC");
                    nob_cmd_append(&cmd, "-I./raylib/raylib-"RAYLIB_VERSION"/src/external/glfw/include");
                    nob_cmd_append(&cmd, "-c", input_path);
                    nob_cmd_append(&cmd, "-o", output_path);
                    break;
                case TARGET_WIN64_MSVC:
                    nob_cmd_append(&cmd, "cl.exe", "/DPLATFORM_DESKTOP");
                    nob_cmd_append(&cmd, "/I", "./raylib/raylib-"RAYLIB_VERSION"/src/external/glfw/include");
                    nob_cmd_append(&cmd, "/c", input_path);
                    nob_cmd_append(&cmd, nob_temp_sprintf("/Fo%s", output_path));
                    break;
                default: NOB_ASSERT(0 && "unreachable");
            }

            Nob_Proc proc = nob_cmd_run_async(cmd);
            nob_da_append(&procs, proc);
        }
    }
    cmd.count = 0;

    if (!nob_procs_wait(procs)) nob_return_defer(false);

    switch (config.target) {
        case TARGET_MACOS:
        case TARGET_LINUX:
        case TARGET_WIN64_MINGW: {
            if (!config.hotreload) {
                const char *libraylib_path = nob_temp_sprintf("%s/libraylib.a", build_path);

                if (nob_needs_rebuild(libraylib_path, object_files.items, object_files.count)) {
                    nob_cmd_append(&cmd, "ar", "-crs", libraylib_path);
                    for (size_t i = 0; i < NOB_ARRAY_LEN(raylib_modules); ++i) {
                        const char *input_path = nob_temp_sprintf("%s/%s.o", build_path, raylib_modules[i]);
                        nob_cmd_append(&cmd, input_path);
                    }
                    if (!nob_cmd_run_sync(cmd)) nob_return_defer(false);
                }
            } else {
                const char *libraylib_path = nob_temp_sprintf("%s/libraylib.so", build_path);

                if (nob_needs_rebuild(libraylib_path, object_files.items, object_files.count)) {
                    if (config.target != TARGET_LINUX) {
                        nob_log(NOB_ERROR, "TODO: dynamic raylib for %s is not supported yet", NOB_ARRAY_GET(target_names, config.target));
                        nob_return_defer(false);
                    }
                    nob_cmd_append(&cmd, "cc");
                    nob_cmd_append(&cmd, "-shared");
                    nob_cmd_append(&cmd, "-o", libraylib_path);
                    for (size_t i = 0; i < NOB_ARRAY_LEN(raylib_modules); ++i) {
                        const char *input_path = nob_temp_sprintf("%s/%s.o", build_path, raylib_modules[i]);
                        nob_cmd_append(&cmd, input_path);
                    }
                    if (!nob_cmd_run_sync(cmd)) nob_return_defer(false);
                }
            }
        } break;

        case TARGET_WIN64_MSVC: {
            if (!config.hotreload) {
                const char *libraylib_path = nob_temp_sprintf("%s/raylib.lib", build_path);
                if (nob_needs_rebuild(libraylib_path, object_files.items, object_files.count)) {
                    nob_cmd_append(&cmd, "lib");
                    for (size_t i = 0; i < NOB_ARRAY_LEN(raylib_modules); ++i) {
                        const char *input_path = nob_temp_sprintf("%s/%s.obj", build_path, raylib_modules[i]);
                        nob_cmd_append(&cmd, input_path);
                    }
                    nob_cmd_append(&cmd, nob_temp_sprintf("/OUT:%s", libraylib_path));
                    if (!nob_cmd_run_sync(cmd)) nob_return_defer(false);
                }
            } else {
                nob_log(NOB_WARNING, "TODO: dynamic raylib for %s is not supported yet", NOB_ARRAY_GET(target_names, config.target));
                nob_return_defer(false);
            }
        } break;

        default: NOB_ASSERT(0 && "unreachable");
    }

defer:
    nob_cmd_free(cmd);
    nob_da_free(object_files);
    return result;
}

bool build_dist(Config config)
{
    if (config.hotreload) {
        nob_log(NOB_ERROR, "We do not ship with hotreload enabled");
        return false;
    }

    switch (config.target) {
        case TARGET_LINUX: {
            if (!nob_mkdir_if_not_exists("./musializer-linux-x86_64/")) return false;
            if (!nob_copy_file("./build/musializer", "./musializer-linux-x86_64/musializer")) return false;
            if (!nob_copy_directory_recursively("./resources/", "./musializer-linux-x86_64/resources/")) return false;
            // TODO: should we pack ffmpeg with Linux build?
            // There are some static executables for Linux
            Nob_Cmd cmd = {0};
            nob_cmd_append(&cmd, "tar", "fvc", "./musializer-linux-x86_64.tar.gz", "./musializer-linux-x86_64");
            bool ok = nob_cmd_run_sync(cmd);
            nob_cmd_free(cmd);
            if (!ok) return false;
        } break;

        case TARGET_WIN64_MINGW: {
            if (!nob_mkdir_if_not_exists("./musializer-win64-mingw/")) return false;
            if (!nob_copy_file("./build/musializer.exe", "./musializer-win64-mingw/musializer.exe")) return false;
            if (!nob_copy_directory_recursively("./resources/", "./musializer-win64-mingw/resources/")) return false;
            if (!nob_copy_file("musializer-logged.bat", "./musializer-win64-mingw/musializer-logged.bat")) return false;
            // TODO: pack ffmpeg.exe with windows build
            //if (!nob_copy_file("ffmpeg.exe", "./musializer-win64-mingw/ffmpeg.exe")) return false;
            Nob_Cmd cmd = {0};
            nob_cmd_append(&cmd, "zip", "-r", "./musializer-win64-mingw.zip", "./musializer-win64-mingw/");
            bool ok = nob_cmd_run_sync(cmd);
            nob_cmd_free(cmd);
            if (!ok) return false;
        } break;

        case TARGET_WIN64_MSVC: {
            nob_log(NOB_ERROR, "TODO: Creating distro for MSVC build is not implemented yet");
            return false;
        } break;

        case TARGET_MACOS: {
            nob_log(NOB_ERROR, "TODO: Creating distro for MacOS build is not implemented yet");
            return false;
        }

        default: NOB_ASSERT(0 && "unreachable");
    }

    return true;
}
#endif

void log_available_subcommands(const char *program, Nob_Log_Level level)
{
    nob_log(level, "Usage: %s [subcommand]", program);
    nob_log(level, "Subcommands:");
    nob_log(level, "    build (default)");
    nob_log(level, "    dist");
    nob_log(level, "    svg");
    nob_log(level, "    help");
}

int main(int argc, char **argv)
{
    nob_log(NOB_INFO, "--- STAGE 2 ---");
    nob_log(NOB_INFO, "Target: %s", MUSIALIZER_TARGET_NAME);
#ifdef MUSIALIZER_HOTRELOAD
    nob_log(NOB_INFO, "Hotreload: ENABLED");
#else
    nob_log(NOB_INFO, "Hotreload: DISABLED");
#endif // MUSIALIZER_HOTRELOAD
#ifdef MUSIALIZER_MICROPHONE
    nob_log(NOB_INFO, "Microphone: ENABLED");
#else
    nob_log(NOB_INFO, "Microphone: DISABLED");
#endif // MUSIALIZER_MICROPHONE
    nob_log(NOB_INFO, "---");

    const char *program = nob_shift_args(&argc, &argv);

    const char *subcommand = NULL;
    if (argc <= 0) {
        subcommand = "build";
    } else {
        subcommand = nob_shift_args(&argc, &argv);
    }

    if (strcmp(subcommand, "build") == 0) {
        // TODO: print the current config somehow, because it's useful information
        if (!build_raylib()) return 1;
        if (!build_musializer()) return 1;
        // // TODO: move the copying of musializer-logged.bat to nob_win64_*.c
        // if (config.target == TARGET_WIN64_MINGW || config.target == TARGET_WIN64_MSVC) {
        //     if (!nob_copy_file("musializer-logged.bat", "build/musializer-logged.bat")) return 1;
        // }
        if (!nob_copy_directory_recursively("./resources/", "./build/resources/")) return 1;
    } else if (strcmp(subcommand, "dist") == 0) {
        if (!build_dist()) return 1;
    } else if (strcmp(subcommand, "svg") == 0) {
        Nob_Procs procs = {0};

        Nob_Cmd cmd = {0};

        if (nob_needs_rebuild1("./resources/logo/logo-256.ico", "./resources/logo/logo.svg")) {
            cmd.count = 0;
            nob_cmd_append(&cmd, "convert");
            nob_cmd_append(&cmd, "-background", "None");
            nob_cmd_append(&cmd, "./resources/logo/logo.svg");
            nob_cmd_append(&cmd, "-resize", "256");
            nob_cmd_append(&cmd, "./resources/logo/logo-256.ico");
            nob_da_append(&procs, nob_cmd_run_async(cmd));
        } else {
            nob_log(NOB_INFO, "./resources/logo/logo-256.ico is up to date");
        }

        if (nob_needs_rebuild1("./resources/logo/logo-256.png", "./resources/logo/logo.svg")) {
            cmd.count = 0;
            nob_cmd_append(&cmd, "convert");
            nob_cmd_append(&cmd, "-background", "None");
            nob_cmd_append(&cmd, "./resources/logo/logo.svg");
            nob_cmd_append(&cmd, "-resize", "256");
            nob_cmd_append(&cmd, "./resources/logo/logo-256.png");
            nob_da_append(&procs, nob_cmd_run_async(cmd));
        } else {
            nob_log(NOB_INFO, "./resources/logo/logo-256.png is up to date");
        }

        if (nob_needs_rebuild1("./resources/icons/fullscreen.png", "./resources/icons/fullscreen.svg")) {
            cmd.count = 0;
            nob_cmd_append(&cmd, "convert");
            nob_cmd_append(&cmd, "-background", "None");
            nob_cmd_append(&cmd, "./resources/icons/fullscreen.svg");
            nob_cmd_append(&cmd, "./resources/icons/fullscreen.png");
            nob_da_append(&procs, nob_cmd_run_async(cmd));
        } else {
            nob_log(NOB_INFO, "./resources/icons/fullscreen.png is up to date");
        }

        if (nob_needs_rebuild1("./resources/icons/volume.png", "./resources/icons/volume.svg")) {
            cmd.count = 0;
            nob_cmd_append(&cmd, "convert");
            nob_cmd_append(&cmd, "-background", "None");
            nob_cmd_append(&cmd, "./resources/icons/volume.svg");
            nob_cmd_append(&cmd, "./resources/icons/volume.png");
            nob_da_append(&procs, nob_cmd_run_async(cmd));
        } else {
            nob_log(NOB_INFO, "./resources/icons/volume.png is up to date");
        }

        if (!nob_procs_wait(procs)) return 1;
    } else if (strcmp(subcommand, "help") == 0){
        log_available_subcommands(program, NOB_INFO);
    } else {
        nob_log(NOB_ERROR, "Unknown subcommand %s", subcommand);
        log_available_subcommands(program, NOB_ERROR);
    }
    // TODO: it would be nice to check for situations like building TARGET_WIN64_MSVC on Linux and report that it's not possible.
    return 0;
}

#else

void generate_default_configuration(Nob_String_Builder *content)
{
#ifdef _WIN32
#   if defined(_MSC_VER)
    nob_sb_append_cstr(content, "#define MUSIALIZER_TARGET TARGET_WIN64_MSVC\n");
#   else
    nob_sb_append_cstr(content, "#define MUSIALIZER_TARGET TARGET_WIN64_MINGW\n");
#   endif
#else
#   if defined (__APPLE__) || defined (__MACH__)
    nob_sb_append_cstr(content, "#define MUSIALIZER_TARGET TARGET_MACOS\n");
#   else
    nob_sb_append_cstr(content, "#define MUSIALIZER_TARGET TARGET_LINUX\n");
#   endif
#endif
    nob_sb_append_cstr(content, "// #define MUSIALIZER_HOTRELOAD\n");
    nob_sb_append_cstr(content, "// #define MUSIALIZER_MICROPHONE\n");
}

int main(int argc, char **argv)
{
    NOB_GO_REBUILD_URSELF(argc, argv);

    nob_log(NOB_INFO, "--- STAGE 1 ---");

    const char *program = nob_shift_args(&argc, &argv);

    if (!nob_mkdir_if_not_exists("build")) return 1;

    int config_exists = nob_file_exists(CONFIG_PATH);
    if (config_exists < 0) return 1;
    if (config_exists == 0) {
        nob_log(NOB_INFO, "Generating %s", CONFIG_PATH);
        Nob_String_Builder content = {0};
        generate_default_configuration(&content);
        if (!nob_write_entire_file(CONFIG_PATH, content.items, content.count)) return 1;
    } else {
        nob_log(NOB_INFO, "file `%s` already exists", CONFIG_PATH);
    }

    Nob_Cmd cmd = {0};
    const char *configured_binary = "./build/nob.configured";
    const char *deps[] = { __FILE__, CONFIG_PATH };
    int needs_rebuild = nob_needs_rebuild(configured_binary, deps, NOB_ARRAY_LEN(deps));
    if (needs_rebuild < 0) return 1;
    if (needs_rebuild) {
        nob_cmd_append(&cmd, NOB_REBUILD_URSELF(configured_binary, "nob.c"), "-DCONFIGURED");
        if (!nob_cmd_run_sync(cmd)) return 1;
    } else {
        nob_log(NOB_INFO, "executable `%s` is up to date", configured_binary);
    }

    cmd.count = 0;
    nob_cmd_append(&cmd, configured_binary);
    nob_da_append_many(&cmd, argv, argc);
    if (!nob_cmd_run_sync(cmd)) return 1;

    return 0;
}

#endif // CONFIGURED
