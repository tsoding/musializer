#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

#include "separate_translation_unit_for_miniaudio.h"

#include "miniaudio.h"
#include "raylib_log.h"

ma_encoder_config encoderConfig;

void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{
    user_callback2_t *user_callback = (user_callback2_t*)pDevice->pUserData;
    assert(user_callback != NULL);
    user_callback((void*)pInput, frameCount);
    (void)pOutput;
}

void *init_audio_devices(user_callback2_t *user_callback)
{
    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_capture);
    deviceConfig.capture.format   = ma_format_f32;
    deviceConfig.capture.channels = 2;
    deviceConfig.sampleRate       = 44100;
    deviceConfig.dataCallback     = data_callback;
    deviceConfig.pUserData        = user_callback;

    ma_device *device = malloc(sizeof(ma_device));
    assert(device != NULL && "Buy MORE RAM lol!!");
    ma_result result = ma_device_init(NULL, &deviceConfig, device);
    if (result != MA_SUCCESS) {
        TraceLog(LOG_ERROR, "MINIAUDIO: Failed to initialize capture device: %s", ma_result_description(result));
        return NULL;
    }

    return device;
}

bool start_the_device(void *device)
{
    ma_result result = ma_device_start(device);
    if (result != MA_SUCCESS) {
        ma_device_uninit(device);
        TraceLog(LOG_ERROR, "MINIAUDIO: Failed to start device: %s", ma_result_description(result));
        return false;
    }

    return true;
}
