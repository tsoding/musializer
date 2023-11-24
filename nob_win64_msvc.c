bool build_musializer(void)
{
    bool result = true;
    Nob_Cmd cmd = {0};
    Nob_Procs procs = {0};

    if (config.hotreload) {
        nob_log(NOB_ERROR, "TODO: hotreloading is not supported on %s yet", NOB_ARRAY_GET(target_names, config.target));
        nob_return_defer(false);
    } else {
        cmd.count = 0;
            nob_cmd_append(&cmd, "rc");
            nob_cmd_append(&cmd, "/fo", "./build/musializer.res");
            nob_cmd_append(&cmd, "./src/musializer.rc");
            // NOTE: Do not change the order of commandline arguments to rc. Their argparser is weird.
        if (!nob_cmd_run_sync(cmd)) nob_return_defer(false);
        cmd.count = 0;
            nob_cmd_append(&cmd, "cl.exe");
            if (config.microphone) nob_cmd_append(&cmd, "/DFEATURE_MICROPHONE");
            nob_cmd_append(&cmd, "/I", "./raylib/raylib-"RAYLIB_VERSION"/src/");
            nob_cmd_append(&cmd, "/Fobuild\\", "/Febuild\\musializer.exe");
            nob_cmd_append(&cmd,
                "./src/musializer.c",
                "./src/plug.c",
                "./src/ffmpeg_windows.c"
                // TODO: building resource file is not implemented for TARGET_WIN64_MSVC
                );
            nob_cmd_append(&cmd,
                "/link",
                nob_temp_sprintf("/LIBPATH:build/raylib/%s", NOB_ARRAY_GET(target_names, config.target)),
                "raylib.lib");
            nob_cmd_append(&cmd, "Winmm.lib", "gdi32.lib", "User32.lib", "Shell32.lib", "./build/musializer.res");
            // TODO: is some sort of `-static` flag needed for MSVC to get a statically linked executable
            //nob_cmd_append(&cmd, "-static");
        if (!nob_cmd_run_sync(cmd)) nob_return_defer(false);
    }

defer:
    nob_cmd_free(cmd);
    nob_da_free(procs);
    return result;
}
