// Since miniaudio.h includes windows.h on Windows this separate translation
// unit was specifically created to avoid collisions between symbols from windows.h
// and raylib.h. The interface of this unit should not depend on anything
// from miniaudio.h.

#ifndef SEPARATE_TRANSLATION_UNIT_FOR_MINIAUDIO_H_
#define SEPARATE_TRANSLATION_UNIT_FOR_MINIAUDIO_H_

#include <stdbool.h>

typedef void (user_callback2_t)(void *bufferData, unsigned int frames);

void *init_audio_devices(user_callback2_t *user_callback);
bool start_the_device(void *device);

#endif  // SEPARATE_TRANSLATION_UNIT_FOR_MINIAUDIO_H_
