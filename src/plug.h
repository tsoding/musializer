#ifndef PLUG_H_
#define PLUG_H_

#include <complex.h>
#include <raylib.h>

typedef struct {
    Music music;
} Plug;

typedef void (plug_hello_t)(void);
typedef void (plug_world_t)(void);
typedef void (plug_init_t)(Plug *plug, const char *file_path);
typedef void (plug_pre_reload_t)(Plug *plug);
typedef void (plug_post_reload_t)(Plug *plug);
typedef void (plug_update_t)(Plug *plug);

#define LIST_OF_PLUGS \
    PLUG(plug_hello) \
    PLUG(plug_world) \
    PLUG(plug_init) \
    PLUG(plug_pre_reload) \
    PLUG(plug_post_reload) \
    PLUG(plug_update)

#endif // PLUG_H_
