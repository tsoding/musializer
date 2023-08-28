#define _GNU_SOURCE
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
#include <sys/inotify.h>

#include "plug.h"

#define ARRAY_LEN(xs) sizeof(xs)/sizeof(xs[0])

const char *libplug_file_name = "libplug.so";
struct link_map *libplug_info = NULL;
void *libplug = NULL;

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
    if (dlinfo(libplug, RTLD_DI_LINKMAP, &libplug_info) < 0) {
        fprintf(stderr, "ERROR: could get info for %s: %s\n", libplug_file_name, dlerror());
        return false;
    }

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

int main(void)
{
    if (!reload_libplug()) return 1;

    int fd = inotify_init1(IN_NONBLOCK);
    if (fd < 0) perror ("inotify_init");
    int wd = inotify_add_watch(fd, libplug_info->l_name, IN_ALL_EVENTS);
    if (wd < 0) fprintf(stderr, "ERROR: could not add watch to %s: %s\n", libplug_info->l_name, strerror(errno));
    #define BUF_LEN (sizeof(struct inotify_event) * 16)
    char buf[BUF_LEN];

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    size_t factor = 60;
    InitWindow(factor*16, factor*9, "Musializer");
    SetTargetFPS(60);
    InitAudioDevice();

    plug_init();
    while (!WindowShouldClose()) {
        int len = read(fd, buf, BUF_LEN);
        for (int i = 0; i < len; i += (sizeof(struct inotify_event))) {
            struct inotify_event *event = (struct inotify_event*)&buf[i];
                if (event->mask & (IN_IGNORED | IN_ATTRIB)) {
                    inotify_rm_watch(fd, wd);
                    wd = inotify_add_watch(fd, libplug_info->l_name, IN_ALL_EVENTS);
                    if (wd < 0) fprintf(stderr, "ERROR: could not add watch to %s: %s\n", libplug_info->l_name, strerror(errno));
                }
                if (event->mask & IN_CLOSE_WRITE) {
                    void *state = plug_pre_reload();
                    if (!reload_libplug()) return 1;
                    plug_post_reload(state);
                }
        }
        
        plug_update();
    }

    return 0;
}
