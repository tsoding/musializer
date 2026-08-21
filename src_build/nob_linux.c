#define MUSIALIZER_TARGET_NAME "linux"

bool build_musializer(void)
{
    bool result = true;
    Cmd cmd = {0};
    Procs procs = {0};

#ifdef MUSIALIZER_HOTRELOAD
    // TODO: add a way to replace `cc` with something else GCC compatible on POSIX
    // Like `clang` for instance
    cmd_append(&cmd, "cc",
        "-Wall", "-Wextra", "-ggdb",
        "-I.", "-I"RAYLIB_SRC_FOLDER,
        "-fPIC", "-shared",
        "-o", "./build/libplug.so",
        "./src/plug.c", "./src/platform_posix.c", "./src/ffmpeg_posix.c", "./thirdparty/tinyfiledialogs.c",
        temp_sprintf("-L./build/raylib/%s", MUSIALIZER_TARGET_NAME), "-l:libraylib.so",
        "-O3", "-march=native", "-ffast-math",
        "-lm", "-ldl", "-flto=auto", "-pthread");
    if (!cmd_run(&cmd)) return_defer(false);

    cmd_append(&cmd, "cc",
        "-Wall", "-Wextra", "-ggdb",
        "-I.", "-I"RAYLIB_SRC_FOLDER,
        "-o", "./build/musializer",
        "./src/musializer.c", "./src/hotreload_posix.c",
        "-Wl,-rpath=./build/",
        "-Wl,-rpath=./",
        temp_sprintf("-Wl,-rpath=./build/raylib/%s", MUSIALIZER_TARGET_NAME),
        // NOTE: just in case somebody wants to run musializer from within the ./build/ folder
        temp_sprintf("-Wl,-rpath=./raylib/%s", MUSIALIZER_TARGET_NAME),
        temp_sprintf("-L./build/raylib/%s", MUSIALIZER_TARGET_NAME),
        "-O3", "-march=native", "-ffast-math",
        "-l:libraylib.so", "-lm", "-ldl", "-flto=auto", "-pthread");
    if (!cmd_run(&cmd)) return_defer(false);

    if (!procs_flush(&procs)) return_defer(false);
#else
    cmd_append(&cmd, "cc",
        "-Wall", "-Wextra", "-ggdb",
        "-I.",
        "-I"RAYLIB_SRC_FOLDER,
        "-o", "./build/musializer",
        "./src/plug.c", "./src/platform_posix.c", "./src/ffmpeg_posix.c", "./src/musializer.c", "./thirdparty/tinyfiledialogs.c",
        temp_sprintf("-L./build/raylib/%s", MUSIALIZER_TARGET_NAME), "-l:libraylib.a",
        "-O3", "-march=native", "-ffast-math",
        "-lm", "-ldl", "-flto=auto", "-pthread");
    if (!cmd_run(&cmd)) return_defer(false);
#endif // MUSIALIZER_HOTRELOAD

defer:
    cmd_free(cmd);
    da_free(procs);
    return result;
}

bool build_raylib(void)
{
    bool result = true;
    Cmd cmd = {0};
    File_Paths object_files = {0};

    if (!mkdir_if_not_exists("./build/raylib")) {
        return_defer(false);
    }

    Procs procs = {0};

    const char *build_path = temp_sprintf("./build/raylib/%s", MUSIALIZER_TARGET_NAME);

    if (!mkdir_if_not_exists(build_path)) {
        return_defer(false);
    }

    for (size_t i = 0; i < ARRAY_LEN(raylib_modules); ++i) {
        const char *input_path = temp_sprintf(RAYLIB_SRC_FOLDER"%s.c", raylib_modules[i]);
        const char *output_path = temp_sprintf("%s/%s.o", build_path, raylib_modules[i]);

        da_append(&object_files, output_path);

        const char *inputs[] = { input_path, "./src_build/nob_linux.c" };
        if (needs_rebuild(output_path, inputs, ARRAY_LEN(inputs))) {
            cmd_append(&cmd, "cc",
                "-ggdb", "-DPLATFORM_DESKTOP", "-D_GLFW_X11", "-fPIC", "-DSUPPORT_FILEFORMAT_FLAC=1",
                "-I"RAYLIB_SRC_FOLDER"external/glfw/include",
                "-O3", "-march=native", "-ffast-math", "-flto=auto", "-pthread",
                "-c", input_path,
                "-o", output_path);
            if (!cmd_run(&cmd, .async = &procs)) return_defer(false);
        }
    }

    if (!procs_flush(&procs)) return_defer(false);

#ifndef MUSIALIZER_HOTRELOAD
    const char *libraylib_path = temp_sprintf("%s/libraylib.a", build_path);

    if (needs_rebuild(libraylib_path, object_files.items, object_files.count)) {
        cmd_append(&cmd, "ar", "-crs", libraylib_path);
        for (size_t i = 0; i < ARRAY_LEN(raylib_modules); ++i) {
            const char *input_path = temp_sprintf("%s/%s.o", build_path, raylib_modules[i]);
            cmd_append(&cmd, input_path);
        }
        if (!cmd_run(&cmd)) return_defer(false);
    }
#else
    const char *libraylib_path = temp_sprintf("%s/libraylib.so", build_path);

    if (needs_rebuild(libraylib_path, object_files.items, object_files.count)) {
        cmd_append(&cmd, "cc", "-shared", "-flto=auto", "-pthread", "-o", libraylib_path);
        for (size_t i = 0; i < ARRAY_LEN(raylib_modules); ++i) {
            const char *input_path = temp_sprintf("%s/%s.o", build_path, raylib_modules[i]);
            cmd_append(&cmd, input_path);
        }
        if (!cmd_run(&cmd)) return_defer(false);
    }
#endif // MUSIALIZER_HOTRELOAD

defer:
    cmd_free(cmd);
    da_free(object_files);
    return result;
}

bool build_dist()
{
#ifdef MUSIALIZER_HOTRELOAD
    nob_log(ERROR, "We do not ship with hotreload enabled");
    return false;
#else
    if (!mkdir_if_not_exists("./musializer-linux-x86_64/")) return false;
    if (!copy_file("./build/musializer", "./musializer-linux-x86_64/musializer")) return false;
    if (!copy_directory_recursively("./resources/", "./musializer-linux-x86_64/resources/")) return false;
    // TODO: should we pack ffmpeg with Linux build?
    // There are some static executables for Linux
    Cmd cmd = {0};
    cmd_append(&cmd, "tar", "fvcz", "./musializer-linux-x86_64.tar.gz", "./musializer-linux-x86_64");
    bool ok = cmd_run(&cmd);
    cmd_free(cmd);
    if (!ok) return false;

    return true;
#endif // MUSIALIZER_HOTRELOAD
}
