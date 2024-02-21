#ifndef HOTRELOAD_H_
#define HOTRELOAD_H_

#include <stdbool.h>

#include "plug.h"
#include "targets.h"
#include "config.h"

#ifdef MUSIALIZER_HOTRELOAD
    #define PLUG(name, ...) extern name##_t *name;
    LIST_OF_PLUGS
    #undef PLUG
    bool reload_libplug(void);
#else
    #define PLUG(name, ...) name##_t name;
    LIST_OF_PLUGS
    #undef PLUG
    #define reload_libplug() true
#endif // HOTRELOAD

#endif // HOTRELOAD_H_
