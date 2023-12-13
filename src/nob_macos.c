// TODO: confirm that MacOS build works on MacOS
#define MUSIALIZER_TARGET_NAME "macos"

bool build_musializer(void)
{
    bool result = true;
    Nob_Cmd cmd = {0};
    Nob_Procs procs = {0};

#ifdef MUSIALIZER_HOTRELOAD
      cmd.count = 0;

        nob_cmd_append(&cmd, "clang");
        nob_cmd_append(&cmd, "-Wall", "-Wextra", "-g");
        nob_cmd_append(&cmd, "-I./build/");
        nob_cmd_append(&cmd, "-I./raylib/raylib-"RAYLIB_VERSION"/src/");

        nob_cmd_append(&cmd, "-dynamiclib");
        nob_cmd_append(&cmd, "-o", "./build/libplug.dylib");
        nob_cmd_append(&cmd,
            "./src/plug.c",
            "./src/ffmpeg_linux.c");
        nob_cmd_append(&cmd,
            nob_temp_sprintf("./build/raylib/%s/libraylib.dylib", MUSIALIZER_TARGET_NAME));

      if (!nob_cmd_run_sync(cmd)) nob_return_defer(false);

      cmd.count = 0;
        nob_cmd_append(&cmd, "clang");
        nob_cmd_append(&cmd, "-Wall", "-Wextra", "-g");
        nob_cmd_append(&cmd, "-I./build/");
        nob_cmd_append(&cmd, "-I./raylib/raylib-"RAYLIB_VERSION"/src/");

        nob_cmd_append(&cmd, "-o", "./build/musializer");
        nob_cmd_append(&cmd,
            "./src/musializer.c",
            "./src/hotreload_mac.c");
        nob_cmd_append(&cmd,
            "-Wl,-rpath,./build/",
            "-Wl,-rpath,./",
            nob_temp_sprintf("-Wl,-rpath,./build/raylib/%s/", MUSIALIZER_TARGET_NAME),
            // NOTE: just in case somebody wants to run musializer from within the ./build/ folder
            nob_temp_sprintf("-Wl,-rpath,./raylib/%s", MUSIALIZER_TARGET_NAME));
        nob_cmd_append(&cmd,
            nob_temp_sprintf("./build/raylib/%s/libraylib.dylib", MUSIALIZER_TARGET_NAME),
            "./build/libplug.dylib");
        nob_cmd_append(&cmd, "-lm", "-ldl", "-lpthread");
      if (!nob_cmd_run_sync(cmd)) nob_return_defer(false);
#else
    cmd.count = 0;
        nob_cmd_append(&cmd, "clang");
        nob_cmd_append(&cmd, "-Wall", "-Wextra", "-g");
        nob_cmd_append(&cmd, "-I./build/");
        nob_cmd_append(&cmd, "-I./raylib/raylib-"RAYLIB_VERSION"/src/");
        nob_cmd_append(&cmd, "-o", "./build/musializer");
        nob_cmd_append(&cmd,
            "./src/plug.c",
            "./src/ffmpeg_linux.c",
            "./src/musializer.c");
        nob_cmd_append(&cmd,
            nob_temp_sprintf("./build/raylib/%s/libraylib.a", MUSIALIZER_TARGET_NAME));

        nob_cmd_append(&cmd, "-framework", "CoreVideo");
        nob_cmd_append(&cmd, "-framework", "IOKit");
        nob_cmd_append(&cmd, "-framework", "Cocoa");
        nob_cmd_append(&cmd, "-framework", "GLUT");
        nob_cmd_append(&cmd, "-framework", "OpenGL");

        nob_cmd_append(&cmd, "-lm", "-ldl", "-lpthread");
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
        const char *output_path = nob_temp_sprintf("%s/%s.o", build_path, raylib_modules[i]);

        nob_da_append(&object_files, output_path);

        if (nob_needs_rebuild(output_path, &input_path, 1)) {
            cmd.count = 0;
            nob_cmd_append(&cmd, "clang");
            nob_cmd_append(&cmd, "-g", "-DPLATFORM_DESKTOP", "-fPIC");
            nob_cmd_append(&cmd, "-I./raylib/raylib-"RAYLIB_VERSION"/src/external/glfw/include");
            nob_cmd_append(&cmd, "-Iexternal/glfw/deps/ming");
            nob_cmd_append(&cmd, "-DGRAPHICS_API_OPENGL_33");
            if(strcmp(raylib_modules[i], "rglfw") == 0) {
                nob_cmd_append(&cmd, "-x", "objective-c");
            }
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
    const char *libraylib_path = nob_temp_sprintf("%s/libraylib.dylib", build_path);

    if (nob_needs_rebuild(libraylib_path, object_files.items, object_files.count)) {
        nob_cmd_append(&cmd, "clang");
        nob_cmd_append(&cmd, "-dynamiclib");
        nob_cmd_append(&cmd, "-framework", "CoreVideo");
        nob_cmd_append(&cmd, "-framework", "IOKit");
        nob_cmd_append(&cmd, "-framework", "Cocoa");
        nob_cmd_append(&cmd, "-framework", "GLUT");
        nob_cmd_append(&cmd, "-framework", "OpenGL");
        nob_cmd_append(&cmd, "-o", libraylib_path);
        for (size_t i = 0; i < NOB_ARRAY_LEN(raylib_modules); ++i) {
            const char *input_path = nob_temp_sprintf("%s/%s.o", build_path, raylib_modules[i]);
            nob_cmd_append(&cmd, input_path);
        }
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
    nob_log(NOB_ERROR, "TODO: Creating distro for MacOS build is not implemented yet");
    return false;
#endif // MUSIALIZER_HOTRELOAD
}
