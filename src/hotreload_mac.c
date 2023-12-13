#include <stdio.h>
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <limits.h>
#include <string.h>

#include <raylib.h>

#include "hotreload.h"

static const char *libplug_file_name = "libplug.dylib";
static void *libplug = NULL;

#define PLUG(name, ...) name##_t *name = NULL;
LIST_OF_PLUGS
#undef PLUG

bool reload_libplug(void)
{
    if (libplug != NULL) dlclose(libplug);

    // I'm not sure if this is the best way to do this but Basically
    // on MacOS the library lookup is a bit weird.
    //
    // The easiest way to load the library is to use the full path,
    // so we get the executable path and replace the executable name
    // with the library name.
    char executable_path[PATH_MAX];
    uint32_t bufsize = PATH_MAX;
    if(_NSGetExecutablePath(executable_path, &bufsize)) {
        TraceLog(LOG_ERROR, "HOTRELOAD: could not get executable path");
        return false;
    }
    // Replace the binary name with the library name.
    //
    // The easiest way to do this is to find the last slash and
    // replace everything after it with the library name.
    char *last_slash = strrchr(executable_path, '/');
    if (last_slash == NULL) {
        TraceLog(LOG_ERROR, "HOTRELOAD: could not find executable path");
        return false;
    }
    strcpy(last_slash + 1, libplug_file_name);

    libplug = dlopen(executable_path, RTLD_NOW);
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
