// TODO: confirm that MacOS build works on MacOS
#include <stddef.h>
#define MUSIALIZER_TARGET_NAME "macos"

bool build_musializer(void)
{
    bool result = true;
    Nob_Cmd cmd = {0};
    Nob_Procs procs = {0};

#ifdef MUSIALIZER_HOTRELOAD
    procs.count = 0;
    cmd.count = 0;
        // TODO: add a way to replace `cc` with something else GCC compatible on POSIX
        // Like `clang` for instance
        nob_cmd_append(&cmd, "clang");
        nob_cmd_append(&cmd, "-Wall", "-Wextra", "-g");
        nob_cmd_append(&cmd, "-I.");
        nob_cmd_append(&cmd, "-I./thirdparty/raylib-"RAYLIB_VERSION"/src/");
        nob_cmd_append(&cmd, "-fPIC", "-shared");
        nob_cmd_append(&cmd, "-o", "./build/libplug.dylib");
        nob_cmd_append(&cmd,
            "./src/plug.c",
            "./src/ffmpeg_posix.c",
            "./thirdparty/tinyfiledialogs.c");
        nob_cmd_append(&cmd, "./build/raylib/macos/libraylib.dylib");
        nob_cmd_append(&cmd, "-lm", "-ldl", "-lpthread");
    nob_da_append(&procs, nob_cmd_run_async(cmd));

    cmd.count = 0;
        nob_cmd_append(&cmd, "clang");
        nob_cmd_append(&cmd, "-Wall", "-Wextra", "-g");
        nob_cmd_append(&cmd, "-I.");
        nob_cmd_append(&cmd, "-I./thirdparty/raylib-"RAYLIB_VERSION"/src/");
        nob_cmd_append(&cmd, "-o", "./build/musializer");
        nob_cmd_append(&cmd,
            "./src/musializer.c",
            "./src/hotreload_posix.c");
        nob_cmd_append(&cmd, "./build/raylib/macos/libraylib.dylib");
        nob_cmd_append(&cmd, "-lm", "-ldl", "-lpthread");
        nob_cmd_append(&cmd, "-rpath", "./build");
        nob_cmd_append(&cmd, "-rpath", "./build/raylib/macos");
        nob_cmd_append(&cmd, "-rpath", "./");
        nob_cmd_append(&cmd, "-rpath", "./raylib/macos");
    nob_da_append(&procs, nob_cmd_run_async(cmd));
    if (!nob_procs_wait(procs)) nob_return_defer(false);
#else
    cmd.count = 0;
        nob_cmd_append(&cmd, "clang");
        nob_cmd_append(&cmd, "-Wall", "-Wextra", "-g");
        nob_cmd_append(&cmd, "-I.");
        nob_cmd_append(&cmd, "-I./thirdparty/raylib-"RAYLIB_VERSION"/src/");
        nob_cmd_append(&cmd, "-o", "./build/musializer");
        nob_cmd_append(&cmd,
            "./src/plug.c",
            "./src/ffmpeg_posix.c",
            "./src/musializer.c",
            "./thirdparty/tinyfiledialogs.c");
        nob_cmd_append(&cmd, "./build/raylib/macos/libraylib.a");

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
        const char *input_path = nob_temp_sprintf("./thirdparty/raylib-"RAYLIB_VERSION"/src/%s.c", raylib_modules[i]);
        const char *output_path = nob_temp_sprintf("%s/%s.o", build_path, raylib_modules[i]);

        nob_da_append(&object_files, output_path);

        if (nob_needs_rebuild(output_path, &input_path, 1)) {
            cmd.count = 0;
            nob_cmd_append(&cmd, "clang");
            nob_cmd_append(&cmd, "-g", "-DPLATFORM_DESKTOP", "-fPIC", "-DSUPPORT_FILEFORMAT_FLAC=1");
            nob_cmd_append(&cmd, "-I./thirdparty/raylib-"RAYLIB_VERSION"/src/external/glfw/include");
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
        nob_cmd_append(&cmd, "cc");
        nob_cmd_append(&cmd, "-shared", "-fPIC");
        nob_cmd_append(&cmd, "-framework", "CoreVideo");
        nob_cmd_append(&cmd, "-framework", "IOKit");
        nob_cmd_append(&cmd, "-framework", "Cocoa");
        nob_cmd_append(&cmd, "-framework", "GLUT");
        nob_cmd_append(&cmd, "-framework", "OpenGL");
        nob_cmd_append(&cmd, "-install_name", "@rpath/libraylib.dylib");
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
#if defined(MUSIALIZER_HOTRELOAD)
    nob_log(NOB_ERROR, "We do not ship with hotreload enabled");
    return false;
#elif defined(MUSIALIZER_UNBUNDLE)
    nob_log(NOB_ERROR, "We do not ship with unbundled resources");
    return false;
#else
    if (!nob_mkdir_if_not_exists("./build/Musializer.app/")) return false;
    if (!nob_mkdir_if_not_exists("./build/Musializer.app/Contents")) return false;
    if (!nob_mkdir_if_not_exists("./build/Musializer.app/Contents/MacOS")) return false;
    if (!nob_mkdir_if_not_exists("./build/Musializer.app/Contents/Resources")) return false;
    if (!nob_mkdir_if_not_exists("./build/AppIcon.iconset")) return false;

    if (!nob_copy_file("./build/musializer", "./build/Musializer.app/Contents/MacOS/Musializer")) return false;
    if (!nob_copy_file("./src_build/Info.plist", "./build/Musializer.app/Contents/Info.plist")) return false;

    typedef struct {
        const char *out_path;
        int size;
    } Icon;
    Icon icons[] = {
        {.out_path = "icon_16x16.png", .size = 16},
        {.out_path = "icon_16x16@2x.png", .size = 32},
        {.out_path = "icon_32x32.png", .size = 32},
        {.out_path = "icon_32x32@2x.png", .size = 64},
        {.out_path = "icon_128x128.png", .size = 128},
        {.out_path = "icon_128x128@2x.png", .size = 256},
        {.out_path = "icon_256x256.png", .size = 256},
        {.out_path = "icon_256x256@2x.png", .size = 512},
        {.out_path = "icon_512x512.png", .size = 512},
        {.out_path = "icon_512x512@2x.png", .size = 1024},
    };

    Nob_Procs procs = {0};
    Nob_Cmd cmd = {0};
    for (size_t i=0; i<NOB_ARRAY_LEN(icons); ++i) {
        char* icon = nob_temp_sprintf("./build/AppIcon.iconset/%s", icons[i].out_path);
        if (nob_needs_rebuild1(icon, "./resources/logo/logo.svg")) {
            cmd.count = 0;
                nob_cmd_append(&cmd, "convert",
                    "-background", "none",
                    "./resources/logo/logo.svg",
                    "-resize", nob_temp_sprintf("%dx%d", icons[i].size, icons[i].size),
                    icon);
            nob_da_append(&procs, nob_cmd_run_async(cmd));
        } else {
            nob_log(NOB_INFO, "%s is up to date", icon);
        }
    }
    if (!nob_procs_wait(procs)) return false;
    cmd.count = 0;
        nob_cmd_append(&cmd, "iconutil", "-c", "icns",
                        "-o", "./build/Musializer.app/Contents/Resources/AppIcon.icns",
                        "./build/AppIcon.iconset");
    if (!nob_cmd_run_sync(cmd)) return false;
    return true;
#endif // MUSIALIZER_HOTRELOAD
}
