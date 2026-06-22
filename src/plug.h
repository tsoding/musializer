#ifndef PLUG_H_
#define PLUG_H_

#define NOB_STRIP_PREFIX
#include "thirdparty/nob.h"

#define LIST_OF_PLUGS \
     PLUG(plug_init, void, Nob_File_Paths*)  \
    PLUG(plug_pre_reload, void*, void) \
    PLUG(plug_post_reload, void, void*) \
    PLUG(plug_load_resource, void*, const char*, size_t*) \
    PLUG(plug_free_resource, void, void*) \
    PLUG(plug_update, void, void) \
    PLUG(plug_start_cli_rendering_track, void, const char *, const char *)

#define PLUG(name, ret, ...) typedef ret (name##_t)(__VA_ARGS__);
LIST_OF_PLUGS
#undef PLUG

#endif // PLUG_H_
