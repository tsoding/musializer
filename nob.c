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
#include "src/nob_linux.c"
#elif MUSIALIZER_TARGET == TARGET_MACOS
#include "src/nob_macos.c"
#elif MUSIALIZER_TARGET == TARGET_WIN64_MINGW
#include "src/nob_win64_mingw.c"
#elif MUSIALIZER_TARGET == TARGET_WIN64_MSVC
#include "src/nob_win64_msvc.c"
#endif // MUSIALIZER_TARGET

void log_available_subcommands(const char *program, Nob_Log_Level level)
{
    nob_log(level, "Usage: %s [subcommand]", program);
    nob_log(level, "Subcommands:");
    nob_log(level, "    build (default)");
    nob_log(level, "    dist");
    nob_log(level, "    svg");
    nob_log(level, "    help");
}

void log_config(Nob_Log_Level level)
{
    nob_log(level, "Target: %s", MUSIALIZER_TARGET_NAME);
#ifdef MUSIALIZER_HOTRELOAD
    nob_log(level, "Hotreload: ENABLED");
#else
    nob_log(level, "Hotreload: DISABLED");
#endif // MUSIALIZER_HOTRELOAD
#ifdef MUSIALIZER_MICROPHONE
    nob_log(level, "Microphone: ENABLED");
#else
    nob_log(level, "Microphone: DISABLED");
#endif // MUSIALIZER_MICROPHONE
}

int main(int argc, char **argv)
{
    nob_log(NOB_INFO, "--- STAGE 2 ---");
    log_config(NOB_INFO);
    nob_log(NOB_INFO, "---");

    const char *program = nob_shift_args(&argc, &argv);

    const char *subcommand = NULL;
    if (argc <= 0) {
        subcommand = "build";
    } else {
        subcommand = nob_shift_args(&argc, &argv);
    }

    if (strcmp(subcommand, "build") == 0) {
        if (!build_raylib()) return 1;
        if (!build_musializer()) return 1;
        if (!nob_copy_directory_recursively("./resources/", "./build/resources/")) return 1;
    } else if (strcmp(subcommand, "dist") == 0) {
        if (!build_dist()) return 1;
    } else if (strcmp(subcommand, "config") == 0) {
        nob_log(NOB_ERROR, "The `config` command does not exist anymore!");
        nob_log(NOB_ERROR, "Edit %s to configure the build!", CONFIG_PATH);
        return 1;
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

void generate_default_config(Nob_String_Builder *content)
{
    nob_sb_append_cstr(content, "//// Build target. Pick only one!\n");
#ifdef _WIN32
#   if defined(_MSC_VER)
    nob_sb_append_cstr(content, "// #define MUSIALIZER_TARGET TARGET_LINUX\n");
    nob_sb_append_cstr(content, "// #define MUSIALIZER_TARGET TARGET_WIN64_MINGW\n");
    nob_sb_append_cstr(content, "#define MUSIALIZER_TARGET TARGET_WIN64_MSVC\n");
    nob_sb_append_cstr(content, "// #define MUSIALIZER_TARGET TARGET_MACOS\n");
#   else
    nob_sb_append_cstr(content, "// #define MUSIALIZER_TARGET TARGET_LINUX\n");
    nob_sb_append_cstr(content, "#define MUSIALIZER_TARGET TARGET_WIN64_MINGW\n");
    nob_sb_append_cstr(content, "// #define MUSIALIZER_TARGET TARGET_WIN64_MSVC\n");
    nob_sb_append_cstr(content, "// #define MUSIALIZER_TARGET TARGET_MACOS\n");
#   endif
#else
#   if defined (__APPLE__) || defined (__MACH__)
    nob_sb_append_cstr(content, "// #define MUSIALIZER_TARGET TARGET_LINUX\n");
    nob_sb_append_cstr(content, "// #define MUSIALIZER_TARGET TARGET_WIN64_MINGW\n");
    nob_sb_append_cstr(content, "// #define MUSIALIZER_TARGET TARGET_WIN64_MSVC\n");
    nob_sb_append_cstr(content, "#define MUSIALIZER_TARGET TARGET_MACOS\n");
#   else
    nob_sb_append_cstr(content, "#define MUSIALIZER_TARGET TARGET_LINUX\n");
    nob_sb_append_cstr(content, "// #define MUSIALIZER_TARGET TARGET_WIN64_MINGW\n");
    nob_sb_append_cstr(content, "// #define MUSIALIZER_TARGET TARGET_WIN64_MSVC\n");
    nob_sb_append_cstr(content, "// #define MUSIALIZER_TARGET TARGET_MACOS\n");
#   endif
#endif
    nob_sb_append_cstr(content, "\n");
    nob_sb_append_cstr(content, "//// Moves everything in src/plub.c to a separate \"DLL\" so it can be hotreloaded. Works only for Linux right now\n");
    nob_sb_append_cstr(content, "// #define MUSIALIZER_HOTRELOAD\n");
    nob_sb_append_cstr(content, "\n");
    nob_sb_append_cstr(content, "//// Unfinished feature that enables capturing sound from the mic.\n");
    nob_sb_append_cstr(content, "// #define MUSIALIZER_MICROPHONE\n");
}

int main(int argc, char **argv)
{
    NOB_GO_REBUILD_URSELF(argc, argv);

    const char *program = nob_shift_args(&argc, &argv);
    const char *build_conf_path = "./build/build.conf";
    int build_conf_exists = nob_file_exists(build_conf_path);
    if (build_conf_exists < 0) return 1;
    if (build_conf_exists) {
        nob_log(NOB_ERROR, "We found %s. That means your build folder has an old schema.", build_conf_path);
        nob_log(NOB_ERROR, "Instead of %s you are suppose to use %s to configure the build now.", build_conf_path, CONFIG_PATH);
        nob_log(NOB_ERROR, "Remove your ./build/ folder and run %s again to regenerate the folder with the new schema.", program);
        return 1;
    }

    nob_log(NOB_INFO, "--- STAGE 1 ---");

    if (!nob_mkdir_if_not_exists("build")) return 1;

    int config_exists = nob_file_exists(CONFIG_PATH);
    if (config_exists < 0) return 1;
    if (config_exists == 0) {
        nob_log(NOB_INFO, "Generating %s", CONFIG_PATH);
        Nob_String_Builder content = {0};
        generate_default_config(&content);
        if (!nob_write_entire_file(CONFIG_PATH, content.items, content.count)) return 1;
    } else {
        nob_log(NOB_INFO, "file `%s` already exists", CONFIG_PATH);
    }

    Nob_Cmd cmd = {0};
    const char *configured_binary = "./build/nob.configured";
    nob_cmd_append(&cmd, NOB_REBUILD_URSELF(configured_binary, "nob.c"), "-DCONFIGURED");
    if (!nob_cmd_run_sync(cmd)) return 1;

    cmd.count = 0;
    nob_cmd_append(&cmd, configured_binary);
    nob_da_append_many(&cmd, argv, argc);
    if (!nob_cmd_run_sync(cmd)) return 1;

    return 0;
}

#endif // CONFIGURED
