#ifndef PLUG_H_
#define PLUG_H_

#include <complex.h>
#include <raylib.h>

typedef struct {
    Music music;
} Plug;

typedef void (*plug_hello_t)(void);
typedef void (*plug_init_t)(Plug *plug, const char *file_path);
typedef void (*plug_pre_reload_t)(Plug *plug);
typedef void (*plug_post_reload_t)(Plug *plug);
typedef void (*plug_update_t)(Plug *plug);

#endif // PLUG_H_
