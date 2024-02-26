// TODO: confirm that MSVC build works on Windows
#define MUSIALIZER_TARGET_NAME "win64-msvc"

bool build_musializer(void)
{
    bool result = true;
    Nob_Cmd cmd = {0};
    Nob_Procs procs = {0};

    cmd.count = 0;
        nob_cmd_append(&cmd, "rc");
        nob_cmd_append(&cmd, "/fo", "./build/musializer.res");
        nob_cmd_append(&cmd, "./src/musializer.rc");
        // NOTE: Do not change the order of commandline arguments to rc. Their argparser is weird.
    if (!nob_cmd_run_sync(cmd)) nob_return_defer(false);
#ifdef MUSIALIZER_HOTRELOAD
    procs.count = 0;
        cmd.count = 0;
            nob_cmd_append(&cmd, "cl.exe");
            nob_cmd_append(&cmd, "/LD");
            nob_cmd_append(&cmd, "/Fobuild\\", "/Fe./build/libplug.dll");
            nob_cmd_append(&cmd, "/I", "./build/");
            nob_cmd_append(&cmd, "/I", "./raylib/raylib-"RAYLIB_VERSION"/src/");
            nob_cmd_append(&cmd,
                "src/plug.c",
                "src/ffmpeg_windows.c");
            nob_cmd_append(&cmd,
                "/link",
                nob_temp_sprintf("/LIBPATH:build/raylib/%s", MUSIALIZER_TARGET_NAME),
                "raylib.lib");
            nob_cmd_append(&cmd, "Winmm.lib", "gdi32.lib", "User32.lib", "Shell32.lib");
        nob_da_append(&procs, nob_cmd_run_async(cmd));
            
        cmd.count = 0;
            nob_cmd_append(&cmd, "cl.exe");
            nob_cmd_append(&cmd, "/I", "./build/");
            nob_cmd_append(&cmd, "/I", "./raylib/raylib-"RAYLIB_VERSION"/src/");
            nob_cmd_append(&cmd, "/Fobuild\\", "/Febuild\\musializer.exe");
            nob_cmd_append(&cmd,
                "./src/musializer.c",
                "./src/hotreload_windows.c",
                );
            nob_cmd_append(&cmd,
                "/link",
                "/SUBSYSTEM:WINDOWS",
                "/entry:mainCRTStartup",
                nob_temp_sprintf("/LIBPATH:build/raylib/%s", MUSIALIZER_TARGET_NAME),
                "raylib.lib");
            nob_cmd_append(&cmd, "Winmm.lib", "gdi32.lib", "User32.lib", "Shell32.lib", "./build/musializer.res");
        nob_da_append(&procs, nob_cmd_run_async(cmd));
    if (!nob_procs_wait(procs)) nob_return_defer(false);
#else
    cmd.count = 0;
        nob_cmd_append(&cmd, "cl.exe");
        nob_cmd_append(&cmd, "/I", "./build/");
        nob_cmd_append(&cmd, "/I", "./raylib/raylib-"RAYLIB_VERSION"/src/");
        nob_cmd_append(&cmd, "/Fobuild\\", "/Febuild\\musializer.exe");
        nob_cmd_append(&cmd,
            "./src/musializer.c",
            "./src/plug.c",
            "./src/ffmpeg_windows.c");
        nob_cmd_append(&cmd,
            "/link",
            "/SUBSYSTEM:WINDOWS",
            "/entry:mainCRTStartup",
            nob_temp_sprintf("/LIBPATH:build/raylib/%s", MUSIALIZER_TARGET_NAME),
            "raylib.lib");
        nob_cmd_append(&cmd, "Winmm.lib", "gdi32.lib", "User32.lib", "Shell32.lib", "./build/musializer.res");
        // TODO: is some sort of `-static` flag needed for MSVC to get a statically linked executable
        //nob_cmd_append(&cmd, "-static");
    if (!nob_cmd_run_sync(cmd)) nob_return_defer(false);
#endif // MUSIALIZER_HOTRELOAD

defer:
    nob_cmd_free(cmd);
    nob_da_free(procs);
    return result;
}

bool build_raylib(void)
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
        const char *output_path = nob_temp_sprintf("%s/%s.obj", build_path, raylib_modules[i]);

        nob_da_append(&object_files, output_path);

        if (nob_needs_rebuild(output_path, &input_path, 1)) {
            cmd.count = 0;
            nob_cmd_append(&cmd, "cl.exe", "/DPLATFORM_DESKTOP", "/DSUPPORT_FILEFORMAT_FLAC=1");
            #ifdef MUSIALIZER_HOTRELOAD
                nob_cmd_append(&cmd, "/DBUILD_LIBTYPE_SHARED");
            #endif
            nob_cmd_append(&cmd, "/I", "./raylib/raylib-"RAYLIB_VERSION"/src/external/glfw/include");
            nob_cmd_append(&cmd, "/c", input_path);
            nob_cmd_append(&cmd, nob_temp_sprintf("/Fo%s", output_path));
            Nob_Proc proc = nob_cmd_run_async(cmd);
            nob_da_append(&procs, proc);
        }
    }
    cmd.count = 0;

    if (!nob_procs_wait(procs)) nob_return_defer(false);
#ifndef MUSIALIZER_HOTRELOAD
    const char *libraylib_path = nob_temp_sprintf("%s/raylib.lib", build_path);
    if (nob_needs_rebuild(libraylib_path, object_files.items, object_files.count)) {
        nob_cmd_append(&cmd, "lib");
        for (size_t i = 0; i < NOB_ARRAY_LEN(raylib_modules); ++i) {
            const char *input_path = nob_temp_sprintf("%s/%s.obj", build_path, raylib_modules[i]);
            nob_cmd_append(&cmd, input_path);
        }
        nob_cmd_append(&cmd, nob_temp_sprintf("/OUT:%s", libraylib_path));
        if (!nob_cmd_run_sync(cmd)) nob_return_defer(false);
    }
#else
    if (nob_needs_rebuild("./build/raylib.dll", object_files.items, object_files.count)) {
        nob_cmd_append(&cmd, "link.exe", "/DLL");
        for (size_t i = 0; i < NOB_ARRAY_LEN(raylib_modules); ++i) {
            const char *input_path = nob_temp_sprintf("%s/%s.obj", build_path, raylib_modules[i]);
            nob_cmd_append(&cmd, input_path);
        }
        nob_cmd_append(&cmd, "Winmm.lib", "gdi32.lib", "User32.lib", "Shell32.lib");
        nob_cmd_append(&cmd, nob_temp_sprintf("/IMPLIB:%s/raylib.lib", build_path));
        nob_cmd_append(&cmd, "/OUT:./build/raylib.dll");
        if (!nob_cmd_run_sync(cmd)) nob_return_defer(false);
    }
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
    nob_log(NOB_ERROR, "TODO: Creating distro for MSVC build is not implemented yet");
    return false;
#endif // MUSIALIZER_HOTRELOAD
}