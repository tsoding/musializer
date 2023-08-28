#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <complex.h>
#include <math.h>
#include <errno.h>

#include <raylib.h>

#include <unistd.h>
#include <fcntl.h>
#include <link.h>
#include <dlfcn.h>
#ifdef __linux__
#include <sys/inotify.h>
#endif

#include "plug.h"

#define ARRAY_LEN(xs) sizeof(xs)/sizeof(xs[0])

const char *libplug_file_name = "libplug.so";
struct link_map *libplug_info = NULL;
void *libplug = NULL;

int libplug_watch_fd = -1;
int libplug_watch_wd = -1;

#ifdef HOTRELOAD
#define PLUG(name, ...) name##_t *name = NULL;
#else
#define PLUG(name, ...) name##_t name;
#endif
LIST_OF_PLUGS
#undef PLUG

#ifdef HOTRELOAD
bool reload_libplug(void)
{
    if (libplug != NULL) dlclose(libplug);

    libplug = dlopen(libplug_file_name, RTLD_NOW);
    if (libplug == NULL) {
        fprintf(stderr, "ERROR: could not load %s: %s\n", libplug_file_name, dlerror());
        return false;
    }

    #ifdef AUTORELOAD
    if (dlinfo(libplug, RTLD_DI_LINKMAP, &libplug_info) < 0) {
        fprintf(stderr, "ERROR: could not get info for %s: %s\n", libplug_file_name, dlerror());
        return false;
    }
    #endif

    #define PLUG(name, ...) \
        name = dlsym(libplug, #name); \
        if (name == NULL) { \
            fprintf(stderr, "ERROR: could not find %s symbol in %s: %s\n", \
                    #name, libplug_file_name, dlerror()); \
            return false; \
        }
    LIST_OF_PLUGS
    #undef PLUG

    return true;
}
#else
#define reload_libplug() true
#endif

#ifdef AUTORELOAD
#ifndef __linux__
#error "ERROR: autoreloading is not supported on your system"
#endif
bool reload_watch(void) {
    if (libplug_watch_fd < 0) libplug_watch_fd = inotify_init1(IN_NONBLOCK);
    if (libplug_watch_fd < 0) {
        fprintf(stderr, "ERROR: could not initialize inotify");
        return false;
    }

    libplug_watch_wd = inotify_add_watch(libplug_watch_fd, libplug_info->l_name, IN_ALL_EVENTS);
    if (libplug_watch_wd < 0)
        fprintf(stderr, "WARNING: could not add watch to %s: %s\n", libplug_info->l_name, strerror(errno));
    return true;
}

bool libplug_needs_reload(void) {
    if (libplug_watch_wd < 0) reload_watch();
    bool needs_reload = false;
    size_t buflen = sizeof(struct inotify_event) * 16;
    char buf[buflen];
    int len = read(libplug_watch_fd, buf, buflen);
    for (int i = 0; i < len; i += (sizeof(struct inotify_event))) {
        struct inotify_event *event = (struct inotify_event*)&buf[i];
        // for some reason, when libplug is recompiled it will generate a single IN_ATTRIB event
        // and then stop sending future events, which is why we reload under this condition.
        if (event->mask & (IN_IGNORED | IN_ATTRIB)) reload_watch();
        // reload only after close because tracking all modification could result in loading a partially-compiled file
        if (event->mask & IN_CLOSE_WRITE) needs_reload = true;
    }
    return needs_reload;
}
#else
#define reload_watch() true
#define libplug_needs_reload() false
#endif

int main(void)
{
    if (!reload_libplug()) return 1;
    if (!reload_watch()) return 1;

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    size_t factor = 60;
    InitWindow(factor*16, factor*9, "Musializer");
    SetTargetFPS(60);
    InitAudioDevice();

    plug_init();
    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_R) || libplug_needs_reload()) {
            void *state = plug_pre_reload();
            if (!reload_libplug()) return 1;
            plug_post_reload(state);
        }
        plug_update();
    }

    return 0;
}
