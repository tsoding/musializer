#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>

#define NOB_IMPLEMENTATION
#include "./src/nob.h"

typedef enum {
    TARGET_LINUX,
    TARGET_WIN64_MINGW,
    TARGET_WIN64_MSVC,
    TARGET_MACOS,
    COUNT_TARGETS
} Target;

static_assert(4 == COUNT_TARGETS, "Amount of targets have changed");
const char *target_names[] = {
    [TARGET_LINUX]       = "linux",
    [TARGET_WIN64_MINGW] = "win64-mingw",
    [TARGET_WIN64_MSVC]  = "win64-msvc",
    [TARGET_MACOS]       = "macos",
};

void log_available_targets(Nob_Log_Level level)
{
    nob_log(level, "Available targets:");
    for (size_t i = 0; i < COUNT_TARGETS; ++i) {
        nob_log(level, "    %s", target_names[i]);
    }
}

typedef struct {
    Target target;
    bool hotreload;
    bool microphone;
} Config;

bool compute_default_config(Config *config)
{
    memset(config, 0, sizeof(Config));
#ifdef _WIN32
#   if defined(_MSC_VER)
        config->target = TARGET_WIN64_MSVC;
#   else
        config->target = TARGET_WIN64_MINGW;
#   endif
#else
#   if defined (__APPLE__) || defined (__MACH__)
        config->target = TARGET_MACOS;
#   else
        config->target = TARGET_LINUX;
#   endif
#endif
    return true;
}

bool parse_config_from_args(int argc, char **argv, Config *config)
{
    while (argc > 0) {
        const char *flag = nob_shift_args(&argc, &argv);
        if (strcmp(flag, "-t") == 0) {
            if (argc <= 0) {
                nob_log(NOB_ERROR, "No value is provided for flag %s", flag);
                log_available_targets(NOB_ERROR);
                return false;
            }

            const char *value = nob_shift_args(&argc, &argv);

            bool found = false;
            for (size_t i = 0; !found && i < COUNT_TARGETS; ++i) {
                if (strcmp(target_names[i], value) == 0) {
                    config->target = i;
                    found = true;
                }
            }

            if (!found) {
                nob_log(NOB_ERROR, "Unknown target %s", value);
                log_available_targets(NOB_ERROR);
                return false;
            }
        } else if (strcmp("-r", flag) == 0) {
            config->hotreload = true;
        } else if (strcmp("-m", flag) == 0) {
            config->microphone = true;
        } else if (strcmp("-h", flag) == 0 || strcmp("--help", flag) == 0) {
            nob_log(NOB_INFO, "Available config flags:");
            nob_log(NOB_INFO, "    -t <target>    set build target");
            nob_log(NOB_INFO, "    -r             enable hotreload");
            nob_log(NOB_INFO, "    -m             enable microphone");
            nob_log(NOB_INFO, "    -h             print this help");
            return false;
        } else {
            nob_log(NOB_ERROR, "Unknown flag %s", flag);
            return false;
        }
    }
    return true;
}

void log_config(Config config)
{
    nob_log(NOB_INFO, "Target: %s", NOB_ARRAY_GET(target_names, config.target));
    nob_log(NOB_INFO, "Hotreload: %s", config.hotreload ? "ENABLED" : "DISABLED");
    nob_log(NOB_INFO, "Microphone: %s", config.microphone ? "ENABLED" : "DISABLED");
}

bool dump_config_to_file(const char *path, Config config)
{
    Nob_String_Builder sb = {0};
    nob_log(NOB_INFO, "Saving configuration to %s", path);
    nob_sb_append_cstr(&sb, nob_temp_sprintf("target = %s"NOB_LINE_END, NOB_ARRAY_GET(target_names, config.target)));
    nob_sb_append_cstr(&sb, nob_temp_sprintf("hotreload = %s"NOB_LINE_END, config.hotreload ? "true" : "false"));
    nob_sb_append_cstr(&sb, nob_temp_sprintf("microphone = %s"NOB_LINE_END, config.microphone ? "true" : "false"));
    bool res = nob_write_entire_file(path, sb.items, sb.count);
    nob_sb_free(sb);
    return res;
}

bool config_parse_boolean(const char *path, size_t row, Nob_String_View token, bool *boolean)
{
    if (nob_sv_eq(token, nob_sv_from_cstr("true"))) {
        *boolean = true;
    } else if (nob_sv_eq(token, nob_sv_from_cstr("false"))) {
        *boolean = false;
    } else {
        nob_log(NOB_ERROR, "%s:%zu: Invalid boolean `"SV_Fmt"`", path, row + 1, SV_Arg(token));
        nob_log(NOB_ERROR, "Expected `true` or `false`");
        return false;
    }
    return true;
}

bool config_parse_target(const char *path, size_t row, Nob_String_View token, Target *target)
{
    bool found = false;
    for (size_t t = 0; !found && t < COUNT_TARGETS; ++t) {
        if (nob_sv_eq(token, nob_sv_from_cstr(target_names[t]))) {
            *target = t;
            return true;
        }
    }
    nob_log(NOB_ERROR, "%s:%zu: Invalid target `"SV_Fmt"`", path, row + 1, SV_Arg(token));
    log_available_targets(NOB_ERROR);
    return false;
}

bool load_config_from_file(const char *path, Config *config)
{
    bool result = true;
    Nob_String_Builder sb = {0};

    nob_log(NOB_INFO, "Loading configuration from %s", path);

    if (!nob_read_entire_file(path, &sb)) nob_return_defer(false);

    Nob_String_View content = {
        .data = sb.items,
        .count = sb.count,
    };

    for (size_t row = 0; content.count > 0; ++row) {
        Nob_String_View line = nob_sv_trim(nob_sv_chop_by_delim(&content, '\n'));
        if (line.count == 0) continue;

        Nob_String_View key = nob_sv_trim(nob_sv_chop_by_delim(&line, '='));
        Nob_String_View value = nob_sv_trim(line);

        if (nob_sv_eq(key, nob_sv_from_cstr("target"))) {
            if (!config_parse_target(path, row, value, &config->target)) nob_return_defer(false);
        } else if (nob_sv_eq(key, nob_sv_from_cstr("hotreload"))) {
            if (!config_parse_boolean(path, row, value, &config->hotreload)) nob_return_defer(false);
        } else if (nob_sv_eq(key, nob_sv_from_cstr("microphone"))) {
            if (!config_parse_boolean(path, row, value, &config->microphone)) nob_return_defer(false);
        } else {
            nob_log(NOB_ERROR, "%s:%zu: Invalid key `"SV_Fmt"`", path, row + 1, SV_Arg(key));
            nob_return_defer(false);
        }
    }

defer:
    nob_sb_free(sb);
    return result;
}

bool build_musializer(Config config)
{
    bool result = true;
    Nob_Cmd cmd = {0};
    Nob_Procs procs = {0};

    switch (config.target) {
        case TARGET_LINUX: {
            if (config.hotreload) {
                procs.count = 0;
                    cmd.count = 0;
                        // TODO: add a way to replace `cc` with something else GCC compatible on POSIX
                        // Like `clang` for instance
                        nob_cmd_append(&cmd, "cc");
                        nob_cmd_append(&cmd, "-Wall", "-Wextra", "-ggdb");
                        if (config.microphone) nob_cmd_append(&cmd, "-DFEATURE_MICROPHONE");
                        nob_cmd_append(&cmd, "-I./raylib/raylib-4.5.0/src/");
                        nob_cmd_append(&cmd, "-fPIC", "-shared");
                        nob_cmd_append(&cmd, "-o", "./build/libplug.so");
                        nob_cmd_append(&cmd,
                            "./src/plug.c",
                            "./src/ffmpeg_linux.c");
                        nob_cmd_append(&cmd,
                            nob_temp_sprintf("-L./build/raylib/%s", NOB_ARRAY_GET(target_names, config.target)),
                            "-l:libraylib.so");
                        nob_cmd_append(&cmd, "-lm", "-ldl", "-lpthread");
                    nob_da_append(&procs, nob_cmd_run_async(cmd));

                    cmd.count = 0;
                        nob_cmd_append(&cmd, "cc");
                        nob_cmd_append(&cmd, "-Wall", "-Wextra", "-ggdb");
                        if (config.microphone) nob_cmd_append(&cmd, "-DFEATURE_MICROPHONE");
                        nob_cmd_append(&cmd, "-I./raylib/raylib-4.5.0/src/");
                        nob_cmd_append(&cmd, "-DHOTRELOAD");
                        nob_cmd_append(&cmd, "-o", "./build/musializer");
                        nob_cmd_append(&cmd,
                            "./src/musializer.c",
                            "./src/hotreload_linux.c");
                        nob_cmd_append(&cmd,
                            "-Wl,-rpath=./build/",
                            "-Wl,-rpath=./",
                            nob_temp_sprintf("-Wl,-rpath=./build/raylib/%s", NOB_ARRAY_GET(target_names, config.target)),
                            // NOTE: just in case somebody wants to run musializer from within the ./build/ folder
                            nob_temp_sprintf("-Wl,-rpath=./raylib/%s", NOB_ARRAY_GET(target_names, config.target)));
                        nob_cmd_append(&cmd,
                            nob_temp_sprintf("-L./build/raylib/%s", NOB_ARRAY_GET(target_names, config.target)),
                            "-l:libraylib.so");
                        nob_cmd_append(&cmd, "-lm", "-ldl", "-lpthread");
                    nob_da_append(&procs, nob_cmd_run_async(cmd));
                if (!nob_procs_wait(procs)) nob_return_defer(false);
            } else {
                cmd.count = 0;
                    nob_cmd_append(&cmd, "cc");
                    nob_cmd_append(&cmd, "-Wall", "-Wextra", "-ggdb");
                    if (config.microphone) nob_cmd_append(&cmd, "-DFEATURE_MICROPHONE");
                    nob_cmd_append(&cmd, "-I./raylib/raylib-4.5.0/src/");
                    nob_cmd_append(&cmd, "-o", "./build/musializer");
                    nob_cmd_append(&cmd,
                        "./src/plug.c",
                        "./src/ffmpeg_linux.c",
                        "./src/musializer.c");
                    nob_cmd_append(&cmd,
                        nob_temp_sprintf("-L./build/raylib/%s", NOB_ARRAY_GET(target_names, config.target)),
                        "-l:libraylib.a");
                    nob_cmd_append(&cmd, "-lm", "-ldl", "-lpthread");
                if (!nob_cmd_run_sync(cmd)) nob_return_defer(false);
            }
        } break;

        case TARGET_MACOS: {
            if (config.hotreload) {
                nob_log(NOB_ERROR, "TODO: hotreloading is not supported on %s yet", NOB_ARRAY_GET(target_names, config.target));
                nob_return_defer(false);
            }

            cmd.count = 0;
                nob_cmd_append(&cmd, "clang");
                nob_cmd_append(&cmd, "-Wall", "-Wextra", "-g");
                if (config.microphone) nob_cmd_append(&cmd, "-DFEATURE_MICROPHONE");
                nob_cmd_append(&cmd, "-I./raylib/raylib-4.5.0/src/");
                nob_cmd_append(&cmd, "-o", "./build/musializer");
                nob_cmd_append(&cmd,
                    "./src/plug.c",
                    "./src/ffmpeg_linux.c",
                    "./src/musializer.c");
                nob_cmd_append(&cmd,
                    nob_temp_sprintf("./build/raylib/%s/libraylib.a", NOB_ARRAY_GET(target_names, config.target)));

                nob_cmd_append(&cmd, "-framework", "CoreVideo");
                nob_cmd_append(&cmd, "-framework", "IOKit");
                nob_cmd_append(&cmd, "-framework", "Cocoa");
                nob_cmd_append(&cmd, "-framework", "GLUT");
                nob_cmd_append(&cmd, "-framework", "OpenGL");

                nob_cmd_append(&cmd, "-lm", "-ldl", "-lpthread");
            if (!nob_cmd_run_sync(cmd)) nob_return_defer(false);
        } break;

        case TARGET_WIN64_MINGW: {
            if (config.hotreload) {
                nob_log(NOB_ERROR, "TODO: hotreloading is not supported on %s yet", NOB_ARRAY_GET(target_names, config.target));
                nob_return_defer(false);
            } else {
                cmd.count = 0;
                #ifdef _WIN32
                    // On windows, mingw doesn't have the `x86_64-w64-mingw32-` prefix for windres.
                    // For gcc, you can use both `x86_64-w64-mingw32-gcc` and just `gcc`
                    nob_cmd_append(&cmd, "windres");
                #else
                    nob_cmd_append(&cmd, "x86_64-w64-mingw32-windres");
                #endif // _WIN32
                    nob_cmd_append(&cmd, "./src/musializer.rc");
                    nob_cmd_append(&cmd, "-O", "coff");
                    nob_cmd_append(&cmd, "-o", "./build/musializer.res");
                if (!nob_cmd_run_sync(cmd)) nob_return_defer(false);

                cmd.count = 0;
                    nob_cmd_append(&cmd, "x86_64-w64-mingw32-gcc");
                    nob_cmd_append(&cmd, "-Wall", "-Wextra", "-ggdb");
                    if (config.microphone) nob_cmd_append(&cmd, "-DFEATURE_MICROPHONE");
                    nob_cmd_append(&cmd, "-I./raylib/raylib-4.5.0/src/");
                    nob_cmd_append(&cmd, "-o", "./build/musializer");
                    nob_cmd_append(&cmd,
                        "./src/plug.c",
                        "./src/ffmpeg_windows.c",
                        "./src/musializer.c",
                        "./build/musializer.res"
                        );
                    nob_cmd_append(&cmd,
                        nob_temp_sprintf("-L./build/raylib/%s", NOB_ARRAY_GET(target_names, config.target)),
                        "-l:libraylib.a");
                    nob_cmd_append(&cmd, "-lwinmm", "-lgdi32");
                    nob_cmd_append(&cmd, "-static");
                if (!nob_cmd_run_sync(cmd)) nob_return_defer(false);
            }
        } break;

        case TARGET_WIN64_MSVC: {
            if (config.hotreload) {
                nob_log(NOB_ERROR, "TODO: hotreloading is not supported on %s yet", NOB_ARRAY_GET(target_names, config.target));
                nob_return_defer(false);
            } else {
                cmd.count = 0;
                    nob_cmd_append(&cmd, "rc");
                    nob_cmd_append(&cmd, "/fo", "./build/musializer.res");
                    nob_cmd_append(&cmd, "./src/musializer.rc");
                    // NOTE: Do not change the order of commandline arguments to rc. Their argparser is weird.
                if (!nob_cmd_run_sync(cmd)) nob_return_defer(false);
                cmd.count = 0;
                    nob_cmd_append(&cmd, "cl.exe");
                    if (config.microphone) nob_cmd_append(&cmd, "/DFEATURE_MICROPHONE");
                    nob_cmd_append(&cmd, "/I", "./raylib/raylib-4.5.0/src/");
                    nob_cmd_append(&cmd, "/Fobuild\\", "/Febuild\\musializer.exe");
                    nob_cmd_append(&cmd,
                        "./src/musializer.c",
                        "./src/plug.c",
                        "./src/ffmpeg_windows.c"
                        // TODO: building resource file is not implemented for TARGET_WIN64_MSVC
                        );
                    nob_cmd_append(&cmd,
                        "/link",
                        nob_temp_sprintf("/LIBPATH:build/raylib/%s", NOB_ARRAY_GET(target_names, config.target)),
                        "raylib.lib");
                    nob_cmd_append(&cmd, "Winmm.lib", "gdi32.lib", "User32.lib", "Shell32.lib", "./build/musializer.res");
                    // TODO: is some sort of `-static` flag needed for MSVC to get a statically linked executable
                    //nob_cmd_append(&cmd, "-static");
                if (!nob_cmd_run_sync(cmd)) nob_return_defer(false);
            }
        } break;

        default: NOB_ASSERT(0 && "unreachable");
    }

defer:
    nob_cmd_free(cmd);
    nob_da_free(procs);
    return result;
}

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
        const char *input_path = nob_temp_sprintf("./raylib/raylib-4.5.0/src/%s.c", raylib_modules[i]);
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
                    nob_cmd_append(&cmd, "-I./raylib/raylib-4.5.0/src/external/glfw/include");
                    nob_cmd_append(&cmd, "-c", input_path);
                    nob_cmd_append(&cmd, "-o", output_path);
                    break;
                case TARGET_MACOS:
                    nob_cmd_append(&cmd, "clang");
                    nob_cmd_append(&cmd, "-g", "-DPLATFORM_DESKTOP", "-fPIC");
                    nob_cmd_append(&cmd, "-I./raylib/raylib-4.5.0/src/external/glfw/include");
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
                    nob_cmd_append(&cmd, "-I./raylib/raylib-4.5.0/src/external/glfw/include");
                    nob_cmd_append(&cmd, "-c", input_path);
                    nob_cmd_append(&cmd, "-o", output_path);
                    break;
                case TARGET_WIN64_MSVC:
                    nob_cmd_append(&cmd, "cl.exe", "/DPLATFORM_DESKTOP");
                    nob_cmd_append(&cmd, "/I", "./raylib/raylib-4.5.0/src/external/glfw/include");
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

void log_available_subcommands(const char *program, Nob_Log_Level level)
{
    nob_log(level, "Usage: %s [subcommand]", program);
    nob_log(level, "Subcommands:");
    nob_log(level, "    build (default)");
    nob_log(level, "    config");
    nob_log(level, "    dist");
    nob_log(level, "    svg");
    nob_log(level, "    help");
}

int main(int argc, char **argv)
{
    NOB_GO_REBUILD_URSELF(argc, argv);

    const char *program = nob_shift_args(&argc, &argv);

    const char *subcommand = NULL;
    if (argc <= 0) {
        subcommand = "build";
    } else {
        subcommand = nob_shift_args(&argc, &argv);
    }

    if (strcmp(subcommand, "build") == 0) {
        Config config = {0};
        switch (nob_file_exists("./build/build.conf")) {
            case -1:
                return 1;
            case 0:
                if (!nob_mkdir_if_not_exists("build")) return 1;
                if (!compute_default_config(&config)) return 1;
                if (!dump_config_to_file("./build/build.conf", config)) return 1;
                break;
            case 1:
                if (!load_config_from_file("./build/build.conf", &config)) return 1;
                break;
        }
        nob_log(NOB_INFO, "------------------------------");
        log_config(config);
        nob_log(NOB_INFO, "------------------------------");
        if (!build_raylib(config)) return 1;
        if (!build_musializer(config)) return 1;
        if (config.target == TARGET_WIN64_MINGW || config.target == TARGET_WIN64_MSVC) {
            if (!nob_copy_file("musializer-logged.bat", "build/musializer-logged.bat")) return 1;
        }
        if (!nob_copy_directory_recursively("./resources/", "./build/resources/")) return 1;
    } else if (strcmp(subcommand, "config") == 0) {
        Config config = {0};
        if (!nob_mkdir_if_not_exists("build")) return 1;
        if (!compute_default_config(&config)) return 1;
        if (!parse_config_from_args(argc, argv, &config)) return 1;
        nob_log(NOB_INFO, "------------------------------");
        log_config(config);
        nob_log(NOB_INFO, "------------------------------");
        if (!dump_config_to_file("./build/build.conf", config)) return 1;
    } else if (strcmp(subcommand, "dist") == 0) {
        Config config = {0};
        if (!load_config_from_file("./build/build.conf", &config)) return 1;
        nob_log(NOB_INFO, "------------------------------");
        log_config(config);
        nob_log(NOB_INFO, "------------------------------");
        if (!build_dist(config)) return 1;
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
