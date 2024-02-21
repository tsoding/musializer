#include <stdio.h>
#include <dlfcn.h>

#include <raylib.h>

#include "hotreload.h"

#if MUSIALIZER_TARGET == TARGET_MACOS
    static const char *libplug_file_name = "libplug.dylib";
#else
    static const char *libplug_file_name = "libplug.so";
#endif

static void *libplug = NULL;

#define PLUG(name, ...) name##_t *name = NULL;
LIST_OF_PLUGS
#undef PLUG

bool reload_libplug(void)
{
    if (libplug != NULL) dlclose(libplug);

    libplug = dlopen(libplug_file_name, RTLD_NOW);
    if (libplug == NULL) {
        TraceLog(LOG_ERROR, "HOTRELOAD: could not load %s: %s", libplug_file_name, dlerror());
        return false;
    }

    #define PLUG(name, ...) \
        name = dlsym(libplug, #name); \
        if (name == NULL) { \
            TraceLog(LOG_ERROR, "HOTRELOAD: could not find %s symbol in %s: %s", \
                     #name, libplug_file_name, dlerror()); \
            return false; \
        }
    LIST_OF_PLUGS
    #undef PLUG

    return true;
}
