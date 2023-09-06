#ifndef SEPARATE_TRANSLATION_UNIT_FOR_MINIAUDIO_H_
#define SEPARATE_TRANSLATION_UNIT_FOR_MINIAUDIO_H_

#include <stdbool.h>

typedef void (user_callback2_t)(void *bufferData, unsigned int frames);

void *init_audio_devices(user_callback2_t *user_callback);
bool start_the_device(void *device);

#endif  // SEPARATE_TRANSLATION_UNIT_FOR_MINIAUDIO_H_
