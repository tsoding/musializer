#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <complex.h>
#include <math.h>

#include <raylib.h>

#define ARRAY_LEN(xs) sizeof(xs)/sizeof(xs[0])

float pi;

#define N 256
float in[N];
float complex out[N];
float max_amp;

typedef struct {
    float left;
    float right;
} Frame;

// Ported from https://rosettacode.org/wiki/Fast_Fourier_transform#Python
void fft(float in[], size_t stride, float complex out[], size_t n)
{
    assert(n > 0);

    if (n == 1) {
        out[0] = in[0];
        return;
    }

    fft(in, stride*2, out, n/2);
    fft(in + stride, stride*2,  out + n/2, n/2);

    for (size_t k = 0; k < n/2; ++k) {
        float t = (float)k/n;
        float complex v = cexp(-2*I*pi*t)*out[k + n/2];
        float complex e = out[k];
        out[k]       = e + v;
        out[k + n/2] = e - v;
    }
}

float amp(float complex z)
{
    float a = fabsf(crealf(z));
    float b = fabsf(cimagf(z));
    if (a < b) return b;
    return a;
}

void callback(void *bufferData, unsigned int frames)
{
    if (frames < N) return;

    Frame *fs = bufferData;

    for (size_t i = 0; i < frames; ++i) {
        in[i] = fs[i].left;
    }
    
    fft(in, 1, out, N);

    max_amp = 0.0f;
    for (size_t i = 0; i < frames; ++i) {
        float a = amp(out[i]);
        if (max_amp < a) max_amp = a;
    }
}

char *shift_args(int *argc, char ***argv)
{
    assert(*argc > 0);
    char *result = (**argv);
    (*argv) += 1;
    (*argc) -= 1;
    return result;
}

int main(int argc, char **argv)
{
    pi = atan2f(1, 1)*4;

    const char *program = shift_args(&argc, &argv);
    
    // TODO: supply input files via drag&drop
    if (argc == 0) {
        fprintf(stderr, "Usage: %s <input>\n", program);
        fprintf(stderr, "ERROR: no input file is provided\n");
        return 1;
    }
    const char *file_path = shift_args(&argc, &argv);

    InitWindow(800, 600, "Musializer");
    SetTargetFPS(60);

    InitAudioDevice();

    Music music = LoadMusicStream(file_path);
    printf("music.frameCount = %u\n", music.frameCount);
    printf("music.stream.sampleRate = %u\n", music.stream.sampleRate);
    printf("music.stream.sampleSize = %u\n", music.stream.sampleSize);
    printf("music.stream.channels = %u\n", music.stream.channels);
    assert(music.stream.sampleSize == 16);
    assert(music.stream.channels == 2);
    
    PlayMusicStream(music);
    SetMusicVolume(music, 0.5f);
    AttachAudioStreamProcessor(music.stream, callback);

    while (!WindowShouldClose()) {
        UpdateMusicStream(music);

        if (IsKeyPressed(KEY_SPACE)) {
            if (IsMusicStreamPlaying(music)) {
                PauseMusicStream(music);
            } else {
                ResumeMusicStream(music);
            }
        }

        int w = GetRenderWidth();
        int h = GetRenderHeight();

        BeginDrawing();
        ClearBackground(CLITERAL(Color) {0x18, 0x18, 0x18, 0xFF});
        float cell_width = (float)w/N;
        for (size_t i = 0; i < N; ++i) {
            float t = amp(out[i])/max_amp;
            DrawRectangle(i*cell_width, h/2 - h/2*t, cell_width, h/2*t, RED);
        }
        // for (size_t i = 0; i < global_frames_count; ++i) {
        //     float t = global_frames[i].left;
        //     if (t > 0) {
        //         DrawRectangle(i*cell_width, h/2 - h/2*t, 1, h/2*t, RED);
        //     } else {
        //         DrawRectangle(i*cell_width, h/2, 1, h/2*t, RED);
        //     }
        // }
        EndDrawing();
    }

    return 0;
}
