#include <stdio.h>
#include <dlfcn.h>

#include "hotreload.h"

static const char *libplug_file_name = "libplug.so";
static void *libplug = NULL;

#define PLUG(name, ...) name##_t *name = NULL;
LIST_OF_PLUGS
#undef PLUG

bool reload_libplug(void)
{
    if (libplug != NULL) dlclose(libplug);

    libplug = dlopen(libplug_file_name, RTLD_NOW);
    if (libplug == NULL) {
        fprintf(stderr, "ERROR: could not load %s: %s\n", libplug_file_name, dlerror());
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
