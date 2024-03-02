#include <stdio.h>
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <windows.h>

#include <raylib.h>
#include "hotreload.h"

static const char *libplug_file_name = "libplug.dll";
static void *libplug = NULL;

#define PLUG(name, ...) name##_t *name = NULL;
LIST_OF_PLUGS
#undef PLUG

bool reload_libplug(void)
{
    if (libplug != NULL) FreeLibrary(libplug);

    libplug = LoadLibrary(libplug_file_name);
    if (libplug == NULL) {
        TraceLog(LOG_ERROR, "HOTRELOAD: could not load %s: %s", libplug_file_name, GetLastError());
        return false;
    }

    #define PLUG(name, ...) \
        name = (void*)GetProcAddress(libplug, #name); \
        if (name == NULL) { \
            TraceLog(LOG_ERROR, "HOTRELOAD: could not find %s symbol in %s: %s", \
                     #name, libplug_file_name, GetLastError()); \
            return false; \
        }
    LIST_OF_PLUGS
    #undef PLUG

    return true;
}
