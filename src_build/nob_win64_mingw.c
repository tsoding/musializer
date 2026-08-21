#define MUSIALIZER_TARGET_NAME "win64-mingw"

// On windows, mingw doesn't have the `x86_64-w64-mingw32-` prefix for tools such as `windres` or `ar`.
// For gcc, you can use both `x86_64-w64-mingw32-gcc` and just `gcc`
#ifdef _WIN32
#define MAYBE_PREFIXED(x) x
#else
#define MAYBE_PREFIXED(x) "x86_64-w64-mingw32-"x
#endif // _WIN32

bool build_musializer(void)
{
    bool result = true;
    Cmd cmd = {0};
    Procs procs = {0};

    cmd_append(&cmd, MAYBE_PREFIXED("windres"));
    cmd_append(&cmd, "./src/musializer.rc");
    cmd_append(&cmd, "-O", "coff");
    cmd_append(&cmd, "-o", "./build/musializer.res");
    if (!cmd_run(&cmd)) return_defer(false);

#ifdef MUSIALIZER_HOTRELOAD
    cmd_append(&cmd, MAYBE_PREFIXED("gcc"));
    cmd_append(&cmd, "-mwindows", "-Wall", "-Wextra", "-ggdb");
    cmd_append(&cmd, "-O3", "-ffast-math", "-flto=auto");
    cmd_append(&cmd, "-I.");
    cmd_append(&cmd, "-I"RAYLIB_SRC_FOLDER);
    cmd_append(&cmd, "-fPIC", "-shared");
    cmd_append(&cmd, "-static-libgcc");
    cmd_append(&cmd, "-o", "./build/libplug.dll");
    cmd_append(&cmd,
        "./src/plug.c",
        "./src/platform_windows.c",
        "./src/ffmpeg_windows.c",
        "./thirdparty/tinyfiledialogs.c");
    cmd_append(&cmd,
        "-L./build",
        "-l:raylib.dll");
    cmd_append(&cmd, "-lwinmm", "-lgdi32", "-lole32");
    if (!cmd_run(&cmd, .async = &procs)) return_defer(false);

    cmd_append(&cmd, MAYBE_PREFIXED("gcc"));
    cmd_append(&cmd, "-mwindows", "-Wall", "-Wextra", "-ggdb");
    cmd_append(&cmd, "-O3", "-ffast-math", "-flto=auto");
    cmd_append(&cmd, "-I.");
    cmd_append(&cmd, "-I"RAYLIB_SRC_FOLDER);
    cmd_append(&cmd, "-o", "./build/musializer");
    cmd_append(&cmd,
        "./src/musializer.c",
        "./src/hotreload_windows.c");
    cmd_append(&cmd,
        "-Wl,-rpath=./build/",
        "-Wl,-rpath=./",
        temp_sprintf("-Wl,-rpath=./build/raylib/%s", MUSIALIZER_TARGET_NAME),
        // NOTE: just in case somebody wants to run musializer from within the ./build/ folder
        temp_sprintf("-Wl,-rpath=./raylib/%s", MUSIALIZER_TARGET_NAME));
    cmd_append(&cmd,
        "-L./build",
        "-l:raylib.dll");
    cmd_append(&cmd, "-lwinmm", "-lgdi32");
    if (!cmd_run(&cmd, .async = &procs)) return_defer(false);

    if (!procs_flush(&procs)) return_defer(false);
#else
    cmd_append(&cmd, MAYBE_PREFIXED("gcc"));
    cmd_append(&cmd, "-mwindows", "-Wall", "-Wextra", "-ggdb");
    cmd_append(&cmd, "-O3", "-ffast-math", "-flto=auto");
    cmd_append(&cmd, "-I.");
    cmd_append(&cmd, "-I"RAYLIB_SRC_FOLDER);
    cmd_append(&cmd, "-o", "./build/musializer");
    cmd_append(&cmd,
        "./src/plug.c",
        "./src/platform_windows.c",
        "./src/ffmpeg_windows.c",
        "./src/musializer.c",
        "./thirdparty/tinyfiledialogs.c",
        "./build/musializer.res"
        );
    cmd_append(&cmd,
        temp_sprintf("-L./build/raylib/%s", MUSIALIZER_TARGET_NAME),
        "-l:libraylib.a");
    cmd_append(&cmd, "-lwinmm", "-lgdi32", "-lole32");
    cmd_append(&cmd, "-static");
    if (!cmd_run(&cmd)) return_defer(false);
#endif // MUSIALIZER_HOTRELOAD

defer:
    cmd_free(cmd);
    da_free(procs);
    return result;
}

bool build_raylib()
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
        output_path = temp_sprintf("%s/%s.o", build_path, raylib_modules[i]);

        da_append(&object_files, output_path);

        const char *inputs[] = {input_path, "./src_build/nob_win64_mingw.c"};
        if (needs_rebuild(output_path, inputs, ARRAY_LEN(inputs))) {
            cmd_append(&cmd, MAYBE_PREFIXED("gcc"));
            cmd_append(&cmd, "-ggdb", "-DPLATFORM_DESKTOP", "-fPIC", "-DSUPPORT_FILEFORMAT_FLAC=1");
            cmd_append(&cmd, "-O3", "-ffast-math", "-flto=auto");
            cmd_append(&cmd, "-DPLATFORM_DESKTOP");
            cmd_append(&cmd, "-fPIC");
            cmd_append(&cmd, "-I"RAYLIB_SRC_FOLDER"external/glfw/include");
            cmd_append(&cmd, "-c", input_path);
            cmd_append(&cmd, "-o", output_path);

            if (!cmd_run(&cmd, .async = &procs)) return_defer(false);
        }
    }

    if (!procs_flush(&procs)) return_defer(false);

#ifndef MUSIALIZER_HOTRELOAD
    const char *libraylib_path = temp_sprintf("%s/libraylib.a", build_path);

    if (needs_rebuild(libraylib_path, object_files.items, object_files.count)) {
        cmd_append(&cmd, MAYBE_PREFIXED("ar"));
        cmd_append(&cmd, "-crs", libraylib_path);
        for (size_t i = 0; i < ARRAY_LEN(raylib_modules); ++i) {
            const char *input_path = temp_sprintf("%s/%s.o", build_path, raylib_modules[i]);
            cmd_append(&cmd, input_path);
        }
        if (!cmd_run(&cmd)) return_defer(false);
    }
#else
    // it cannot load the raylib dll if it not in the same folder as the executable
    const char *libraylib_path = "./build/raylib.dll";

    if (needs_rebuild(libraylib_path, object_files.items, object_files.count)) {
        cmd_append(&cmd, MAYBE_PREFIXED("gcc"));
        cmd_append(&cmd, "-shared");
        cmd_append(&cmd, "-o", libraylib_path);
        for (size_t i = 0; i < ARRAY_LEN(raylib_modules); ++i) {
            const char *input_path = temp_sprintf("%s/%s.o", build_path, raylib_modules[i]);
            cmd_append(&cmd, input_path);
        }
        cmd_append(&cmd, "-lwinmm", "-lgdi32");
        if (!cmd_run(&cmd)) return_defer(false);
    }
#endif // MUSIALIZER_HOTRELOAD

defer:
    cmd_free(cmd);
    da_free(object_files);
    return result;
}

bool build_dist(void)
{
#ifdef MUSIALIZER_HOTRELOAD
    nob_log(ERROR, "We do not ship with hotreload enabled");
    return false;
#else
    if (!mkdir_if_not_exists("./musializer-win64-mingw/")) return false;
    if (!copy_file("./build/musializer.exe", "./musializer-win64-mingw/musializer.exe")) return false;
    if (!copy_directory_recursively("./resources/", "./musializer-win64-mingw/resources/")) return false;
    if (!copy_file("musializer-logged.bat", "./musializer-win64-mingw/musializer-logged.bat")) return false;
    // TODO: pack ffmpeg.exe with windows build
    //if (!copy_file("ffmpeg.exe", "./musializer-win64-mingw/ffmpeg.exe")) return false;
    Cmd cmd = {0};
    const char *dist_path = "./musializer-win64-mingw.zip";
    cmd_append(&cmd, "zip", "-r", dist_path, "./musializer-win64-mingw/");
    bool ok = cmd_run(&cmd);
    cmd_free(cmd);
    if (!ok) return false;
    nob_log(INFO, "Created %s", dist_path);
    return true;
#endif // MUSIALIZER_HOTRELOAD
}
