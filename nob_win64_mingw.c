#define MUSIALIZER_TARGET_NAME "win64-mingw"

bool build_musializer(void)
{
    bool result = true;
    Nob_Cmd cmd = {0};
    Nob_Procs procs = {0};

#ifdef MUSIALIZER_HOTRELOAD
    nob_log(NOB_ERROR, "TODO: hotreloading is not supported on %s yet", NOB_ARRAY_GET(target_names, config.target));
    nob_return_defer(false);
#else
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
        nob_cmd_append(&cmd, "-I./build/");
        nob_cmd_append(&cmd, "-I./raylib/raylib-"RAYLIB_VERSION"/src/");
        nob_cmd_append(&cmd, "-o", "./build/musializer");
        nob_cmd_append(&cmd,
            "./src/plug.c",
            "./src/ffmpeg_windows.c",
            "./src/musializer.c",
            "./build/musializer.res"
            );
        nob_cmd_append(&cmd,
            nob_temp_sprintf("-L./build/raylib/%s", MUSIALIZER_TARGET_NAME),
            "-l:libraylib.a");
        nob_cmd_append(&cmd, "-lwinmm", "-lgdi32");
        nob_cmd_append(&cmd, "-static");
    if (!nob_cmd_run_sync(cmd)) nob_return_defer(false);
#endif // MUSIALIZER_HOTRELOAD

defer:
    nob_cmd_free(cmd);
    nob_da_free(procs);
    return result;
}

bool build_raylib()
{
    bool result = true;
    Nob_Cmd cmd = {0};
    Nob_File_Paths object_files = {0};

    if (!nob_mkdir_if_not_exists("./build/raylib")) {
        nob_return_defer(false);
    }

    Nob_Procs procs = {0};

    const char *build_path = nob_temp_sprintf("./build/raylib/%s", MUSIALIZER_TARGET_NAME);

    if (!nob_mkdir_if_not_exists(build_path)) {
        nob_return_defer(false);
    }

    for (size_t i = 0; i < NOB_ARRAY_LEN(raylib_modules); ++i) {
        const char *input_path = nob_temp_sprintf("./raylib/raylib-"RAYLIB_VERSION"/src/%s.c", raylib_modules[i]);
        const char *output_path = nob_temp_sprintf("%s/%s.o", build_path, raylib_modules[i]);
        output_path = nob_temp_sprintf("%s/%s.o", build_path, raylib_modules[i]);

        nob_da_append(&object_files, output_path);

        if (nob_needs_rebuild(output_path, &input_path, 1)) {
            cmd.count = 0;
            nob_cmd_append(&cmd, "x86_64-w64-mingw32-gcc");
            nob_cmd_append(&cmd, "-ggdb", "-DPLATFORM_DESKTOP", "-fPIC");
            nob_cmd_append(&cmd, "-DPLATFORM_DESKTOP");
            nob_cmd_append(&cmd, "-fPIC");
            nob_cmd_append(&cmd, "-I./raylib/raylib-"RAYLIB_VERSION"/src/external/glfw/include");
            nob_cmd_append(&cmd, "-c", input_path);
            nob_cmd_append(&cmd, "-o", output_path);

            Nob_Proc proc = nob_cmd_run_async(cmd);
            nob_da_append(&procs, proc);
        }
    }
    cmd.count = 0;

    if (!nob_procs_wait(procs)) nob_return_defer(false);

#ifndef MUSIALIZER_HOTRELOAD
    const char *libraylib_path = nob_temp_sprintf("%s/libraylib.a", build_path);

    if (nob_needs_rebuild(libraylib_path, object_files.items, object_files.count)) {
        nob_cmd_append(&cmd, "ar", "-crs", libraylib_path);
        for (size_t i = 0; i < NOB_ARRAY_LEN(raylib_modules); ++i) {
            const char *input_path = nob_temp_sprintf("%s/%s.o", build_path, raylib_modules[i]);
            nob_cmd_append(&cmd, input_path);
        }
        if (!nob_cmd_run_sync(cmd)) nob_return_defer(false);
    }
#else
#error "TODO: dynamic raylib is not supported for TARGET_WIN64_MINGW"
#endif // MUSIALIZER_HOTRELOAD

defer:
    nob_cmd_free(cmd);
    nob_da_free(object_files);
    return result;
}

bool build_dist(void)
{
#ifdef MUSIALIZER_HOTRELOAD
    nob_log(NOB_ERROR, "We do not ship with hotreload enabled");
    return false;
#else
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
    return true;
#endif // MUSIALIZER_HOTRELOAD
}
