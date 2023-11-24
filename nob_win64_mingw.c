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
            if (config.microphone) nob_cmd_append(&cmd, "-DFEATURE_MICROPHONE");
            nob_cmd_append(&cmd, "-I./raylib/raylib-"RAYLIB_VERSION"/src/");
            nob_cmd_append(&cmd, "-o", "./build/musializer");
            nob_cmd_append(&cmd,
                "./src/plug.c",
                "./src/ffmpeg_windows.c",
                "./src/musializer.c",
                "./build/musializer.res"
                );
            nob_cmd_append(&cmd,
                nob_temp_sprintf("-L./build/raylib/%s", NOB_ARRAY_GET(target_names, config.target)),
                "-l:libraylib.a");
            nob_cmd_append(&cmd, "-lwinmm", "-lgdi32");
            nob_cmd_append(&cmd, "-static");
        if (!nob_cmd_run_sync(cmd)) nob_return_defer(false);
    }

defer:
    nob_cmd_free(cmd);
    nob_da_free(procs);
    return result;
}
