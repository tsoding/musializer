#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <complex.h>

#include "plug.h"
#include "ffmpeg.h"

#include <raylib.h>
#include <rlgl.h>

#define GLSL_VERSION 330

#define N (1<<13)
#define FONT_SIZE 69

#define RENDER_FPS 60
#define RENDER_FACTOR 120
#define RENDER_WIDTH (16*RENDER_FACTOR)
#define RENDER_HEIGHT (9*RENDER_FACTOR)

typedef struct {
    char *file_path;
    Music music;
    Font font;
    Shader circle;
    int circle_radius_location;
    int circle_power_location;
    bool error;

    bool rendering;
    RenderTexture2D screen;
    Wave wave;
    float *wave_samples;
    size_t wave_cursor;
    int ffmpeg;
} Plug;

Plug *plug = NULL;

float in_raw[N];
float in_win[N];
float complex out_raw[N];
float out_log[N];
float out_smooth[N];
float out_smear[N];

bool fft_settled(void)
{
    float eps = 1e-3;
    for (size_t i = 0; i < N; ++i) {
        if (out_smooth[i] > eps) return false;
        if (out_smear[i] > eps) return false;
    }
    return true;
}

void fft_clean(void)
{
    memset(in_raw, 0, sizeof(in_raw));
    memset(in_win, 0, sizeof(in_win));
    memset(out_raw, 0, sizeof(out_raw));
    memset(out_log, 0, sizeof(out_log));
    memset(out_smooth, 0, sizeof(out_smooth));
    memset(out_smear, 0, sizeof(out_smear));
}

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
        float complex v = cexp(-2*I*PI*t)*out[k + n/2];
        float complex e = out[k];
        out[k]       = e + v;
        out[k + n/2] = e - v;
    }
}

static inline float amp(float complex z)
{
    float a = crealf(z);
    float b = cimagf(z);
    return logf(a*a + b*b);
}

size_t fft_analyze(float dt)
{
    // Apply the Hann Window on the Input - https://en.wikipedia.org/wiki/Hann_function
    for (size_t i = 0; i < N; ++i) {
        float t = (float)i/(N - 1);
        float hann = 0.5 - 0.5*cosf(2*PI*t);
        in_win[i] = in_raw[i]*hann;
    }

    // FFT
    fft(in_win, 1, out_raw, N);

    // "Squash" into the Logarithmic Scale
    float step = 1.06;
    float lowf = 1.0f;
    size_t m = 0;
    float max_amp = 1.0f;
    for (float f = lowf; (size_t) f < N/2; f = ceilf(f*step)) {
        float f1 = ceilf(f*step);
        float a = 0.0f;
        for (size_t q = (size_t) f; q < N/2 && q < (size_t) f1; ++q) {
            float b = amp(out_raw[q]);
            if (b > a) a = b;
        }
        if (max_amp < a) max_amp = a;
        out_log[m++] = a;
    }

    // Normalize Frequencies to 0..1 range
    for (size_t i = 0; i < m; ++i) {
        out_log[i] /= max_amp;
    }

    // Smooth out and smear the values
    for (size_t i = 0; i < m; ++i) {
        float smoothness = 8;
        out_smooth[i] += (out_log[i] - out_smooth[i])*smoothness*dt;
        float smearness = 3;
        out_smear[i] += (out_smooth[i] - out_smear[i])*smearness*dt;
    }

    return m;
}

void fft_render(size_t w, size_t h, size_t m)
{
    // The width of a single bar
    float cell_width = (float)w/m;

    // Global color parameters
    float saturation = 0.75f;
    float value = 1.0f;

    // Display the Bars
    for (size_t i = 0; i < m; ++i) {
        float t = out_smooth[i];
        float hue = (float)i/m;
        Color color = ColorFromHSV(hue*360, saturation, value);
        Vector2 startPos = {
            i*cell_width + cell_width/2,
            h - h*2/3*t,
        };
        Vector2 endPos = {
            i*cell_width + cell_width/2,
            h,
        };
        float thick = cell_width/3*sqrtf(t);
        DrawLineEx(startPos, endPos, thick, color);
    }

    Texture2D texture = { rlGetTextureIdDefault(), 1, 1, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };

    // Display the Smears
    SetShaderValue(plug->circle, plug->circle_radius_location, (float[1]){ 0.3f }, SHADER_UNIFORM_FLOAT);
    SetShaderValue(plug->circle, plug->circle_power_location, (float[1]){ 3.0f }, SHADER_UNIFORM_FLOAT);
    BeginShaderMode(plug->circle);
    for (size_t i = 0; i < m; ++i) {
        float start = out_smear[i];
        float end = out_smooth[i];
        float hue = (float)i/m;
        Color color = ColorFromHSV(hue*360, saturation, value);
        Vector2 startPos = {
            i*cell_width + cell_width/2,
            h - h*2/3*start,
        };
        Vector2 endPos = {
            i*cell_width + cell_width/2,
            h - h*2/3*end,
        };
        float radius = cell_width*3*sqrtf(end);
        Vector2 origin = {0};
        if (endPos.y >= startPos.y) {
            Rectangle dest = {
                .x = startPos.x - radius/2,
                .y = startPos.y,
                .width = radius,
                .height = endPos.y - startPos.y
            };
            Rectangle source = {0, 0, 1, 0.5};
            DrawTexturePro(texture, source, dest, origin, 0, color);
        } else {
            Rectangle dest = {
                .x = endPos.x - radius/2,
                .y = endPos.y,
                .width = radius,
                .height = startPos.y - endPos.y
            };
            Rectangle source = {0, 0.5, 1, 0.5};
            DrawTexturePro(texture, source, dest, origin, 0, color);
        }
    }
    EndShaderMode();

    // Display the Circles
    SetShaderValue(plug->circle, plug->circle_radius_location, (float[1]){ 0.07f }, SHADER_UNIFORM_FLOAT);
    SetShaderValue(plug->circle, plug->circle_power_location, (float[1]){ 5.0f }, SHADER_UNIFORM_FLOAT);
    BeginShaderMode(plug->circle);
    for (size_t i = 0; i < m; ++i) {
        float t = out_smooth[i];
        float hue = (float)i/m;
        Color color = ColorFromHSV(hue*360, saturation, value);
        Vector2 center = {
            i*cell_width + cell_width/2,
            h - h*2/3*t,
        };
        float radius = cell_width*6*sqrtf(t);
        Vector2 position = {
            .x = center.x - radius,
            .y = center.y - radius,
        };
        DrawTextureEx(texture, position, 0, 2*radius, color);
    }
    EndShaderMode();

}

void fft_push(float frame)
{
    memmove(in_raw, in_raw + 1, (N - 1)*sizeof(in_raw[0]));
    in_raw[N-1] = frame;
}

void callback(void *bufferData, unsigned int frames)
{
    // https://cdecl.org/?q=float+%28*fs%29%5B2%5D
    float (*fs)[2] = bufferData;

    for (size_t i = 0; i < frames; ++i) {
        fft_push(fs[i][0]);
    }
}

void plug_init(void)
{
    plug = malloc(sizeof(*plug));
    assert(plug != NULL && "Buy more RAM lol");
    memset(plug, 0, sizeof(*plug));

    plug->font = LoadFontEx("./resources/fonts/Alegreya-Regular.ttf", FONT_SIZE, NULL, 0);
    // TODO: Maybe we should try to keep compiling different versions of shaders
    // until one of them works?
    //
    // If the shader can not be compiled maybe we could fallback to software rendering
    // of the texture of a fuzzy circle? The shader does not really do anything particularly
    // special.
    plug->circle = LoadShader(NULL, TextFormat("./resources/shaders/glsl%d/circle.fs", GLSL_VERSION));
    plug->circle_radius_location = GetShaderLocation(plug->circle, "radius");
    plug->circle_power_location = GetShaderLocation(plug->circle, "power");
    plug->screen = LoadRenderTexture(RENDER_WIDTH, RENDER_HEIGHT);
}

Plug *plug_pre_reload(void)
{
    if (IsMusicReady(plug->music)) {
        DetachAudioStreamProcessor(plug->music.stream, callback);
    }
    return plug;
}

void plug_post_reload(Plug *prev)
{
    plug = prev;
    if (IsMusicReady(plug->music)) {
        AttachAudioStreamProcessor(plug->music.stream, callback);
    }
    UnloadShader(plug->circle);
    plug->circle = LoadShader(NULL, TextFormat("./resources/shaders/glsl%d/circle.fs", GLSL_VERSION));
    plug->circle_radius_location = GetShaderLocation(plug->circle, "radius");
    plug->circle_power_location = GetShaderLocation(plug->circle, "power");
}

void plug_update(void)
{
    int w = GetRenderWidth();
    int h = GetRenderHeight();

    if (IsFileDropped()) {
        FilePathList droppedFiles = LoadDroppedFiles();
        if (droppedFiles.count > 0) {
            free(plug->file_path);
            plug->file_path = strdup(droppedFiles.paths[0]);

            if (IsMusicReady(plug->music)) {
                StopMusicStream(plug->music);
                UnloadMusicStream(plug->music);
            }

            plug->music = LoadMusicStream(plug->file_path);

            if (IsMusicReady(plug->music)) {
                plug->error = false;
                printf("music.frameCount = %u\n", plug->music.frameCount);
                printf("music.stream.sampleRate = %u\n", plug->music.stream.sampleRate);
                printf("music.stream.sampleSize = %u\n", plug->music.stream.sampleSize);
                printf("music.stream.channels = %u\n", plug->music.stream.channels);
                SetMusicVolume(plug->music, 0.5f);
                AttachAudioStreamProcessor(plug->music.stream, callback);
                PlayMusicStream(plug->music);
            } else {
                plug->error = true;
            }
        }
        UnloadDroppedFiles(droppedFiles);
    }

    BeginDrawing();
    ClearBackground(CLITERAL(Color) {
        0x15, 0x15, 0x15, 0xFF
    });

    if (!plug->rendering) {
        if (IsMusicReady(plug->music)) {
            UpdateMusicStream(plug->music);

            if (IsKeyPressed(KEY_SPACE)) {
                if (IsMusicStreamPlaying(plug->music)) {
                    PauseMusicStream(plug->music);
                } else {
                    ResumeMusicStream(plug->music);
                }
            }

            if (IsKeyPressed(KEY_Q)) {
                StopMusicStream(plug->music);
                PlayMusicStream(plug->music);
            }

            if (IsKeyPressed(KEY_F)) {
                StopMusicStream(plug->music);

                fft_clean();
                plug->wave = LoadWave(plug->file_path);
                plug->wave_cursor = 0;
                plug->wave_samples = LoadWaveSamples(plug->wave);
                // TODO: set the rendering output path based on the input path
                // Basically output into the same folder
                plug->ffmpeg = ffmpeg_start_rendering(plug->screen.texture.width, plug->screen.texture.height, RENDER_FPS, plug->file_path);
                plug->rendering = true;
            }

            size_t m = fft_analyze(GetFrameTime());
            fft_render(GetRenderWidth(), GetRenderHeight(), m);
        } else {
            const char *label;
            Color color;
            if (plug->error) {
                label = "Could not load file";
                color = RED;
            } else {
                label = "Drag&Drop Music Here";
                color = WHITE;
            }
            Vector2 size = MeasureTextEx(plug->font, label, plug->font.baseSize, 0);
            Vector2 position = {
                w/2 - size.x/2,
                h/2 - size.y/2,
            };
            DrawTextEx(plug->font, label, position, plug->font.baseSize, 0, color);
        }
    } else {
        if (plug->wave_cursor >= plug->wave.frameCount && fft_settled()) {
            ffmpeg_end_rendering(plug->ffmpeg);
            UnloadWave(plug->wave);
            UnloadWaveSamples(plug->wave_samples);
            plug->rendering = false;
            fft_clean();
        } else {
            // TODO: indicate the rendering progress
            const char *label = "Rendering video...";
            Color color = WHITE;

            Vector2 size = MeasureTextEx(plug->font, label, plug->font.baseSize, 0);
            Vector2 position = {
                w/2 - size.x/2,
                h/2 - size.y/2,
            };
            DrawTextEx(plug->font, label, position, plug->font.baseSize, 0, color);

            size_t chunk_size = plug->wave.sampleRate/RENDER_FPS;
            // https://cdecl.org/?q=float+%28*fs%29%5B2%5D
            float (*fs)[plug->wave.channels] = (void*)plug->wave_samples;
            for (size_t i = 0; i < chunk_size; ++i) {
                if (plug->wave_cursor < plug->wave.frameCount) {
                    fft_push(fs[plug->wave_cursor][0]);
                } else {
                    fft_push(0);
                }
                plug->wave_cursor += 1;
            }

            size_t m = fft_analyze(1.0f/RENDER_FPS);

            BeginTextureMode(plug->screen);
            ClearBackground(CLITERAL(Color) {0x15, 0x15, 0x15, 0xFF});
            fft_render(plug->screen.texture.width, plug->screen.texture.height, m);
            EndTextureMode();

            Image image = LoadImageFromTexture(plug->screen.texture);
            ffmpeg_send_frame_flipped(plug->ffmpeg, image.data, image.width, image.height);
            UnloadImage(image);
        }
    }

    EndDrawing();
}
