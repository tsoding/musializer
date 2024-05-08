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

#include "./src/targets.h"
#include CONFIG_PATH

#define RAYLIB_VERSION "5.0"

#define TARGET_LINUX 0
#define TARGET_WIN64_MINGW 1
#define TARGET_WIN64_MSVC 2
#define TARGET_MACOS 3
#define TARGET_OPENBSD 4


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
#elif MUSIALIZER_TARGET == TARGET_OPENBSD
#include "src/nob_openbsd.c"
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

#define genf(out, ...) \
    do { \
        fprintf((out), __VA_ARGS__); \
        fprintf((out), " // %s:%d\n", __FILE__, __LINE__); \
    } while(0)

typedef struct {
    const char *file_path;
    size_t offset;
    size_t size;
} Resource;

Resource resources[] = {
    { .file_path = "./resources/logo/logo-256.png" },
    { .file_path = "./resources/shaders/glsl330/circle.fs" },
    { .file_path = "./resources/shaders/glsl120/circle.fs" },
    { .file_path = "./resources/icons/volume.png" },
    { .file_path = "./resources/icons/play.png" },
    { .file_path = "./resources/icons/render.png" },
    { .file_path = "./resources/icons/fullscreen.png" },
    { .file_path = "./resources/icons/microphone.png" },
    { .file_path = "./resources/fonts/Alegreya-Regular.ttf" },
};

bool generate_resource_bundle(void)
{
    bool result = true;
    Nob_String_Builder bundle = {0};
    Nob_String_Builder content = {0};
    FILE *out = NULL;

    // bundle  = [aaaaaaaaabbbbb]
    //            ^        ^
    // content = []
    // 0, 9

    for (size_t i = 0; i < NOB_ARRAY_LEN(resources); ++i) {
        content.count = 0;
        if (!nob_read_entire_file(resources[i].file_path, &content)) nob_return_defer(false);
        resources[i].offset = bundle.count;
        resources[i].size = content.count;
        nob_da_append_many(&bundle, content.items, content.count);
        nob_da_append(&bundle, 0);
    }

    const char *bundle_h_path = "./build/bundle.h";
    out = fopen(bundle_h_path, "wb");
    if (out == NULL) {
        nob_log(NOB_ERROR, "Could not open file %s for writing: %s", bundle_h_path, strerror(errno));
        nob_return_defer(false);
    }

    genf(out, "#ifndef BUNDLE_H_");
    genf(out, "#define BUNDLE_H_");
    genf(out, "typedef struct {");
    genf(out, "    const char *file_path;");
    genf(out, "    size_t offset;");
    genf(out, "    size_t size;");
    genf(out, "} Resource;");
    genf(out, "size_t resources_count = %zu;", NOB_ARRAY_LEN(resources));
    genf(out, "Resource resources[] = {");
    for (size_t i = 0; i < NOB_ARRAY_LEN(resources); ++i) {
        genf(out, "    {.file_path = \"%s\", .offset = %zu, .size = %zu},",
               resources[i].file_path, resources[i].offset, resources[i].size);
    }
    genf(out, "};");

    genf(out, "unsigned char bundle[] = {");
    size_t row_size = 20;
    for (size_t i = 0; i < bundle.count; ){
        fprintf(out, "     ");
        for (size_t col = 0; col < row_size && i < bundle.count; ++col, ++i) {
            fprintf(out, "0x%02X, ", (unsigned char)bundle.items[i]);
        }
        genf(out, "");
    }
    genf(out, "};");
    genf(out, "#endif // BUNDLE_H_");

    nob_log(NOB_INFO, "Generated %s", bundle_h_path);

defer:
    if (out) fclose(out);
    free(content.items);
    free(bundle.items);
    return result;
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
#ifndef MUSIALIZER_UNBUNDLE
        if (!generate_resource_bundle()) return 1;
#endif // MUSIALIZER_UNBUNDLE
        if (!build_musializer()) return 1;
    } else if (strcmp(subcommand, "dist") == 0) {
        if (!build_dist()) return 1;
    } else if (strcmp(subcommand, "config") == 0) {
        nob_log(NOB_ERROR, "The `config` command does not exist anymore!");
        nob_log(NOB_ERROR, "Edit %s to configure the build!", CONFIG_PATH);
        return 1;
    } else if (strcmp(subcommand, "svg") == 0) {
        Nob_Procs procs = {0};

        Nob_Cmd cmd = {0};

        typedef struct {
            const char *in_path;
            const char *out_path;
            int resize;
        } Svg;

        Svg svgs[] = {
            {.out_path = "./resources/logo/logo-256.ico",    .in_path = "./resources/logo/logo.svg", .resize = 256, },
            {.out_path = "./resources/logo/logo-256.png",    .in_path = "./resources/logo/logo.svg", .resize = 256, },
            {.out_path = "./resources/icons/fullscreen.png", .in_path = "./resources/icons/fullscreen.svg"          },
            {.out_path = "./resources/icons/volume.png",     .in_path = "./resources/icons/volume.svg"              },
            {.out_path = "./resources/icons/play.png",       .in_path = "./resources/icons/play.svg"                },
            {.out_path = "./resources/icons/render.png",     .in_path = "./resources/icons/render.svg"              },
            {.out_path = "./resources/icons/microphone.png", .in_path = "./resources/icons/microphone.svg"          },
        };

        for (size_t i = 0; i < NOB_ARRAY_LEN(svgs); ++i) {
            if (nob_needs_rebuild1(svgs[i].out_path, svgs[i].in_path)) {
                cmd.count = 0;
                nob_cmd_append(&cmd, "convert");
                nob_cmd_append(&cmd, "-background", "None");
                nob_cmd_append(&cmd, svgs[i].in_path);
                if (svgs[i].resize) {
                    nob_cmd_append(&cmd, "-resize", nob_temp_sprintf("%d", svgs[i].resize));
                }
                nob_cmd_append(&cmd, svgs[i].out_path);
                nob_da_append(&procs, nob_cmd_run_async(cmd));
            } else {
                nob_log(NOB_INFO, "%s is up to date", svgs[i].out_path);
            }
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
    nob_sb_append_cstr(content, "// #define MUSIALIZER_TARGET TARGET_OPENBSD\n");
#   else
    nob_sb_append_cstr(content, "// #define MUSIALIZER_TARGET TARGET_LINUX\n");
    nob_sb_append_cstr(content, "#define MUSIALIZER_TARGET TARGET_WIN64_MINGW\n");
    nob_sb_append_cstr(content, "// #define MUSIALIZER_TARGET TARGET_WIN64_MSVC\n");
    nob_sb_append_cstr(content, "// #define MUSIALIZER_TARGET TARGET_MACOS\n");
    nob_sb_append_cstr(content, "// #define MUSIALIZER_TARGET TARGET_OPENBSD\n");
#   endif
#elif defined (__APPLE__) || defined (__MACH__)
    nob_sb_append_cstr(content, "// #define MUSIALIZER_TARGET TARGET_LINUX\n");
    nob_sb_append_cstr(content, "// #define MUSIALIZER_TARGET TARGET_WIN64_MINGW\n");
    nob_sb_append_cstr(content, "// #define MUSIALIZER_TARGET TARGET_WIN64_MSVC\n");
    nob_sb_append_cstr(content, "#define MUSIALIZER_TARGET TARGET_MACOS\n");
    nob_sb_append_cstr(content, "// #define MUSIALIZER_TARGET TARGET_OPENBSD\n");
#elif defined(__OpenBSD__)
    nob_sb_append_cstr(content, "// #define MUSIALIZER_TARGET TARGET_LINUX\n");
    nob_sb_append_cstr(content, "// #define MUSIALIZER_TARGET TARGET_WIN64_MINGW\n");
    nob_sb_append_cstr(content, "// #define MUSIALIZER_TARGET TARGET_WIN64_MSVC\n");
    nob_sb_append_cstr(content, "// #define MUSIALIZER_TARGET TARGET_MACOS\n");
    nob_sb_append_cstr(content, "#define MUSIALIZER_TARGET TARGET_OPENBSD\n");
#else
    nob_sb_append_cstr(content, "#define MUSIALIZER_TARGET TARGET_LINUX\n");
    nob_sb_append_cstr(content, "// #define MUSIALIZER_TARGET TARGET_WIN64_MINGW\n");
    nob_sb_append_cstr(content, "// #define MUSIALIZER_TARGET TARGET_WIN64_MSVC\n");
    nob_sb_append_cstr(content, "// #define MUSIALIZER_TARGET TARGET_MACOS\n");
    nob_sb_append_cstr(content, "// #define MUSIALIZER_TARGET TARGET_OPENBSD\n");
#endif

    nob_sb_append_cstr(content, "\n");
    nob_sb_append_cstr(content, "//// Moves everything in src/plug.c to a separate \"DLL\" so it can be hotreloaded. Works only for Linux right now\n");
    nob_sb_append_cstr(content, "// #define MUSIALIZER_HOTRELOAD\n");
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
    nob_cmd_append(&cmd, NOB_REBUILD_URSELF(configured_binary, "nob.c"), "-DCONFIGURED");
    if (!nob_cmd_run_sync(cmd)) return 1;

    cmd.count = 0;
    nob_cmd_append(&cmd, configured_binary);
    nob_da_append_many(&cmd, argv, argc);
    if (!nob_cmd_run_sync(cmd)) return 1;

    return 0;
}

#endif // CONFIGURED
