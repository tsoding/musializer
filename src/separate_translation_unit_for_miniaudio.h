// Since miniaudio.h includes windows.h on Windows this separate translation
// unit was specifically created to avoid collisions between symbols from windows.h
// and raylib.h. The interface of this unit should not depend on anything
// from miniaudio.h.

#ifndef SEPARATE_TRANSLATION_UNIT_FOR_MINIAUDIO_H_
#define SEPARATE_TRANSLATION_UNIT_FOR_MINIAUDIO_H_

#include <stdbool.h>

typedef void (capture_device_callback2_t)(void *bufferData, unsigned int frames);
typedef void Capture_Device;

Capture_Device *init_default_capture_device(capture_device_callback2_t *capture_device_callback);
void uninit_capture_device(Capture_Device *device);
bool start_capture_device(Capture_Device *device);

#endif  // SEPARATE_TRANSLATION_UNIT_FOR_MINIAUDIO_H_
