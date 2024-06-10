#define RAYLIB_VERSION "5.0"
#define CONFIG_PATH "./build/config.h"

typedef struct {
    const char *macro;
    bool enabled_by_default;
} Target_Flag;

static Target_Flag target_flags[] = {
    {
        .macro = "MUSIALIZER_TARGET_LINUX",
        #if defined(linux) || defined(__linux) || defined(__linux__)
            .enabled_by_default = true,
        #else
            .enabled_by_default = false,
        #endif
    },
    {
        .macro = "MUSIALIZER_TARGET_WIN64_MINGW",
        #if (defined(WIN32) || defined(_WIN32)) && defined(__MINGW32__)
            .enabled_by_default = true,
        #else
            .enabled_by_default = false,
        #endif
    },
    {
        .macro = "MUSIALIZER_TARGET_WIN64_MSVC",
        #if (defined(WIN32) || defined(_WIN32)) && defined(_MSC_VER)
            .enabled_by_default = true,
        #else
            .enabled_by_default = false,
        #endif
    },
    {
        .macro = "MUSIALIZER_TARGET_MACOS",
        #if defined(__APPLE__) || defined(__MACH__)
            .enabled_by_default = true,
        #else
            .enabled_by_default = false,
        #endif
    },
    {
        .macro = "MUSIALIZER_TARGET_OPENBSD",
        #if defined(__OpenBSD__)
            .enabled_by_default = true,
        #else
            .enabled_by_default = false,
        #endif
    },
};

typedef struct {
    const char *display;
    const char *macro;
    const char *description;
} Feature_Flag;

static Feature_Flag feature_flags[] = {
    {
        .display = "Hotreload",
        .macro = "MUSIALIZER_HOTRELOAD",
        .description = "Moves everything in src/plug.c to a separate \"DLL\" so it can be hotreloaded.",
    },
    {
        .display = "Unbundle",
        .macro = "MUSIALIZER_UNBUNDLE",
        .description = "Don't bundle resources/ folder with the executable and load the resources directly from the folder.",
    },
    {
        .display = "Microphone",
        .macro = "MUSIALIZER_MICROPHONE",
        .description = "Unfinished feature that enables capturing sound from the mic."
    },
    {
        .display = "Act on Press",
        .macro = "MUSIALIZER_ACT_ON_PRESS",
        .description = "Activate UI buttons on Press instead of Release just as John Carmack explained https://twitter.com/ID_AA_Carmack/status/1787850053912064005"
    },
};

#define genf(out, ...) \
    do { \
        fprintf((out), __VA_ARGS__); \
        fprintf((out), " // %s:%d\n", __FILE__, __LINE__); \
    } while(0)

bool generate_default_config(const char *file_path)
{
    nob_log(NOB_INFO, "Generating %s", file_path);
    FILE *f = fopen(file_path, "wb");
    if (f == NULL) {
        nob_log(NOB_ERROR, "Could not generate %s: %s", file_path, strerror(errno));
        return false;
    }

    fprintf(f, "//// Build target. Pick only one!\n");
    for (size_t i = 0; i < NOB_ARRAY_LEN(target_flags); ++i) {
        if (target_flags[i].enabled_by_default) {
            fprintf(f, "#define %s\n", target_flags[i].macro);
        } else {
            fprintf(f, "// #define %s\n", target_flags[i].macro);
        }
    }

    fprintf(f, "\n");

    for (size_t i = 0; i < NOB_ARRAY_LEN(feature_flags); ++i) {
        fprintf(f, "//// %s\n", feature_flags[i].description);
        if (strcmp(feature_flags[i].macro, "MUSIALIZER_HOTRELOAD") == 0) {
            // TODO: FIX ASAP! This requires bootstrapping nob with additional flags which goes against its philosophy!
            #ifdef MUSIALIZER_HOTRELOAD
                fprintf(f, "#define %s\n", feature_flags[i].macro);
            #else
                fprintf(f, "// #define %s\n", feature_flags[i].macro);
            #endif
        } else {
            fprintf(f, "// #define %s\n", feature_flags[i].macro);
        }
        fprintf(f, "\n");
    }

    fclose(f);
    return true;
}

bool generate_config_logger(const char *config_logger_path)
{
    nob_log(NOB_INFO, "Generating %s", config_logger_path);
    FILE *f = fopen(config_logger_path, "wb");
    if (f == NULL) {
        nob_log(NOB_ERROR, "Could not generate %s: %s", config_logger_path, strerror(errno));
        return false;
    }

    genf(f, "void log_config(Nob_Log_Level level)");
    genf(f, "{");
    genf(f, "    nob_log(level, \"Target: %%s\", MUSIALIZER_TARGET_NAME);");
    for (size_t i = 0; i < NOB_ARRAY_LEN(feature_flags); ++i) {
        genf(f, "    #ifdef %s", feature_flags[i].macro);
        genf(f, "        nob_log(level, \"%s: ENABLED\");", feature_flags[i].display);
        genf(f, "    #else");
        genf(f, "        nob_log(level, \"%s: DISABLED\");", feature_flags[i].display);
        genf(f, "    #endif");
    }
    genf(f, "}");

    fclose(f);
    return true;
}
