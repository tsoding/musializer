#define RAYLIB_VERSION "5.5"
#define CONFIG_PATH "./build/config.h"
#define RAYLIB_SRC_FOLDER "./thirdparty/raylib-" RAYLIB_VERSION "/src/"

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
    const char *name;
    const char *macro;
    const char *description;
    bool enabled_by_default;
} Feature_Flag;

static Feature_Flag feature_flags[] = {
    {
        .macro = "MUSIALIZER_HOTRELOAD",
        .name = "hotreload",
        .description = "Moves everything in src/plug.c to a separate \"DLL\" so it can be hotreloaded.",
    },
    {
        .macro = "MUSIALIZER_UNBUNDLE",
        .name = "unbundle",
        .description = "Don't bundle resources/ folder with the executable and load the resources directly from the folder.",
    },
    {
        .macro = "MUSIALIZER_MICROPHONE",
        .name = "microphone",
        .description = "Unfinished feature that enables capturing sound from the mic."
    },
};

// Removed feature flags
#ifdef MUSIALIZER_ACT_ON_PRESS
#error "MUSIALIZER_ACT_ON_PRESS no longer exists. Please remove it from your build/config.h"
#endif // MUSIALIZER_ACT_ON_PRESS

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

    // TODO: generate_default_config() should also log what platform it picked
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
        if (feature_flags[i].enabled_by_default) {
            nob_log(INFO, "%s: ENABLED", feature_flags[i].name);
            fprintf(f, "#define %s\n", feature_flags[i].macro);
        } else {
            nob_log(INFO, "%s: DISABLED", feature_flags[i].name);
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
        genf(f, "        nob_log(level, \"%s: ENABLED\");", feature_flags[i].name);
        genf(f, "    #else");
        genf(f, "        nob_log(level, \"%s: DISABLED\");", feature_flags[i].name);
        genf(f, "    #endif");
    }
    genf(f, "}");

    fclose(f);
    return true;
}
