bool build_musializer(void)
{
    bool result = true;
    Nob_Cmd cmd = {0};
    Nob_Procs procs = {0};

    if (config.hotreload) {
        nob_log(NOB_ERROR, "TODO: hotreloading is not supported on %s yet", NOB_ARRAY_GET(target_names, config.target));
        nob_return_defer(false);
    }

    cmd.count = 0;
        nob_cmd_append(&cmd, "clang");
        nob_cmd_append(&cmd, "-Wall", "-Wextra", "-g");
        if (config.microphone) nob_cmd_append(&cmd, "-DFEATURE_MICROPHONE");
        nob_cmd_append(&cmd, "-I./raylib/raylib-"RAYLIB_VERSION"/src/");
        nob_cmd_append(&cmd, "-o", "./build/musializer");
        nob_cmd_append(&cmd,
            "./src/plug.c",
            "./src/ffmpeg_linux.c",
            "./src/musializer.c");
        nob_cmd_append(&cmd,
            nob_temp_sprintf("./build/raylib/%s/libraylib.a", NOB_ARRAY_GET(target_names, config.target)));

        nob_cmd_append(&cmd, "-framework", "CoreVideo");
        nob_cmd_append(&cmd, "-framework", "IOKit");
        nob_cmd_append(&cmd, "-framework", "Cocoa");
        nob_cmd_append(&cmd, "-framework", "GLUT");
        nob_cmd_append(&cmd, "-framework", "OpenGL");

        nob_cmd_append(&cmd, "-lm", "-ldl", "-lpthread");
    if (!nob_cmd_run_sync(cmd)) nob_return_defer(false);

defer:
    nob_cmd_free(cmd);
    nob_da_free(procs);
    return result;
}
