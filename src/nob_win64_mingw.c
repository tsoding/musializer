#define MUSIALIZER_TARGET_NAME "win64-mingw"

bool build_musializer(void)
{
    bool result = true;
    Nob_Cmd cmd = {0};
    Nob_Procs procs = {0};

#ifdef MUSIALIZER_HOTRELOAD
    cmd.count = 0;
    #ifdef _WIN32
        // On windows, mingw doesn't have the `x86_64-w64-mingw32-` prefix for windres.
        // For gcc, you can use both `x86_64-w64-mingw32-gcc` and just `gcc`
        nob_cmd_append(&cmd, "windres");
    #else
        nob_cmd_append(&cmd, "x86_64-w64-mingw32-gcc");
    #endif // _WIN32
        nob_cmd_append(&cmd, "./src/musializer.rc");
        nob_cmd_append(&cmd, "-O", "coff");
        nob_cmd_append(&cmd, "-o", "./build/musializer.res");
    if (!nob_cmd_run_sync(cmd)) nob_return_defer(false);

    cmd.count = 0;
    // TODO: add a way to replace `cc` with something else GCC compatible on POSIX
    // Like `clang` for instance
    nob_cmd_append(&cmd, "x86_64-w64-mingw32-gcc");
    nob_cmd_append(&cmd, "-Wall", "-Wextra", "-ggdb");
    nob_cmd_append(&cmd, "-I./build/");
    nob_cmd_append(&cmd, "-I./raylib/raylib-"RAYLIB_VERSION"/src/");
    nob_cmd_append(&cmd, "-fPIC", "-shared");
    nob_cmd_append(&cmd, "-o", "./build/libplug.lib");
    nob_cmd_append(&cmd,
        "./src/plug.c",
        "./src/ffmpeg_windows.c");
    nob_cmd_append(&cmd,
        nob_temp_sprintf("-L./build/raylib/%s", MUSIALIZER_TARGET_NAME));
    nob_cmd_append(&cmd, "-lm", "-lpthread", "-lwinmm", "-lgdi32","-l:raylib.dll");
    nob_da_append(&procs, nob_cmd_run_async(cmd));

    cmd.count = 0;
    nob_cmd_append(&cmd, "x86_64-w64-mingw32-gcc");
    nob_cmd_append(&cmd, "-Wall", "-Wextra", "-ggdb");
    nob_cmd_append(&cmd, "-I./build/");
    nob_cmd_append(&cmd, "-I./src/");
    nob_cmd_append(&cmd, "-I./raylib/raylib-"RAYLIB_VERSION"/src/");
    nob_cmd_append(&cmd, "-o", "./build/musializer");
    nob_cmd_append(&cmd,
        "./src/musializer.c",
        "./build/musializer.res",
        "./src/hotreload_posix.c",
        "./src/dlfcn.c");
    nob_cmd_append(&cmd,
        "-Wl,-rpath=./build/",
        "-Wl,-rpath=./",
        nob_temp_sprintf("-Wl,-rpath=./build/raylib/%s", MUSIALIZER_TARGET_NAME),
        // NOTE: just in case somebody wants to run musializer from within the ./build/ folder
        nob_temp_sprintf("-Wl,-rpath=./raylib/%s", MUSIALIZER_TARGET_NAME));
    nob_cmd_append(&cmd,
        nob_temp_sprintf("-L./build/raylib/%s", MUSIALIZER_TARGET_NAME));
    nob_cmd_append(&cmd, "-lm", "-lpthread", "-lwinmm", "-lgdi32", "-l:raylib.dll");
    if (!nob_cmd_run_sync(cmd)) nob_return_defer(false);
#else
    cmd.count = 0;
    #ifdef _WIN32
        // On windows, mingw doesn't have the `x86_64-w64-mingw32-` prefix for windres.
        // For gcc, you can use both `x86_64-w64-mingw32-gcc` and just `gcc`
        nob_cmd_append(&cmd, "windres");
    #else
        nob_cmd_append(&cmd, "x86_64-w64-mingw32-gcc");
    #endif // _WIN32
        nob_cmd_append(&cmd, "./src/musializer.rc");
        nob_cmd_append(&cmd, "-O", "coff");
        nob_cmd_append(&cmd, "-o", "./build/musializer.res");
    if (!nob_cmd_run_sync(cmd)) nob_return_defer(false);

    cmd.count = 0;
        nob_cmd_append(&cmd, "x86_64-w64-mingw32-gcc");
        nob_cmd_append(&cmd, "-mwindows", "-Wall", "-Wextra", "-ggdb");
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
    //./raylib.dll.rc.data  -L. -L../src -Wl,--out-implib,../src/libraylibdll.a -static-libgcc -lopengl32 -lgdi32 -lwinmm
    const char *libraylib_path = nob_temp_sprintf("%s/raylib.dll", build_path);
    if (nob_needs_rebuild(libraylib_path, object_files.items, object_files.count)) {
        nob_cmd_append(&cmd, "x86_64-w64-mingw32-gcc", "-shared", "-o", libraylib_path);
        for (size_t i = 0; i < NOB_ARRAY_LEN(raylib_modules); ++i) {
            const char *input_path = nob_temp_sprintf("%s/%s.o", build_path, raylib_modules[i]);
            nob_cmd_append(&cmd, input_path);
        }
        const char *rc_data = nob_temp_sprintf("./src/raylib.dll.rc.data", libraylib_path);
        nob_cmd_append(&cmd, rc_data);
        nob_cmd_append(&cmd, "-static-libgcc", "-lopengl32", "-lgdi32", "-lwinmm");
        if (!nob_cmd_run_sync(cmd)) nob_return_defer(false);
    }
    nob_copy_file(libraylib_path, "./build/raylib.dll");
#endif // MUSIALIZER_HOTRELOAD

defer:
    nob_cmd_free(cmd);
    nob_da_free(object_files);
    return result;
}

bool build_dist(void)
{
#ifdef MUSIALIZER_HOTRELOAD
    nob_log(NOB_INFO, "Hot reload is finally supported");
    return true;
#else
    if (!nob_mkdir_if_not_exists("./musializer-win64-mingw/")) return false;
    if (!nob_copy_file("./build/musializer.exe", "./musializer-win64-mingw/musializer.exe")) return false;
    if (!nob_copy_directory_recursively("./resources/", "./musializer-win64-mingw/resources/")) return false;
    if (!nob_copy_file("musializer-logged.bat", "./musializer-win64-mingw/musializer-logged.bat")) return false;
    // TODO: pack ffmpeg.exe with windows build
    //if (!nob_copy_file("ffmpeg.exe", "./musializer-win64-mingw/ffmpeg.exe")) return false;
    Nob_Cmd cmd = {0};
    const char *dist_path = "./musializer-win64-mingw.zip";
    nob_cmd_append(&cmd, "zip", "-r", dist_path, "./musializer-win64-mingw/");
    bool ok = nob_cmd_run_sync(cmd);
    nob_cmd_free(cmd);
    if (!ok) return false;
    nob_log(NOB_INFO, "Created %s", dist_path);
    return true;
#endif // MUSIALIZER_HOTRELOAD
}
