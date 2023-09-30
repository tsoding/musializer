#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <complex.h>

#include "plug.h"
#include "ffmpeg.h"
#define _WINDOWS_
#include "miniaudio.h"
#define NOB_IMPLEMENTATION
#include "nob.h"

#include <raylib.h>
#include <rlgl.h>

#define GLSL_VERSION 330

#define N (1<<13)
#define FONT_SIZE 69

#define RENDER_FPS 30
#define RENDER_FACTOR 100
#define RENDER_WIDTH (16*RENDER_FACTOR)
#define RENDER_HEIGHT (9*RENDER_FACTOR)

// Microsoft could not update their parser OMEGALUL:
// https://learn.microsoft.com/en-us/cpp/c-runtime-library/complex-math-support?view=msvc-170#types-used-in-complex-math
#ifdef _MSC_VER
#    define Float_Complex _Fcomplex
#    define cfromreal(re) _FCbuild(re, 0)
#    define cfromimag(im) _FCbuild(0, im)
#    define mulcc _FCmulcc
#    define addcc(a, b) _FCbuild(crealf(a) + crealf(b), cimagf(a) + cimagf(b))
#    define subcc(a, b) _FCbuild(crealf(a) - crealf(b), cimagf(a) - cimagf(b))
#else
#    define Float_Complex float complex
#    define cfromreal(re) (re)
#    define cfromimag(im) ((im)*I)
#    define mulcc(a, b) ((a)*(b))
#    define addcc(a, b) ((a)+(b))
#    define subcc(a, b) ((a)-(b))
#endif

typedef struct {
    char *file_path;
    Music music;
} Track;

typedef struct {
    Track *items;
    size_t count;
    size_t capacity;
} Tracks;

typedef struct {
    // Visualizer
    Tracks tracks;
    int current_track;
    Font font;
    Shader circle;
    int circle_radius_location;
    int circle_power_location;

    // Renderer
    bool rendering;
    RenderTexture2D screen;
    Wave wave;
    float *wave_samples;
    size_t wave_cursor;
    FFMPEG *ffmpeg;

    // FFT Analyzer
    float in_raw[N];
    float in_win[N];
    Float_Complex out_raw[N];
    float out_log[N];
    float out_smooth[N];
    float out_smear[N];

    // Microphone
    bool capturing;
    ma_device *microphone;
} Plug;

Plug *p = NULL;

bool fft_settled(void)
{
    float eps = 1e-3;
    for (size_t i = 0; i < N; ++i) {
        if (p->out_smooth[i] > eps) return false;
        if (p->out_smear[i] > eps) return false;
    }
    return true;
}

void fft_clean(void)
{
    memset(p->in_raw, 0, sizeof(p->in_raw));
    memset(p->in_win, 0, sizeof(p->in_win));
    memset(p->out_raw, 0, sizeof(p->out_raw));
    memset(p->out_log, 0, sizeof(p->out_log));
    memset(p->out_smooth, 0, sizeof(p->out_smooth));
    memset(p->out_smear, 0, sizeof(p->out_smear));
}

// Ported from https://rosettacode.org/wiki/Fast_Fourier_transform#Python
void fft(float in[], size_t stride, Float_Complex out[], size_t n)
{
    assert(n > 0);

    if (n == 1) {
        out[0] = cfromreal(in[0]);
        return;
    }

    fft(in, stride*2, out, n/2);
    fft(in + stride, stride*2,  out + n/2, n/2);

    for (size_t k = 0; k < n/2; ++k) {
        float t = (float)k/n;
        Float_Complex v = mulcc(cexpf(cfromimag(-2*PI*t)), out[k + n/2]);
        Float_Complex e = out[k];
        out[k]       = addcc(e, v);
        out[k + n/2] = subcc(e, v);
    }
}

static inline float amp(Float_Complex z)
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
        p->in_win[i] = p->in_raw[i]*hann;
    }

    // FFT
    fft(p->in_win, 1, p->out_raw, N);

    // "Squash" into the Logarithmic Scale
    float step = 1.06;
    float lowf = 1.0f;
    size_t m = 0;
    float max_amp = 1.0f;
    for (float f = lowf; (size_t) f < N/2; f = ceilf(f*step)) {
        float f1 = ceilf(f*step);
        float a = 0.0f;
        for (size_t q = (size_t) f; q < N/2 && q < (size_t) f1; ++q) {
            float b = amp(p->out_raw[q]);
            if (b > a) a = b;
        }
        if (max_amp < a) max_amp = a;
        p->out_log[m++] = a;
    }

    // Normalize Frequencies to 0..1 range
    for (size_t i = 0; i < m; ++i) {
        p->out_log[i] /= max_amp;
    }

    // Smooth out and smear the values
    for (size_t i = 0; i < m; ++i) {
        float smoothness = 8;
        p->out_smooth[i] += (p->out_log[i] - p->out_smooth[i])*smoothness*dt;
        float smearness = 3;
        p->out_smear[i] += (p->out_smooth[i] - p->out_smear[i])*smearness*dt;
    }

    return m;
}

void fft_render(Rectangle boundary, size_t m)
{
    // The width of a single bar
    float cell_width = boundary.width/m;

    // Global color parameters
    float saturation = 0.75f;
    float value = 1.0f;

    // Display the Bars
    for (size_t i = 0; i < m; ++i) {
        float t = p->out_smooth[i];
        float hue = (float)i/m;
        Color color = ColorFromHSV(hue*360, saturation, value);
        Vector2 startPos = {
            boundary.x + i*cell_width + cell_width/2,
            boundary.y + boundary.height - boundary.height*2/3*t,
        };
        Vector2 endPos = {
            boundary.x + i*cell_width + cell_width/2,
            boundary.y + boundary.height,
        };
        float thick = cell_width/3*sqrtf(t);
        DrawLineEx(startPos, endPos, thick, color);
    }

    Texture2D texture = { rlGetTextureIdDefault(), 1, 1, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };

    // Display the Smears
    SetShaderValue(p->circle, p->circle_radius_location, (float[1]){ 0.3f }, SHADER_UNIFORM_FLOAT);
    SetShaderValue(p->circle, p->circle_power_location, (float[1]){ 3.0f }, SHADER_UNIFORM_FLOAT);
    BeginShaderMode(p->circle);
    for (size_t i = 0; i < m; ++i) {
        float start = p->out_smear[i];
        float end = p->out_smooth[i];
        float hue = (float)i/m;
        Color color = ColorFromHSV(hue*360, saturation, value);
        Vector2 startPos = {
            boundary.x + i*cell_width + cell_width/2,
            boundary.y + boundary.height - boundary.height*2/3*start,
        };
        Vector2 endPos = {
            boundary.x + i*cell_width + cell_width/2,
            boundary.y + boundary.height - boundary.height*2/3*end,
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
    SetShaderValue(p->circle, p->circle_radius_location, (float[1]){ 0.07f }, SHADER_UNIFORM_FLOAT);
    SetShaderValue(p->circle, p->circle_power_location, (float[1]){ 5.0f }, SHADER_UNIFORM_FLOAT);
    BeginShaderMode(p->circle);
    for (size_t i = 0; i < m; ++i) {
        float t = p->out_smooth[i];
        float hue = (float)i/m;
        Color color = ColorFromHSV(hue*360, saturation, value);
        Vector2 center = {
            boundary.x + i*cell_width + cell_width/2,
            boundary.y + boundary.height - boundary.height*2/3*t,
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
    memmove(p->in_raw, p->in_raw + 1, (N - 1)*sizeof(p->in_raw[0]));
    p->in_raw[N-1] = frame;
}

void callback(void *bufferData, unsigned int frames)
{
    // https://cdecl.org/?q=float+%28*fs%29%5B2%5D
    float (*fs)[2] = bufferData;

    for (size_t i = 0; i < frames; ++i) {
        fft_push(fs[i][0]);
    }
}
void ma_callback(ma_device *pDevice, void *pOutput, const void *pInput,ma_uint32 frameCount)
{
    callback((void*)pInput,frameCount);
    (void)pOutput;
    (void)pDevice;
}

void plug_init(void)
{
    p = malloc(sizeof(*p));
    assert(p != NULL && "Buy more RAM lol");
    memset(p, 0, sizeof(*p));

    p->font = LoadFontEx("./resources/fonts/Alegreya-Regular.ttf", FONT_SIZE, NULL, 0);
    // TODO: Maybe we should try to keep compiling different versions of shaders
    // until one of them works?
    //
    // If the shader can not be compiled maybe we could fallback to software rendering
    // of the texture of a fuzzy circle? The shader does not really do anything particularly
    // special.
    p->circle = LoadShader(NULL, TextFormat("./resources/shaders/glsl%d/circle.fs", GLSL_VERSION));
    p->circle_radius_location = GetShaderLocation(p->circle, "radius");
    p->circle_power_location = GetShaderLocation(p->circle, "power");
    p->screen = LoadRenderTexture(RENDER_WIDTH, RENDER_HEIGHT);
    p->current_track = -1;
}

Plug *plug_pre_reload(void)
{
    for (size_t i = 0; i < p->tracks.count; ++i) {
        Track *it = &p->tracks.items[i];
        DetachAudioStreamProcessor(it->music.stream, callback);
    }
    return p;
}

void plug_post_reload(Plug *pp)
{
    p = pp;
    for (size_t i = 0; i < p->tracks.count; ++i) {
        Track *it = &p->tracks.items[i];
        AttachAudioStreamProcessor(it->music.stream, callback);
    }
    UnloadShader(p->circle);
    p->circle = LoadShader(NULL, TextFormat("./resources/shaders/glsl%d/circle.fs", GLSL_VERSION));
    p->circle_radius_location = GetShaderLocation(p->circle, "radius");
    p->circle_power_location = GetShaderLocation(p->circle, "power");
}

Track *current_track(void)
{
    if (0 <= p->current_track && (size_t) p->current_track < p->tracks.count) {
        return &p->tracks.items[p->current_track];
    }
    return NULL;
}

void error_load_file_popup(void)
{
    // TODO: implement annoying popup that indicates that we could not load the file
    TraceLog(LOG_ERROR, "Could not load file");
}

void plug_update(void)
{
    int w = GetRenderWidth();
    int h = GetRenderHeight();

    BeginDrawing();
    ClearBackground(GetColor(0x151515FF));

    if (!p->rendering) { // We are in the Preview Mode
        // TODO: there is no visual indication whether we are in the capturing or playing mode
        if (p->capturing) {
            if (p->microphone != NULL) {
                if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_M)) {
                    ma_device_uninit(p->microphone);
                    p->microphone = NULL;
                    p->capturing = false;
                }

                size_t m = fft_analyze(GetFrameTime());
                fft_render(CLITERAL(Rectangle) {
                    0, 0, GetRenderWidth(), GetRenderHeight()
                }, m);
            } else {
                if (IsKeyPressed(KEY_ESCAPE)) {
                    p->capturing = false;
                }

                const char *label = "Capture Device Error: Check the Logs";
                Color color = RED;
                int fontSize = p->font.baseSize;
                Vector2 size = MeasureTextEx(p->font, label, fontSize, 0);
                Vector2 position = {
                    w/2 - size.x/2,
                    h/2 - size.y/2,
                };
                DrawTextEx(p->font, label, position, fontSize, 0, color);

                label = "(Press ESC to Continue)";
                fontSize = p->font.baseSize*2/3;
                size = MeasureTextEx(p->font, label, fontSize, 0);
                position.x = w/2 - size.x/2,
                position.y = h/2 - size.y/2 + fontSize,
                DrawTextEx(p->font, label, position, fontSize, 0, color);
            }
        } else {
            if (IsFileDropped()) {
                FilePathList droppedFiles = LoadDroppedFiles();
                for (size_t i = 0; i < droppedFiles.count; ++i) {
                    char *file_path = strdup(droppedFiles.paths[i]);

                    Track *track = current_track();
                    if (track) StopMusicStream(track->music);

                    Music music = LoadMusicStream(file_path);

                    if (IsMusicReady(music)) {
                        SetMusicVolume(music, 0.5f);
                        AttachAudioStreamProcessor(music.stream, callback);
                        PlayMusicStream(music);

                        nob_da_append(&p->tracks, (CLITERAL(Track) {
                            .file_path = file_path,
                            .music = music,
                        }));
                        p->current_track = p->tracks.count - 1;
                    } else {
                        free(file_path);
                        error_load_file_popup();
                    }
                }
                UnloadDroppedFiles(droppedFiles);
            }

            if (IsKeyPressed(KEY_M)) {
                // TODO: let the user choose their mic
                ma_device_config deviceConfig = ma_device_config_init(ma_device_type_capture);
                deviceConfig.capture.format = ma_format_f32;
                deviceConfig.capture.channels = 2;
                deviceConfig.sampleRate = 44100;
                deviceConfig.dataCallback = ma_callback;
                deviceConfig.pUserData = NULL;
                p->microphone = malloc(sizeof(ma_device));
                assert(p->microphone != NULL && "Buy MORE RAM lol!!");
                ma_result result = ma_device_init(NULL, &deviceConfig, p->microphone);
                if (result != MA_SUCCESS) {
                    TraceLog(LOG_ERROR,"MINIAUDIO: Failed to initialize capture device: %s",ma_result_description(result));
                }
                if (p->microphone != NULL) {
                    ma_result result = ma_device_start(p->microphone);
                    if (result != MA_SUCCESS) {
                        TraceLog(LOG_ERROR, "MINIAUDIO: Failed to start device: %s",ma_result_description(result));
                        ma_device_uninit(p->microphone);
                        p->microphone = NULL;
                    }
                }
                p->capturing = true;
            }

            Track *track = current_track();
            if (track) { // The music is loaded and ready
                UpdateMusicStream(track->music);

                if (IsKeyPressed(KEY_SPACE)) {
                    if (IsMusicStreamPlaying(track->music)) {
                        PauseMusicStream(track->music);
                    } else {
                        ResumeMusicStream(track->music);
                    }
                }

                if (IsKeyPressed(KEY_Q)) {
                    StopMusicStream(track->music);
                    PlayMusicStream(track->music);
                }

                if (IsKeyPressed(KEY_F)) {
                    StopMusicStream(track->music);

                    fft_clean();
                    // TODO: LoadWave is pretty slow on big files
                    p->wave = LoadWave(track->file_path);
                    p->wave_cursor = 0;
                    p->wave_samples = LoadWaveSamples(p->wave);
                    // TODO: set the rendering output path based on the input path
                    // Basically output into the same folder
                    p->ffmpeg = ffmpeg_start_rendering(p->screen.texture.width, p->screen.texture.height, RENDER_FPS, track->file_path);
                    p->rendering = true;
                    SetTraceLogLevel(LOG_WARNING);
                }

                float panel_height = h*0.25;
                Rectangle preview_boundary = {
                    0, 0, w, h - panel_height
                };

                size_t m = fft_analyze(GetFrameTime());
                fft_render(preview_boundary, m);

                static float panel_scroll = 0;
                static float panel_velocity = 0;
                panel_velocity *= 0.9;
                panel_velocity += GetMouseWheelMove()*panel_height*4;
                panel_scroll -= panel_velocity*GetFrameTime();
                float min_scroll = 0;
                if (panel_scroll < min_scroll) panel_scroll = min_scroll;
                float max_scroll = panel_height*p->tracks.count - w;
                if (max_scroll < 0) max_scroll = 0;
                if (panel_scroll > max_scroll) panel_scroll = max_scroll;
                Rectangle panel_boundary = {
                    .x = -panel_scroll,
                    .y = preview_boundary.height,
                    .width = w,
                    .height = panel_height
                };
                float panel_padding = panel_height*0.1;

                for (size_t i = 0; i < p->tracks.count; ++i) {
                    // TODO: tooltip with filepath on each item in the panel
                    Rectangle item_boundary = {
                        .x = i*panel_height + panel_boundary.x + panel_padding,
                        .y = panel_boundary.y + panel_padding,
                        .width = panel_height - panel_padding*2,
                        .height = panel_height - panel_padding*2,
                    };
                    if (((int) i != p->current_track)) {
                        if (CheckCollisionPointRec(GetMousePosition(), item_boundary)) {
                            if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                                Track *track = current_track();
                                if (track) StopMusicStream(track->music);
                                PlayMusicStream(p->tracks.items[i].music);
                                p->current_track = i;
                            }
                            DrawRectangleRec(item_boundary, RED);
                        } else {
                            DrawRectangleRec(item_boundary, WHITE);
                        }
                    } else {
                        DrawRectangleRec(item_boundary, BLUE);
                    }
                }
            } else { // We are waiting for the user to Drag&Drop the Music
                const char *label = "Drag&Drop Music Here";
                Color color = WHITE;
                Vector2 size = MeasureTextEx(p->font, label, p->font.baseSize, 0);
                Vector2 position = {
                    w/2 - size.x/2,
                    h/2 - size.y/2,
                };
                DrawTextEx(p->font, label, position, p->font.baseSize, 0, color);
            }
        }
    } else { // We are in the Rendering Mode
        Track *track = current_track();
        NOB_ASSERT(track != NULL);
        if (p->ffmpeg == NULL) { // Starting FFmpeg process has failed for some reason
            if (IsKeyPressed(KEY_ESCAPE)) {
                SetTraceLogLevel(LOG_INFO);
                UnloadWave(p->wave);
                UnloadWaveSamples(p->wave_samples);
                p->rendering = false;
                fft_clean();
                PlayMusicStream(track->music);
            }

            const char *label = "FFmpeg Failure: Check the Logs";
            Color color = RED;
            int fontSize = p->font.baseSize;
            Vector2 size = MeasureTextEx(p->font, label, fontSize, 0);
            Vector2 position = {
                w/2 - size.x/2,
                h/2 - size.y/2,
            };
            DrawTextEx(p->font, label, position, fontSize, 0, color);

            label = "(Press ESC to Continue)";
            fontSize = p->font.baseSize*2/3;
            size = MeasureTextEx(p->font, label, fontSize, 0);
            position.x = w/2 - size.x/2,
            position.y = h/2 - size.y/2 + fontSize,
            DrawTextEx(p->font, label, position, fontSize, 0, color);
        } else { // FFmpeg process is going
            // TODO: introduce a rendering mode that perfectly loops the video
            if ((p->wave_cursor >= p->wave.frameCount && fft_settled()) || IsKeyPressed(KEY_ESCAPE)) { // Rendering is finished or cancelled
                // TODO: ffmpeg processes frames slower than we generate them
                // So when we cancel the rendering ffmpeg is still going and blocking the UI
                // We need to do something about that. For example inform the user that
                // we are finalizing the rendering or something.
                if (!ffmpeg_end_rendering(p->ffmpeg)) {
                    // NOTE: Ending FFmpeg process has failed, let's mark ffmpeg handle as NULL
                    // which will be interpreted as "FFmpeg Failure" on the next frame.
                    //
                    // It should be safe to set ffmpeg to NULL even if ffmpeg_end_rendering() failed
                    // cause it should deallocate all the resources even in case of a failure.
                    p->ffmpeg = NULL;
                } else {
                    SetTraceLogLevel(LOG_INFO);
                    UnloadWave(p->wave);
                    UnloadWaveSamples(p->wave_samples);
                    p->rendering = false;
                    fft_clean();
                    PlayMusicStream(track->music);
                }
            } else { // Rendering is going...
                // Label
                const char *label = "Rendering video...";
                Color color = WHITE;

                Vector2 size = MeasureTextEx(p->font, label, p->font.baseSize, 0);
                Vector2 position = {
                    w/2 - size.x/2,
                    h/2 - size.y/2,
                };
                DrawTextEx(p->font, label, position, p->font.baseSize, 0, color);

                // Progress bar
                float bar_width = w*2/3;
                float bar_height = p->font.baseSize*0.25;
                float bar_progress = (float)p->wave_cursor/p->wave.frameCount;
                float bar_padding_top = p->font.baseSize*0.5;
                if (bar_progress > 1) bar_progress = 1;
                Rectangle bar_filling = {
                    .x = w/2 - bar_width/2,
                    .y = h/2 + p->font.baseSize/2 + bar_padding_top,
                    .width = bar_width*bar_progress,
                    .height = bar_height,
                };
                DrawRectangleRec(bar_filling, WHITE);

                Rectangle bar_box = {
                    .x = w/2 - bar_width/2,
                    .y = h/2 + p->font.baseSize/2 + bar_padding_top,
                    .width = bar_width,
                    .height = bar_height,
                };
                DrawRectangleLinesEx(bar_box, 2, WHITE);

                // Rendering
                size_t chunk_size = p->wave.sampleRate/RENDER_FPS;
                // https://cdecl.org/?q=float+%28*fs%29%5B2%5D
                {
                    float *fs = (float*)p->wave_samples;
                    for (size_t i = 0; i < chunk_size; ++i) {
                        if (p->wave_cursor < p->wave.frameCount) {
                            fft_push(fs[p->wave_cursor*p->wave.channels + 0]);
                        } else {
                            fft_push(0);
                        }
                        p->wave_cursor += 1;
                    }
                }

                size_t m = fft_analyze(1.0f/RENDER_FPS);

                BeginTextureMode(p->screen);
                ClearBackground(GetColor(0x151515FF));
                fft_render(CLITERAL(Rectangle) {
                    0, 0, p->screen.texture.width, p->screen.texture.height
                }, m);
                EndTextureMode();

                Image image = LoadImageFromTexture(p->screen.texture);
                if (!ffmpeg_send_frame_flipped(p->ffmpeg, image.data, image.width, image.height)) {
                    // NOTE: we don't check the result of ffmpeg_end_rendering here because we
                    // don't care at this point: writing a frame failed, so something went completely
                    // wrong. So let's just show to the user the "FFmpeg Failure" screen. ffmpeg_end_rendering
                    // should log any additional errors anyway.
                    ffmpeg_end_rendering(p->ffmpeg);
                    p->ffmpeg = NULL;
                }
                UnloadImage(image);
            }
        }
    }

    EndDrawing();
}
