#include <stdbool.h>

#define NOB_IMPLEMENTATION
#include "../nob.h"
#include "../build/config.h"
#include "./configurer.c"

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

// @backcomp
#if defined(MUSIALIZER_TARGET)
#error "We recently replaced a single MUSIALIZER_TARGET macro with a bunch of MUSIALIZER_TARGET_<TARGET> macros instead. Since MUSIALIZER_TARGET is still defined your ./build/ is probably old. Please remove it so ./build/config.h gets regenerated."
#endif // MUSIALIZER_TARGET

#if defined(MUSIALIZER_TARGET_LINUX)
#include "nob_linux.c"
#elif defined(MUSIALIZER_TARGET_MACOS)
#include "nob_macos.c"
#elif defined(MUSIALIZER_TARGET_WIN64_MINGW)
#include "nob_win64_mingw.c"
#elif defined(MUSIALIZER_TARGET_WIN64_MSVC)
#include "nob_win64_msvc.c"
#elif defined(MUSIALIZER_TARGET_OPENBSD)
#include "nob_openbsd.c"
#else
#error "No Musializer Target is defined. Check your ./build/config.h."
#endif // MUSIALIZER_TARGET

#include "../build/config_logger.c"

void log_available_subcommands(const char *program, Nob_Log_Level level)
{
    nob_log(level, "Usage: %s [subcommand]", program);
    nob_log(level, "Subcommands:");
    nob_log(level, "    build (default)");
    nob_log(level, "    dist");
    nob_log(level, "    svg");
    nob_log(level, "    help");
}

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
    for (size_t i = 0; i < bundle.count; ) {
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
    } else if (strcmp(subcommand, "help") == 0) {
        log_available_subcommands(program, NOB_INFO);
    } else {
        nob_log(NOB_ERROR, "Unknown subcommand %s", subcommand);
        log_available_subcommands(program, NOB_ERROR);
    }
    // TODO: it would be nice to check for situations like building TARGET_WIN64_MSVC on Linux and report that it's not possible.
    return 0;
}
