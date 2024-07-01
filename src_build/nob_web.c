#define MUSIALIZER_TARGET_NAME "web"

bool build_musializer(void)
{
    bool result = true;
    Nob_Cmd cmd = {0};
    Nob_Procs procs = {0};

    nob_cmd_append(&cmd, "emcc");
    nob_cmd_append(&cmd, "-o", "./build/musializer.html");
    nob_cmd_append(&cmd, "-Os", "-Wall");
    nob_cmd_append(&cmd, "-I./raylib/raylib-"RAYLIB_VERSION"/src/");
    nob_cmd_append(&cmd, "-I.");
        nob_cmd_append(&cmd,
            "./src/plug.c",
            "./src/ffmpeg_linux.c",
            "./src/musializer.c");
    nob_cmd_append(&cmd,
        nob_temp_sprintf("-L./build/raylib/%s", MUSIALIZER_TARGET_NAME),
        "-l:libraylib.a");
    nob_cmd_append(&cmd, "-s", "USE_GLFW=3");
    nob_cmd_append(&cmd, "-s", "ASYNCIFY");
    nob_cmd_append(&cmd, "--shell-file", "src/shell.html");
    if (!nob_cmd_run_sync(cmd)) nob_return_defer(false);

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

    if (!nob_mkdir_if_not_exists("./build/raylib")) nob_return_defer(false);

    Nob_Procs procs = {0};

    const char *build_path = nob_temp_sprintf("./build/raylib/%s", MUSIALIZER_TARGET_NAME);

    if (!nob_mkdir_if_not_exists(build_path)) nob_return_defer(false);

    for (size_t i = 0; i < NOB_ARRAY_LEN(raylib_modules); ++i) {
        // Don't compile glfw for web because emscripten provides implementation
        if (strcmp("rglfw", raylib_modules[i]) == 0) continue;

        const char *input_path = nob_temp_sprintf("./raylib/raylib-"RAYLIB_VERSION"/src/%s.c", raylib_modules[i]);
        const char *output_path = nob_temp_sprintf("%s/%s.o", build_path, raylib_modules[i]);
        output_path = nob_temp_sprintf("%s/%s.o", build_path, raylib_modules[i]);

        nob_da_append(&object_files, output_path);

        if (nob_needs_rebuild(output_path, &input_path, 1)) {
            cmd.count = 0;
            nob_cmd_append(&cmd, "emcc");
            nob_cmd_append(&cmd, "-Os", "-w");
            nob_cmd_append(&cmd, "-DPLATFORM_WEB", "-DGRAPHICS_API_OPENGL_ES2");
            nob_cmd_append(&cmd, "-DSUPPORT_FILEFORMAT_FLAC=1");
            nob_cmd_append(&cmd, "-c", input_path);
            nob_cmd_append(&cmd, "-o", output_path);
            Nob_Proc proc = nob_cmd_run_async(cmd);
            nob_da_append(&procs, proc);
        }
    }
    if (!nob_procs_wait(procs)) nob_return_defer(false);

    cmd.count = 0;
    const char *libraylib_path = nob_temp_sprintf("%s/libraylib.a", build_path);
    if (nob_needs_rebuild(libraylib_path, object_files.items, object_files.count)) {
        nob_cmd_append(&cmd, "emar", "rsc", libraylib_path);
        for (size_t i = 0; i < NOB_ARRAY_LEN(raylib_modules); ++i) {
            // Don't compile glfw for web because emscripten provides implementation
            if (strcmp("rglfw", raylib_modules[i]) == 0) continue;
            const char *input_path = nob_temp_sprintf("%s/%s.o", build_path, raylib_modules[i]);
            nob_cmd_append(&cmd, input_path);

        }
        if (!nob_cmd_run_sync(cmd)) nob_return_defer(false);
    }

defer:
    nob_cmd_free(cmd);
    nob_da_free(object_files);
    return result;
}

bool build_dist()
{
    return false;
}
