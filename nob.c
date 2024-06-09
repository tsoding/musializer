#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>

#define NOB_IMPLEMENTATION
#include "./nob.h"

#define CONFIG_PATH "./build/config.h"

void generate_default_config(Nob_String_Builder *content)
{
    nob_sb_append_cstr(content, "//// Build target. Pick only one!\n");
#ifdef _WIN32
#   if defined(_MSC_VER)
    nob_sb_append_cstr(content, "// #define MUSIALIZER_TARGET_LINUX\n");
    nob_sb_append_cstr(content, "// #define MUSIALIZER_TARGET_WIN64_MINGW\n");
    nob_sb_append_cstr(content, "#define MUSIALIZER_TARGET_WIN64_MSVC\n");
    nob_sb_append_cstr(content, "// #define MUSIALIZER_TARGET_MACOS\n");
    nob_sb_append_cstr(content, "// #define MUSIALIZER_TARGET_OPENBSD\n");
#   else
    nob_sb_append_cstr(content, "// #define MUSIALIZER_TARGET_LINUX\n");
    nob_sb_append_cstr(content, "#define MUSIALIZER_TARGET_WIN64_MINGW\n");
    nob_sb_append_cstr(content, "// #define MUSIALIZER_TARGET_WIN64_MSVC\n");
    nob_sb_append_cstr(content, "// #define MUSIALIZER_TARGET_MACOS\n");
    nob_sb_append_cstr(content, "// #define MUSIALIZER_TARGET_OPENBSD\n");
#   endif
#elif defined (__APPLE__) || defined (__MACH__)
    nob_sb_append_cstr(content, "// #define MUSIALIZER_TARGET_LINUX\n");
    nob_sb_append_cstr(content, "// #define MUSIALIZER_TARGET_WIN64_MINGW\n");
    nob_sb_append_cstr(content, "// #define MUSIALIZER_TARGET_WIN64_MSVC\n");
    nob_sb_append_cstr(content, "#define MUSIALIZER_TARGET_MACOS\n");
    nob_sb_append_cstr(content, "// #define MUSIALIZER_TARGET_OPENBSD\n");
#elif defined(__OpenBSD__)
    nob_sb_append_cstr(content, "// #define MUSIALIZER_TARGET_LINUX\n");
    nob_sb_append_cstr(content, "// #define MUSIALIZER_TARGET_WIN64_MINGW\n");
    nob_sb_append_cstr(content, "// #define MUSIALIZER_TARGET_WIN64_MSVC\n");
    nob_sb_append_cstr(content, "// #define MUSIALIZER_TARGET_MACOS\n");
    nob_sb_append_cstr(content, "#define MUSIALIZER_TARGET_OPENBSD\n");
#else
    nob_sb_append_cstr(content, "#define MUSIALIZER_TARGET_LINUX\n");
    nob_sb_append_cstr(content, "// #define MUSIALIZER_TARGET_WIN64_MINGW\n");
    nob_sb_append_cstr(content, "// #define MUSIALIZER_TARGET_WIN64_MSVC\n");
    nob_sb_append_cstr(content, "// #define MUSIALIZER_TARGET_MACOS\n");
    nob_sb_append_cstr(content, "// #define MUSIALIZER_TARGET_OPENBSD\n");
#endif

    nob_sb_append_cstr(content, "\n");
    nob_sb_append_cstr(content, "//// Moves everything in src/plug.c to a separate \"DLL\" so it can be hotreloaded. Works only for Linux right now\n");
    // TODO: FIX ASAP! This requires bootstrapping nob with additional flags which goes against its philosophy!
#ifdef MUSIALIZER_HOTRELOAD
    nob_sb_append_cstr(content, "#define MUSIALIZER_HOTRELOAD\n");
#else
    nob_sb_append_cstr(content, "// #define MUSIALIZER_HOTRELOAD\n");
#endif
    nob_sb_append_cstr(content, "\n");
    nob_sb_append_cstr(content, "//// Don't bundle resources/ folder with the executable and load the resources directly from the folder.\n");
    nob_sb_append_cstr(content, "// #define MUSIALIZER_UNBUNDLE\n");
    nob_sb_append_cstr(content, "\n");
    nob_sb_append_cstr(content, "//// Unfinished feature that enables capturing sound from the mic.\n");
    nob_sb_append_cstr(content, "// #define MUSIALIZER_MICROPHONE\n");
    nob_sb_append_cstr(content, "\n");
    nob_sb_append_cstr(content, "//// Activate UI buttons on Press instead of Release just as John Carmack explained https://twitter.com/ID_AA_Carmack/status/1787850053912064005\n");
    nob_sb_append_cstr(content, "// #define MUSIALIZER_ACT_ON_PRESS\n");
}

int main(int argc, char **argv)
{
    NOB_GO_REBUILD_URSELF(argc, argv);

    const char *program = nob_shift_args(&argc, &argv);
    const char *build_conf_path = "./build/build.conf";
    int build_conf_exists = nob_file_exists(build_conf_path);
    if (build_conf_exists < 0) return 1;
    if (build_conf_exists) {
        // @backcomp
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
    const char *configured_binary = "build/nob.configured";
    nob_cmd_append(&cmd, NOB_REBUILD_URSELF(configured_binary, "./src_build/nob_configured.c"));
    if (!nob_cmd_run_sync(cmd)) return 1;

    cmd.count = 0;
    nob_cmd_append(&cmd, configured_binary);
    nob_da_append_many(&cmd, argv, argc);
    if (!nob_cmd_run_sync(cmd)) return 1;

    return 0;
}
