#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <math.h>
#include <string.h>
#include <complex.h>

#include "build/config.h"
#include "plug.h"
#include "ffmpeg.h"
#include "platform.h"
#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
// #define NOB_WARN_DEPRECATED
#include "thirdparty/nob.h"
#include "thirdparty/tinyfiledialogs.h"

#include <raylib.h>
#include <rlgl.h>

#if defined(_WIN32) && defined(MUSIALIZER_HOTRELOAD)
    #define MUSIALIZER_PLUG __declspec(dllexport)
#else
    #define MUSIALIZER_PLUG
#endif

#define PLUG(name, ret, ...) MUSIALIZER_PLUG ret name(__VA_ARGS__);
LIST_OF_PLUGS
#undef PLUG

#ifndef MUSIALIZER_UNBUNDLE
#include "build/bundle.h"

MUSIALIZER_PLUG void plug_free_resource(void *data)
{
    (void) data;
}

MUSIALIZER_PLUG void *plug_load_resource(const char *file_path, size_t *size)
{
    for (size_t i = 0; i < resources_count; ++i) {
        if (strcmp(resources[i].file_path, file_path) == 0) {
            *size = resources[i].size;
            return &bundle[resources[i].offset];
        }
    }
    return NULL;
}

#else

MUSIALIZER_PLUG void plug_free_resource(void *data)
{
    UnloadFileData(data);
}

MUSIALIZER_PLUG void *plug_load_resource(const char *file_path, size_t *size)
{
    int dataSize;
    void *data = LoadFileData(file_path, &dataSize);
    *size = dataSize;
    return data;
}
#endif

#define _WINDOWS_
#include "external/miniaudio.h"
#include "external/dr_wav.h"

#define GLSL_VERSION 330

#define FFT_SIZE (1<<13)
#define FFT_LOG_STEP 1.06f
#define WAVEFORM_CACHE_BINS 4096
#define EQ_LOW_FC 300.0f
#define EQ_HIGH_FC 8000.0f
#define EQ_SAMPLE_RATE 44100.0f
#define FONT_SIZE 48

#define PREVIEW_FPS 60

#define RENDER_FPS 30
#define RENDER_FACTOR 100
#define RENDER_WIDTH (16*RENDER_FACTOR)
#define RENDER_HEIGHT (9*RENDER_FACTOR)

#define COLOR_ACCENT                  ColorFromHSV(225, 0.75, 0.8)
#define COLOR_BACKGROUND              GetColor(0x151515FF)
#define COLOR_TRACK_PANEL_BACKGROUND  ColorBrightness(COLOR_BACKGROUND, -0.1)
#define COLOR_TRACK_BUTTON_BACKGROUND ColorBrightness(COLOR_BACKGROUND, 0.15)
#define COLOR_TRACK_BUTTON_HOVEROVER  ColorBrightness(COLOR_TRACK_BUTTON_BACKGROUND, 0.15)
#define COLOR_TRACK_BUTTON_SELECTED   COLOR_ACCENT
#define COLOR_TIMELINE_CURSOR         COLOR_ACCENT
#define COLOR_TIMELINE_BACKGROUND     ColorBrightness(COLOR_BACKGROUND, -0.3)
#define COLOR_HUD_BUTTON_BACKGROUND   COLOR_TRACK_BUTTON_BACKGROUND
#define COLOR_HUD_BUTTON_HOVEROVER    COLOR_TRACK_BUTTON_HOVEROVER
#define COLOR_POPUP_BACKGROUND        ColorFromHSV(0, 0.75, 0.8)
#define COLOR_POPUP_SUCCESS           ColorFromHSV(120, 0.75, 0.8)
#define COLOR_TOOLTIP_BACKGROUND      COLOR_TRACK_PANEL_BACKGROUND
#define COLOR_TOOLTIP_FOREGROUND      WHITE
#define HUD_TIMER_SECS 1.0f
#define HUD_BUTTON_SIZE 50
#define HUD_BUTTON_MARGIN 50
#define HUD_ICON_SCALE 0.5
#define HUD_POPUP_LIFETIME_SECS 2.0f
#define HUD_POPUP_SLIDEIN_SECS 0.1f
#define TOOLTIP_PADDING 20.0f
#define TRACKLABEL_SCROLL_SECS 0.05f

#define KEY_TOGGLE_PLAY KEY_SPACE
#define KEY_RENDER      KEY_R
#define IS_KEY_DOWN_MOD(mod) (IsKeyDown(KEY_LEFT_##mod) || IsKeyDown(KEY_RIGHT_##mod))
#define IS_CTRL_DOWN     IS_KEY_DOWN_MOD(CONTROL)
#define KEY_FULLSCREEN  KEY_F
#define KEY_CAPTURE     KEY_C
#define KEY_TOGGLE_MUTE KEY_M

static char *duplicate_string(const char *text)
{
    size_t length = strlen(text) + 1;
    char *result = malloc(length);
    if (result != NULL) memcpy(result, text, length);
    return result;
}

// Microsoft could not update their parser OMEGALUL:
// https://learn.microsoft.com/en-us/cpp/c-runtime-library/complex-math-support?view=msvc-170#types-used-in-complex-math
#ifdef _MSC_VER
#    define Float_Complex _Fcomplex
#    define cbuild(re, im) _FCbuild(re, im)
#    define cfromreal(re) _FCbuild(re, 0)
#    define cfromimag(im) _FCbuild(0, im)
#    define mulcc _FCmulcc
#    define addcc(a, b) _FCbuild(crealf(a) + crealf(b), cimagf(a) + cimagf(b))
#    define subcc(a, b) _FCbuild(crealf(a) - crealf(b), cimagf(a) - cimagf(b))
#else
#    define Float_Complex float complex
#    define cbuild(re, im) ((re) + (im)*I)
#    define cfromreal(re) (re)
#    define cfromimag(im) ((im)*I)
#    define mulcc(a, b) ((a)*(b))
#    define addcc(a, b) ((a)+(b))
#    define subcc(a, b) ((a)-(b))
#endif

typedef struct {
    char *file_path;
    Music music;
    unsigned char *music_data;
    Texture2D cover;
    bool has_cover;
} Track;

typedef enum {
    REPEAT_NONE,
    REPEAT_ALL,
} Repeat_Mode;

typedef enum {
    VIZ_BARS,
    VIZ_CIRCULAR,
    VIZ_WAVEFORM,
    COUNT_VIZ_MODES,
} Viz_Mode;

typedef struct {
    Track *items;
    size_t count;
    size_t capacity;
} Tracks;

typedef struct {
    float lifetime;
    char message[64];
    bool success;
} Popup;

typedef struct {
    float min;
    float max;
} Waveform_Peak;

#define PT_GET(pt, index) (assert(index < (pt)->count), &(pt)->items[((pt)->begin + index)%POPUP_TRAY_CAPACITY])
#define PT_FIRST(pt) PT_GET((pt), 0)
#define PT_LAST(pt) PT_GET((pt), (pt)->count - 1)

#define POPUP_TRAY_CAPACITY 20
typedef struct {
    Popup items[POPUP_TRAY_CAPACITY];
    size_t begin;
    size_t count;
    float slide;
} Popup_Tray;


typedef enum {
    SIDE_LEFT,
    SIDE_RIGHT,
    SIDE_TOP,
    SIDE_BOTTOM,
} Side;

typedef enum {
    UI_ICON_FULLSCREEN,
    UI_ICON_VOLUME,
    UI_ICON_PLAY,
    UI_ICON_RENDER,
    UI_ICON_MICROPHONE,
    COUNT_UI_ICONS,
} UI_Icon;

static_assert(COUNT_UI_ICONS == 5, "Amount of icons changed");
static const char *icon_file_paths[COUNT_UI_ICONS] = {
    [UI_ICON_FULLSCREEN] = "./resources/icons/fullscreen.png",
    [UI_ICON_VOLUME]     = "./resources/icons/volume.png",
    [UI_ICON_PLAY]       = "./resources/icons/play.png",
    [UI_ICON_RENDER]     = "./resources/icons/render.png",
    [UI_ICON_MICROPHONE] = "./resources/icons/microphone.png",
};

typedef struct {
    // Assets
    Texture2D icon_textures[COUNT_UI_ICONS];

    // Visualizer
    Tracks tracks;
    int current_track;
    Repeat_Mode repeat_mode;
    bool shuffle;
    Font font;
    Shader circle;
    int circle_radius_location;
    int circle_power_location;
    bool fullscreen;

    // Renderer
    bool rendering;
    RenderTexture2D screen;
    Wave wave;
    float *wave_samples;
    size_t wave_cursor;
    FFMPEG *ffmpeg;
    bool cancel_rendering;

    // Waveform Preview
    char *preview_waveform_path;
    Waveform_Peak *preview_waveform;
    size_t preview_waveform_count;

    // FFT Analyzer
    float in_raw[FFT_SIZE];
    size_t fft_write_cursor;
    float in_win[FFT_SIZE];
    Float_Complex out_raw[FFT_SIZE];
    float out_log[FFT_SIZE];
    float out_smooth[FFT_SIZE];
    float out_smear[FFT_SIZE];
    // TODO: Make FFT Analyzer take into account multiple channels somehow
    //   Extracted from https://github.com/tsoding/musializer/pull/11

    uint64_t active_button_id;

    // Equalizer
    float eq_low;
    float eq_mid;
    float eq_high;
    bool eq_low_drag;
    bool eq_mid_drag;
    bool eq_high_drag;

    // Audio EQ (1-pole filter states for 3-band splitter)
    float eq_low_lp[2];
    float eq_high_lp[2];

    // Crossfade
    bool crossfading;
    float crossfade_timer;
    float crossfade_duration;
    Music crossfade_music;

    // Playback tracking
    bool track_was_playing;

    // Beat Detection
    float beat_energy_history[43];
    size_t beat_history_index;
    float beat_intensity;
    bool beat_detected;

    // Visualization
    Viz_Mode viz_mode;
    float repeat_mode_label_timer;

    // Now-playing banner
    int now_playing_track;
    float now_playing_timer;

    // Keyboard shortcut overlay
    bool show_help;

    Popup_Tray pt;

    bool tooltip_show;
    char tooltip_buffer[32];
    Side tooltip_align;
    Rectangle tooltip_element_boundary;

#ifdef MUSIALIZER_MICROPHONE
    bool capturing;
    ma_device microphone;
    drwav wav;
    bool microphone_working;
#endif // MUSIALIZER_MICROPHONE
} Plug;

static Plug *p = NULL;
static Platform_Mutex *fft_mutex = NULL;
static float fft_hann_window[FFT_SIZE];
static size_t fft_bit_reverse[FFT_SIZE];
static Float_Complex fft_twiddles[FFT_SIZE/2];
static struct {
    size_t begin;
    size_t end;
} fft_log_bins[FFT_SIZE/2];
static size_t fft_log_bin_count;
static Color fft_colors[FFT_SIZE/2];
static Color fft_colors_dim[FFT_SIZE/2];
static float fft_circle_x[FFT_SIZE/2];
static float fft_circle_y[FFT_SIZE/2];
static float eq_alpha_low;
static float eq_alpha_high;

static void fft_buffer_init(void)
{
    fft_mutex = platform_mutex_create();
    assert(fft_mutex != NULL && "Could not create FFT mutex");
    eq_alpha_low = 1.0f - expf(-2.0f*PI*EQ_LOW_FC/EQ_SAMPLE_RATE);
    eq_alpha_high = 1.0f - expf(-2.0f*PI*EQ_HIGH_FC/EQ_SAMPLE_RATE);

    for (size_t i = 0; i < FFT_SIZE; ++i) {
        float t = (float)i/(FFT_SIZE - 1);
        fft_hann_window[i] = 0.5f - 0.5f*cosf(2*PI*t);
    }

    size_t reversed = 0;
    for (size_t i = 0; i < FFT_SIZE; ++i) {
        fft_bit_reverse[i] = reversed;
        if (i + 1 < FFT_SIZE) {
            size_t bit = FFT_SIZE >> 1;
            while (reversed & bit) {
                reversed ^= bit;
                bit >>= 1;
            }
            reversed ^= bit;
        }
    }

    for (size_t i = 0; i < FFT_SIZE/2; ++i) {
        float angle = 2.0f*PI*(float)i/FFT_SIZE;
        fft_twiddles[i] = cbuild(cosf(angle), sinf(angle));
    }

    fft_log_bin_count = 0;
    for (float f = 1.0f; (size_t)f < FFT_SIZE/2; f = ceilf(f*FFT_LOG_STEP)) {
        size_t begin = (size_t)f;
        size_t end = (size_t)ceilf(f*FFT_LOG_STEP);
        if (end > FFT_SIZE/2) end = FFT_SIZE/2;
        fft_log_bins[fft_log_bin_count].begin = begin;
        fft_log_bins[fft_log_bin_count].end = end;
        fft_log_bin_count++;
    }

    // The exact bin count depends on FFT_SIZE and FFT_LOG_STEP. Build stable
    // render data once so the frame loop does not repeat color and trig work.
    for (size_t i = 0; i < fft_log_bin_count; ++i) {
        float hue = 360.0f*(float)i/fft_log_bin_count;
        float angle = 2.0f*PI*(float)i/fft_log_bin_count - PI/2.0f;
        fft_colors[i] = ColorFromHSV(hue, 0.75f, 1.0f);
        fft_colors_dim[i] = ColorFromHSV(hue, 0.75f, 0.5f);
        fft_circle_x[i] = cosf(angle);
        fft_circle_y[i] = sinf(angle);
    }
}

static void fft_buffer_shutdown(void)
{
    platform_mutex_destroy(fft_mutex);
    fft_mutex = NULL;
}

static bool fft_settled(void)
{
    float eps = 1e-3;
    for (size_t i = 0; i < fft_log_bin_count; ++i) {
        if (p->out_smooth[i] > eps) return false;
        if (p->out_smear[i] > eps) return false;
    }
    return true;
}

static void fft_clean(void)
{
    platform_mutex_lock(fft_mutex);
    memset(p->in_raw, 0, sizeof(p->in_raw));
    p->fft_write_cursor = 0;
    platform_mutex_unlock(fft_mutex);
    memset(p->in_win, 0, sizeof(p->in_win));
    memset(p->out_raw, 0, sizeof(p->out_raw));
    memset(p->out_log, 0, sizeof(p->out_log));
    memset(p->out_smooth, 0, sizeof(p->out_smooth));
    memset(p->out_smear, 0, sizeof(p->out_smear));
}

// Ported from https://cp-algorithms.com/algebra/fft.html
static void fft(const float in[], Float_Complex out[], size_t n)
{
    assert(n == FFT_SIZE);
    for(size_t i = 0; i < n; i++) {
        out[fft_bit_reverse[i]] = cfromreal(in[i]);
    }

    for (size_t len = 2; len <= n; len <<= 1) {
        size_t twiddle_step = n/len;
        for (size_t i = 0; i < n; i += len) {
            for (size_t j = 0; j < len / 2; j++) {
                Float_Complex w = fft_twiddles[j*twiddle_step];
                Float_Complex u = out[i+j], v = mulcc(out[i+j+len/2], w);
                out[i+j] = addcc(u, v);
                out[i+j+len/2] = subcc(u, v);
            }
        }
    }
}

static inline float power(Float_Complex z)
{
    float a = crealf(z);
    float b = cimagf(z);
    return a*a + b*b;
}

static size_t fft_analyze(float dt)
{
    // Snapshot the audio-thread input, then release it before doing analyzer work.
    platform_mutex_lock(fft_mutex);
    size_t cursor = p->fft_write_cursor;
    size_t tail = FFT_SIZE - cursor;
    memcpy(p->in_win, p->in_raw + cursor, tail*sizeof(p->in_win[0]));
    memcpy(p->in_win + tail, p->in_raw, cursor*sizeof(p->in_win[0]));
    platform_mutex_unlock(fft_mutex);

    // Apply the Hann Window on the Input - https://en.wikipedia.org/wiki/Hann_function
    for (size_t i = 0; i < FFT_SIZE; ++i) {
        p->in_win[i] *= fft_hann_window[i];
    }

    // FFT
    fft(p->in_win, p->out_raw, FFT_SIZE);

    // "Squash" into the Logarithmic Scale
    size_t m = fft_log_bin_count;
    float max_amp = 1.0f;
    for (size_t i = 0; i < m; ++i) {
        float max_power = 1.0f;
        for (size_t q = fft_log_bins[i].begin; q < fft_log_bins[i].end; ++q) {
            float value = power(p->out_raw[q]);
            if (value > max_power) max_power = value;
        }
        float a = logf(max_power);
        if (max_amp < a) max_amp = a;
        p->out_log[i] = a;
    }

    // Normalize Frequencies to 0..1 range
    for (size_t i = 0; i < m; ++i) {
        p->out_log[i] /= max_amp;
    }

    // Apply EQ gains
    {
        size_t low_end = m / 6;
        if (low_end < 1) low_end = 1;
        size_t mid_end = m / 2;
        if (mid_end <= low_end) mid_end = low_end + 1;
        for (size_t i = 0; i < m; ++i) {
            float gain;
            if (i < low_end) gain = p->eq_low * 2.0f;
            else if (i < mid_end) gain = p->eq_mid * 2.0f;
            else gain = p->eq_high * 2.0f;
            p->out_log[i] *= gain;
        }
    }

    // Smooth out and smear the values
    float fast_decay = expf(-8.0f*dt);
    float smoothness = 1.0f - fast_decay;
    float smearness = 1.0f - expf(-3.0f*dt);
    for (size_t i = 0; i < m; ++i) {
        p->out_smooth[i] += (p->out_log[i] - p->out_smooth[i])*smoothness;
        p->out_smear[i] += (p->out_smooth[i] - p->out_smear[i])*smearness;
    }

    // Beat Detection
    {
        float avg_energy = 0;
        for (size_t i = 0; i < m; i++) {
            avg_energy += p->out_log[i];
        }
        avg_energy /= m;

        size_t beat_hist_len = sizeof(p->beat_energy_history)/sizeof(p->beat_energy_history[0]);
        p->beat_energy_history[p->beat_history_index % beat_hist_len] = avg_energy;
        p->beat_history_index++;

        if (p->beat_history_index > beat_hist_len) {
            float sum = 0;
            for (size_t i = 0; i < beat_hist_len; i++) {
                sum += p->beat_energy_history[i];
            }
            float avg = sum / beat_hist_len;
            p->beat_detected = avg_energy > avg * 1.5f;
        } else {
            p->beat_detected = false;
        }

        p->beat_intensity *= fast_decay;
        if (p->beat_detected) p->beat_intensity = 1.0f;
    }

    return m;
}

static const char *viz_mode_name(Viz_Mode mode)
{
    switch (mode) {
        case VIZ_BARS:     return "Bars";
        case VIZ_CIRCULAR: return "Circular";
        case VIZ_WAVEFORM: return "Waveform";
        default:           return "";
    }
}

static const char *repeat_mode_name(Repeat_Mode mode)
{
    switch (mode) {
        case REPEAT_NONE: return "Repeat: Off";
        case REPEAT_ALL:  return "Repeat: All";
        default:          return "";
    }
}

static void fft_render_circular(Rectangle boundary, size_t m)
{
    float cx = boundary.x + boundary.width / 2;
    float cy = boundary.y + boundary.height / 2;
    float max_radius = (boundary.width < boundary.height ? boundary.width : boundary.height) * 0.4f;
    for (size_t i = 0; i < m; ++i) {
        float t = p->out_smooth[i];
        Color color = fft_colors[i];
        float r = max_radius * (0.3f + 0.7f * t);
        float px = cx + fft_circle_x[i] * r;
        float py = cy + fft_circle_y[i] * r;
        DrawCircleV((Vector2){px, py}, max_radius * 0.03f + max_radius * 0.05f * t, color);
        DrawLineEx((Vector2){cx, cy}, (Vector2){px, py}, max_radius * 0.02f * t, ColorAlpha(color, 0.3f));
    }
}

static void fft_render_waveform(Rectangle boundary, size_t m)
{
    float mid_y = boundary.y + boundary.height / 2;
    float amp = boundary.height * 0.4f;

    for (size_t i = 0; i + 1 < m; ++i) {
        float t0 = p->out_smooth[i];
        float t1 = p->out_smooth[i + 1];
        Color color = fft_colors[i];
        float x0 = boundary.x + (float)i / (m - 1) * boundary.width;
        float x1 = boundary.x + (float)(i + 1) / (m - 1) * boundary.width;
        float y0 = mid_y - t0 * amp;
        float y1 = mid_y - t1 * amp;
        DrawLineEx((Vector2){x0, y0}, (Vector2){x1, y1}, boundary.height * 0.02f, color);
    }

    // Mirror below
    for (size_t i = 0; i + 1 < m; ++i) {
        float t0 = p->out_smooth[i];
        float t1 = p->out_smooth[i + 1];
        Color color = fft_colors_dim[i];
        float x0 = boundary.x + (float)i / (m - 1) * boundary.width;
        float x1 = boundary.x + (float)(i + 1) / (m - 1) * boundary.width;
        float y0 = mid_y + t0 * amp * 0.5f;
        float y1 = mid_y + t1 * amp * 0.5f;
        DrawLineEx((Vector2){x0, y0}, (Vector2){x1, y1}, boundary.height * 0.01f, ColorAlpha(color, 0.3f));
    }
}

static void fft_render(Rectangle boundary, size_t m)
{
    if (m == 0) return;

    switch (p->viz_mode) {
        case VIZ_CIRCULAR:
            fft_render_circular(boundary, m);
            goto beat_flash;
        case VIZ_WAVEFORM:
            fft_render_waveform(boundary, m);
            goto beat_flash;
        default:
            break;
    }

    // The width of a single bar
    float cell_width = boundary.width/m;

    // Display the Bars
    for (size_t i = 0; i < m; ++i) {
        float t = p->out_smooth[i];
        Color color = fft_colors[i];
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
        Color color = fft_colors[i];
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
        Color color = fft_colors[i];
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

    // Beat flash overlay
beat_flash:
    if (p->beat_intensity > 0.01f) {
        DrawRectangleRec(boundary, ColorAlpha(WHITE, p->beat_intensity * 0.15f));
    }

    // Viz mode label (top-right corner)
    {
        float t = GetTime();
        static float mode_switch_time = 0;
        if (IsKeyPressed(KEY_V)) mode_switch_time = t;
        if (t - mode_switch_time < 1.5f) {
            const char *name = viz_mode_name(p->viz_mode);
            float fs = 24;
            Vector2 sz = MeasureTextEx(p->font, name, fs, 0);
            Vector2 pos = { boundary.x + boundary.width - sz.x - 20, boundary.y + 10 };
            DrawRectangleRec((Rectangle){pos.x - 5, pos.y - 5, sz.x + 10, sz.y + 10}, ColorAlpha(COLOR_BACKGROUND, 0.7f));
            DrawTextEx(p->font, name, pos, fs, 0, WHITE);
        }
    }

    // Repeat mode label (below viz mode label)
    if (p->repeat_mode_label_timer > 0) {
        p->repeat_mode_label_timer -= GetFrameTime();
        const char *name = repeat_mode_name(p->repeat_mode);
        float fs = 20;
        Vector2 sz = MeasureTextEx(p->font, name, fs, 0);
        Vector2 pos = { boundary.x + boundary.width - sz.x - 20, boundary.y + 10 + 30 };
        DrawRectangleRec((Rectangle){pos.x - 5, pos.y - 5, sz.x + 10, sz.y + 10}, ColorAlpha(COLOR_BACKGROUND, 0.7f));
        DrawTextEx(p->font, name, pos, fs, 0, ColorAlpha(WHITE, 0.8f));
    }
}

static bool fft_push_frames(const float *samples, size_t frame_count, size_t channels, bool wait)
{
    if (frame_count == 0) return true;
    assert(samples == NULL || channels > 0);

    if (wait) {
        platform_mutex_lock(fft_mutex);
    } else if (!platform_mutex_try_lock(fft_mutex)) {
        // Visualization data is best-effort. Never make the real-time audio
        // thread wait for the UI thread to finish taking an FFT snapshot.
        return false;
    }

    if (frame_count >= FFT_SIZE) {
        size_t first_frame = frame_count - FFT_SIZE;
        if (samples == NULL) {
            memset(p->in_raw, 0, sizeof(p->in_raw));
        } else if (channels == 1) {
            memcpy(p->in_raw, samples + first_frame, sizeof(p->in_raw));
        } else {
            for (size_t i = 0; i < FFT_SIZE; ++i) {
                p->in_raw[i] = samples[(first_frame + i)*channels];
            }
        }
        p->fft_write_cursor = 0;
    } else {
        size_t cursor = p->fft_write_cursor;
        size_t first_count = FFT_SIZE - cursor;
        if (first_count > frame_count) first_count = frame_count;
        size_t second_count = frame_count - first_count;

        if (samples == NULL) {
            memset(p->in_raw + cursor, 0, first_count*sizeof(p->in_raw[0]));
            memset(p->in_raw, 0, second_count*sizeof(p->in_raw[0]));
        } else if (channels == 1) {
            memcpy(p->in_raw + cursor, samples, first_count*sizeof(p->in_raw[0]));
            memcpy(p->in_raw, samples + first_count, second_count*sizeof(p->in_raw[0]));
        } else {
            for (size_t i = 0; i < first_count; ++i) {
                p->in_raw[cursor + i] = samples[i*channels];
            }
            for (size_t i = 0; i < second_count; ++i) {
                p->in_raw[i] = samples[(first_count + i)*channels];
            }
        }

        p->fft_write_cursor = (cursor + frame_count) & (FFT_SIZE - 1);
    }

    platform_mutex_unlock(fft_mutex);
    return true;
}

static void apply_audio_eq(float (*buffer)[2], unsigned int frames)
{
    float gain_low = p->eq_low * 2.0f;
    float gain_mid = p->eq_mid * 2.0f;
    float gain_high = p->eq_high * 2.0f;

    for (unsigned int i = 0; i < frames; ++i) {
        for (int ch = 0; ch < 2; ++ch) {
            float input = buffer[i][ch];

            p->eq_low_lp[ch] += eq_alpha_low * (input - p->eq_low_lp[ch]);
            float low = p->eq_low_lp[ch];

            p->eq_high_lp[ch] += eq_alpha_high * (input - p->eq_high_lp[ch]);
            float low_mid = p->eq_high_lp[ch];

            float mid = low_mid - low;
            float high = input - low_mid;

            buffer[i][ch] = low * gain_low + mid * gain_mid + high * gain_high;
        }
    }
}

// TODO: make sure the audio callback is thread-safe
static void callback(void *bufferData, unsigned int frames)
{
    // https://cdecl.org/?q=float+%28*fs%29%5B2%5D
    float (*fs)[2] = bufferData;

    apply_audio_eq(fs, frames);

    (void)fft_push_frames((float *)fs, frames, 2, false);

#ifdef MUSIALIZER_MICROPHONE
    if (p->capturing) {
        // TODO: according to documentation drwav_write_pcm_frames may not write all the frames.
        // Make sure it does.
        drwav_write_pcm_frames(&p->wav, frames, bufferData);
    }
#endif // MUSIALIZER_MICROPHONE
}

#ifdef MUSIALIZER_MICROPHONE
static void ma_callback(ma_device *pDevice, void *pOutput, const void *pInput,ma_uint32 frameCount)
{
    callback((void*)pInput,frameCount);
    (void)pOutput;
    (void)pDevice;
}
#endif // MUSIALIZER_MICROPHONE

static Track *current_track(void)
{
    if (0 <= p->current_track && (size_t) p->current_track < p->tracks.count) {
        return &p->tracks.items[p->current_track];
    }
    return NULL;
}


static void popup_tray_push(Popup_Tray *pt, const char *message, bool success)
{
    if (pt->count < POPUP_TRAY_CAPACITY) {
        if (pt->begin == 0) {
            pt->begin = POPUP_TRAY_CAPACITY - 1;
        } else {
            pt->begin -= 1;
        }
        pt->count += 1;

        pt->slide += HUD_POPUP_SLIDEIN_SECS;
        PT_FIRST(pt)->lifetime = HUD_POPUP_LIFETIME_SECS + pt->slide;
        PT_FIRST(pt)->success = success;
        strncpy(PT_FIRST(pt)->message, message, sizeof(PT_FIRST(pt)->message) - 1);
        PT_FIRST(pt)->message[sizeof(PT_FIRST(pt)->message) - 1] = '\0';
    }
}

static void snap_segment_inside_other_segment(float ls, float rs, float *lt, float *rt)
{
    float dt = *rt - *lt;
    if (rs < *lt || rs < *rt) {
        *rt = rs;
        *lt = rs - dt;
    }

    if (*lt < ls || *rt < ls) {
        *lt = ls;
        *rt = ls + dt;
    }
}

static void snap_boundary_inside_screen(Rectangle *boundary)
{
    float ls = 0;
    float rs = GetScreenWidth();
    float ts = 0;
    float bs = GetScreenHeight();

    float lt = boundary->x;
    float rt = boundary->x + boundary->width;
    float tt = boundary->y;
    float bt = boundary->y + boundary->height;

    snap_segment_inside_other_segment(ls, rs, &lt, &rt);
    snap_segment_inside_other_segment(ts, bs, &tt, &bt);

    boundary->x = lt;
    boundary->y = tt;
    boundary->width = rt - lt;
    boundary->height = bt - tt;
}

static void align_to_side_of_rect(Rectangle who, Rectangle *what, Side where)
{
    switch (where) {
        case SIDE_BOTTOM: {
            float cx = who.x + who.width/2;
            float cy = who.y + who.height + TOOLTIP_PADDING;
            what->x = cx - what->width/2;
            what->y = cy;
        } break;

        case SIDE_TOP: {
            float cx = who.x + who.width/2;
            float cy = who.y - TOOLTIP_PADDING - what->height;
            what->x = cx - what->width/2;
            what->y = cy;
        } break;

        case SIDE_RIGHT: {
            float cx = who.x + who.width + TOOLTIP_PADDING;
            float cy = who.y + who.height/2;
            what->x = cx;
            what->y = cy - what->height/2;
        } break;

        case SIDE_LEFT: {
            float cx = who.x - TOOLTIP_PADDING - what->width;
            float cy = who.y + who.height/2;
            what->x = cx;
            what->y = cy - what->height/2;
        } break;

        default: {
            assert(0 && "unreachable");
        }
    }
}

static void begin_tooltip_frame(void)
{
    p->tooltip_show = false;
}

static void end_tooltip_frame(void)
{
    if (!p->tooltip_show) return;

    float fontSize = 30;
    float spacing = 0.0;
    Vector2 margin = {20.0, 10.0};
    Vector2 text_size = MeasureTextEx(p->font, p->tooltip_buffer, fontSize, spacing);

    Rectangle tooltip_boundary = {
        .width = text_size.x + margin.x*2.0,
        .height = text_size.y + margin.y*2.0,
    };

    align_to_side_of_rect(p->tooltip_element_boundary, &tooltip_boundary, p->tooltip_align);
    snap_boundary_inside_screen(&tooltip_boundary);

    DrawRectangleRounded(tooltip_boundary, 0.4, 20, COLOR_TOOLTIP_BACKGROUND);
    Vector2 position = {
        .x = tooltip_boundary.x + tooltip_boundary.width/2 - text_size.x/2,
        .y = tooltip_boundary.y + tooltip_boundary.height/2 - text_size.y/2,
    };
    DrawTextEx(p->font, p->tooltip_buffer, position, fontSize, spacing, COLOR_TOOLTIP_FOREGROUND);
}

static void tooltip(Rectangle boundary, const char *text, Side align, bool persists)
{
    if (!(CheckCollisionPointRec(GetMousePosition(), boundary) || persists)) return;
    p->tooltip_show = true;
    // TODO: this may not work properly if text contains UTF-8
    snprintf(p->tooltip_buffer, sizeof(p->tooltip_buffer), "%s", text);
    p->tooltip_align = align;
    p->tooltip_element_boundary = boundary;
}

static void unload_preview_waveform(void)
{
    free(p->preview_waveform);
    p->preview_waveform = NULL;
    p->preview_waveform_count = 0;
    free(p->preview_waveform_path);
    p->preview_waveform_path = NULL;
}

#if defined(_WIN32)
#define FFMPEG_EXECUTABLE "ffmpeg.exe"
#else
#define FFMPEG_EXECUTABLE "ffmpeg"
#endif

static bool convert_audio_with_ffmpeg(const char *source_path, const char *wav_path)
{
    const char *const argv[] = {
        FFMPEG_EXECUTABLE,
        "-nostdin", "-loglevel", "error", "-y",
        "-i", source_path,
        "-f", "wav", wav_path,
        NULL,
    };
    return platform_run_command(argv, true);
}

static bool extract_cover_with_ffmpeg(const char *source_path, const char *cover_path)
{
    const char *const argv[] = {
        FFMPEG_EXECUTABLE,
        "-nostdin", "-loglevel", "error", "-y",
        "-i", source_path,
        "-an", "-frames:v", "1", cover_path,
        NULL,
    };
    return platform_run_command(argv, true);
}

static Music load_music_from_memory_file(const char *file_path, unsigned char **music_data)
{
    Music music = {0};
    unsigned char *data = NULL;
    size_t size = 0;
    if (!platform_read_entire_file(file_path, &data, &size) || size > INT_MAX) {
        free(data);
        return music;
    }

    music = LoadMusicStreamFromMemory(GetFileExtension(file_path), data, (int)size);
    if (!IsMusicValid(music)) {
        free(data);
        return music;
    }

    *music_data = data;
    return music;
}

static Music load_music_from_utf8_path(const char *file_path, unsigned char **music_data)
{
    *music_data = NULL;
    Music music = LoadMusicStream(file_path);
    if (IsMusicValid(music)) return music;
    return load_music_from_memory_file(file_path, music_data);
}

static Wave load_wave_from_utf8_path(const char *file_path)
{
    Wave wave = LoadWave(file_path);
    if (wave.frameCount > 0) return wave;

    unsigned char *data = NULL;
    size_t size = 0;
    if (!platform_read_entire_file(file_path, &data, &size) || size > INT_MAX) {
        free(data);
        return wave;
    }
    wave = LoadWaveFromMemory(GetFileExtension(file_path), data, (int)size);
    free(data);
    return wave;
}

static Wave load_wave_with_ffmpeg_fallback(const char *file_path)
{
    Wave wave = load_wave_from_utf8_path(file_path);
    if (wave.frameCount > 0) return wave;

    char wav_path[4096] = {0};
    if (platform_make_temp_file(wav_path, sizeof(wav_path), "musializer", ".wav")) {
        if (convert_audio_with_ffmpeg(file_path, wav_path)) {
            wave = load_wave_from_utf8_path(wav_path);
        }
        platform_remove_file(wav_path);
    }
    return wave;
}

static Waveform_Peak *create_waveform_cache(const char *file_path, size_t *peak_count_out)
{
    *peak_count_out = 0;
    Wave wave = load_wave_with_ffmpeg_fallback(file_path);
    if (wave.frameCount == 0 || wave.channels == 0) return NULL;

    float *samples = LoadWaveSamples(wave);
    if (samples == NULL) {
        UnloadWave(wave);
        return NULL;
    }

    size_t frame_count = wave.frameCount;
    size_t channel_count = wave.channels;
    size_t peak_count = frame_count < WAVEFORM_CACHE_BINS ? frame_count : WAVEFORM_CACHE_BINS;
    Waveform_Peak *peaks = malloc(peak_count*sizeof(*peaks));
    if (peaks != NULL) {
        for (size_t i = 0; i < peak_count; ++i) {
            size_t begin = i*frame_count/peak_count;
            size_t end = (i + 1)*frame_count/peak_count;
            float min_value = 0.0f;
            float max_value = 0.0f;
            for (size_t frame = begin; frame < end; ++frame) {
                float value = 0.0f;
                for (size_t channel = 0; channel < channel_count; ++channel) {
                    value += samples[frame*channel_count + channel];
                }
                value /= (float)channel_count;
                if (value < min_value) min_value = value;
                if (value > max_value) max_value = value;
            }
            peaks[i] = (Waveform_Peak){ .min = min_value, .max = max_value };
        }
        *peak_count_out = peak_count;
    }

    UnloadWaveSamples(samples);
    UnloadWave(wave);
    return peaks;
}

static Image load_image_from_utf8_path(const char *file_path)
{
    Image image = LoadImage(file_path);
    if (image.data != NULL) return image;

    unsigned char *data = NULL;
    size_t size = 0;
    if (!platform_read_entire_file(file_path, &data, &size) || size > INT_MAX) {
        free(data);
        return image;
    }
    image = LoadImageFromMemory(GetFileExtension(file_path), data, (int)size);
    free(data);
    return image;
}

static void play_track(int index)
{
    if (index < 0 || (size_t)index >= p->tracks.count) return;
    Track *old = current_track();
    if (old && old != &p->tracks.items[index]) {
        p->crossfade_music = old->music;
        p->crossfading = true;
        p->crossfade_timer = 0.0f;
    }
    SeekMusicStream(p->tracks.items[index].music, 0);
    PlayMusicStream(p->tracks.items[index].music);
    if (old && old != &p->tracks.items[index]) {
        SetMusicVolume(p->tracks.items[index].music, 0.0f);
    } else {
        SetMusicVolume(p->tracks.items[index].music, GetMasterVolume());
    }
    p->current_track = index;
    p->now_playing_track = index;
    p->now_playing_timer = 2.5f;
    fft_clean();
    {
        char title[2048];
        snprintf(title, sizeof(title), "Musializer - %s", GetFileName(p->tracks.items[index].file_path));
        SetWindowTitle(title);
    }
}

static void next_track(void)
{
    if (p->tracks.count == 0) return;
    if (p->shuffle && p->tracks.count > 1) {
        int next;
        do {
            next = rand() % (int)p->tracks.count;
        } while (next == p->current_track);
        play_track(next);
    } else {
        int next = p->current_track + 1;
        if (next >= (int)p->tracks.count) {
            if (p->repeat_mode == REPEAT_ALL) next = 0;
            else return;
        }
        play_track(next);
    }
}

static void prev_track(void)
{
    if (p->tracks.count == 0) return;
    int prev = p->current_track - 1;
    if (prev < 0) {
        if (p->repeat_mode == REPEAT_ALL) prev = (int)p->tracks.count - 1;
        else prev = 0;
    }
    play_track(prev);
}

static bool is_audio_extension(const char *ext)
{
    if (!ext || ext[0] != '.') return false;
    const char *e = ext + 1;
    // raylib native
    if ((e[0] == 'w' || e[0] == 'W') && (e[1] == 'a' || e[1] == 'A') && (e[2] == 'v' || e[2] == 'V') && !e[3]) return true;
    if ((e[0] == 'o' || e[0] == 'O') && (e[1] == 'g' || e[1] == 'G') && (e[2] == 'g' || e[2] == 'G') && !e[3]) return true;
    if ((e[0] == 'm' || e[0] == 'M') && (e[1] == 'p' || e[1] == 'P') && (e[2] == '3') && !e[3]) return true;
    if ((e[0] == 'f' || e[0] == 'F') && (e[1] == 'l' || e[1] == 'L') && (e[2] == 'a' || e[2] == 'A') && (e[3] == 'c' || e[3] == 'C') && !e[4]) return true;
    if ((e[0] == 'q' || e[0] == 'Q') && (e[1] == 'o' || e[1] == 'O') && (e[2] == 'a' || e[2] == 'A') && !e[3]) return true;
    if ((e[0] == 'x' || e[0] == 'X') && (e[1] == 'm' || e[1] == 'M') && !e[2]) return true;
    if ((e[0] == 'm' || e[0] == 'M') && (e[1] == 'o' || e[1] == 'O') && (e[2] == 'd' || e[2] == 'D') && !e[3]) return true;
    // FFmpeg fallback
    if ((e[0] == 'm' || e[0] == 'M') && (e[1] == '4' || e[1] == '4') && (e[2] == 'a' || e[2] == 'A') && !e[3]) return true;
    if ((e[0] == 'a' || e[0] == 'A') && (e[1] == 'a' || e[1] == 'A') && (e[2] == 'c' || e[2] == 'C') && !e[3]) return true;
    if ((e[0] == 'w' || e[0] == 'W') && (e[1] == 'm' || e[1] == 'M') && (e[2] == 'a' || e[2] == 'A') && !e[3]) return true;
    if ((e[0] == 'a' || e[0] == 'A') && (e[1] == 'i' || e[1] == 'I') && (e[2] == 'f' || e[2] == 'F') && (e[3] == 'f' || e[3] == 'F') && !e[4]) return true;
    if ((e[0] == 'a' || e[0] == 'A') && (e[1] == 'p' || e[1] == 'P') && (e[2] == 'e' || e[2] == 'E') && !e[3]) return true;
    if ((e[0] == 'o' || e[0] == 'O') && (e[1] == 'p' || e[1] == 'P') && (e[2] == 'u' || e[2] == 'U') && (e[3] == 's' || e[3] == 'S') && !e[4]) return true;
    return false;
}

// ----------------------------------------------------------------------------
// Threaded loader for FFmpeg conversion, cover extraction, and waveform caches
// ----------------------------------------------------------------------------
typedef struct {
    char source_path[4096];
    char wav_path[4096];
    char cover_path[4096];
    bool has_cover;
    bool failed;
    int target_track; // -1 = create new track, >=0 = apply cover to existing track
    bool need_conversion;
    bool need_cover;
    bool need_waveform;
    Waveform_Peak *waveform;
    size_t waveform_count;
} Load_Job;

static struct {
    Platform_Thread *thread;
    Platform_Mutex *mutex;
    Platform_Condition *condition;
    bool running;
    Load_Job *pending;
    size_t pending_count;
    size_t pending_cap;
    Load_Job *completed;
    size_t completed_count;
    size_t completed_cap;
} loader = {0};

static void *loader_thread(void *arg)
{
    (void)arg;
    while (1) {
        Load_Job job;
        platform_mutex_lock(loader.mutex);
        while (loader.pending_count == 0 && loader.running) {
            platform_condition_wait(loader.condition, loader.mutex);
        }
        if (!loader.running) {
            platform_mutex_unlock(loader.mutex);
            return NULL;
        }
        job = loader.pending[0];
        memmove(loader.pending, loader.pending + 1, (loader.pending_count - 1) * sizeof(Load_Job));
        loader.pending_count--;
        platform_mutex_unlock(loader.mutex);

        if (job.need_waveform) {
            job.waveform = create_waveform_cache(job.source_path, &job.waveform_count);
            job.failed = job.waveform == NULL;
            goto done;
        }

        // Process: FFmpeg conversion
        if (job.need_conversion) {
            if (!platform_make_temp_file(job.wav_path, sizeof(job.wav_path), "musializer", ".wav")) {
                job.failed = true;
                goto done;
            }
            if (!convert_audio_with_ffmpeg(job.source_path, job.wav_path)) {
                platform_remove_file(job.wav_path);
                job.wav_path[0] = '\0';
                job.failed = true;
                goto done;
            }
        }

        // Process: cover extraction
        if (job.need_cover && !job.failed) {
            if (platform_make_temp_file(job.cover_path, sizeof(job.cover_path), "musializer_cover", ".jpg")) {
                if (extract_cover_with_ffmpeg(job.source_path, job.cover_path)) {
                    job.has_cover = true;
                } else {
                    platform_remove_file(job.cover_path);
                    job.cover_path[0] = '\0';
                }
            }
        }

    done:
        platform_mutex_lock(loader.mutex);
        if (loader.completed_count >= loader.completed_cap) {
            loader.completed_cap = loader.completed_cap ? loader.completed_cap * 2 : 8;
            loader.completed = realloc(loader.completed, loader.completed_cap * sizeof(Load_Job));
            assert(loader.completed != NULL);
        }
        loader.completed[loader.completed_count++] = job;
        platform_mutex_unlock(loader.mutex);
    }
}

static bool loader_init(void)
{
    loader.mutex = platform_mutex_create();
    loader.condition = platform_condition_create();
    if (loader.mutex == NULL || loader.condition == NULL) goto fail;

    loader.running = true;
    loader.thread = platform_thread_start(loader_thread, NULL);
    if (loader.thread == NULL) goto fail;
    return true;

fail:
    loader.running = false;
    platform_condition_destroy(loader.condition);
    platform_mutex_destroy(loader.mutex);
    loader.condition = NULL;
    loader.mutex = NULL;
    return false;
}

static void enqueue_load_job(const char *file_path, bool need_conversion)
{
    if (!loader.running && !loader_init()) {
        popup_tray_push(&p->pt, "Could not start file loader", false);
        return;
    }

    Load_Job job = {0};
    strncpy(job.source_path, file_path, sizeof(job.source_path) - 1);
    job.need_conversion = need_conversion;
    job.need_cover = true;
    job.target_track = -1;

    platform_mutex_lock(loader.mutex);
    if (loader.pending_count >= loader.pending_cap) {
        loader.pending_cap = loader.pending_cap ? loader.pending_cap * 2 : 8;
        loader.pending = realloc(loader.pending, loader.pending_cap * sizeof(Load_Job));
        assert(loader.pending != NULL);
    }
    loader.pending[loader.pending_count++] = job;
    platform_mutex_unlock(loader.mutex);
    platform_condition_signal(loader.condition);
}

static void enqueue_cover_job(const char *file_path, int track_index)
{
    if (!loader.running && !loader_init()) return;

    Load_Job job = {0};
    strncpy(job.source_path, file_path, sizeof(job.source_path) - 1);
    job.need_conversion = false;
    job.need_cover = true;
    job.target_track = track_index;

    platform_mutex_lock(loader.mutex);
    if (loader.pending_count >= loader.pending_cap) {
        loader.pending_cap = loader.pending_cap ? loader.pending_cap * 2 : 8;
        loader.pending = realloc(loader.pending, loader.pending_cap * sizeof(Load_Job));
        assert(loader.pending != NULL);
    }
    loader.pending[loader.pending_count++] = job;
    platform_mutex_unlock(loader.mutex);
    platform_condition_signal(loader.condition);
}

static void enqueue_waveform_job(const char *file_path)
{
    if (!loader.running && !loader_init()) return;

    Load_Job job = {0};
    strncpy(job.source_path, file_path, sizeof(job.source_path) - 1);
    job.need_waveform = true;

    platform_mutex_lock(loader.mutex);
    // Only the current track's preview matters. Drop stale queued previews and
    // put this one ahead of cover-art work so it appears promptly.
    for (size_t i = 0; i < loader.pending_count; ) {
        if (loader.pending[i].need_waveform) {
            memmove(loader.pending + i, loader.pending + i + 1,
                (loader.pending_count - i - 1)*sizeof(*loader.pending));
            loader.pending_count--;
        } else {
            i++;
        }
    }
    if (loader.pending_count >= loader.pending_cap) {
        loader.pending_cap = loader.pending_cap ? loader.pending_cap * 2 : 8;
        loader.pending = realloc(loader.pending, loader.pending_cap * sizeof(Load_Job));
        assert(loader.pending != NULL);
    }
    memmove(loader.pending + 1, loader.pending, loader.pending_count*sizeof(*loader.pending));
    loader.pending[0] = job;
    loader.pending_count++;
    platform_mutex_unlock(loader.mutex);
    platform_condition_signal(loader.condition);
}

static void process_completed_loads(void)
{
    if (!loader.running) return;

    platform_mutex_lock(loader.mutex);
    size_t n = loader.completed_count;
    Load_Job *completed = loader.completed;
    loader.completed = NULL;
    loader.completed_count = 0;
    loader.completed_cap = 0;
    platform_mutex_unlock(loader.mutex);

    for (size_t i = 0; i < n; i++) {
        Load_Job *job = &completed[i];
        if (job->need_waveform) {
            if (!job->failed && p->preview_waveform_path != NULL &&
                strcmp(p->preview_waveform_path, job->source_path) == 0) {
                free(p->preview_waveform);
                p->preview_waveform = job->waveform;
                p->preview_waveform_count = job->waveform_count;
                job->waveform = NULL;
            }
            free(job->waveform);
        } else if (job->target_track >= 0) {
            // Cover-only job: apply cover to existing track
            ptrdiff_t target_track = -1;
            if ((size_t)job->target_track < p->tracks.count &&
                strcmp(p->tracks.items[job->target_track].file_path, job->source_path) == 0) {
                target_track = job->target_track;
            } else {
                // A track can be reordered or removed while FFmpeg is working.
                for (size_t track_index = 0; track_index < p->tracks.count; ++track_index) {
                    if (strcmp(p->tracks.items[track_index].file_path, job->source_path) == 0) {
                        target_track = (ptrdiff_t)track_index;
                        break;
                    }
                }
            }
            if (!job->failed && job->has_cover && target_track >= 0) {
                Image img = load_image_from_utf8_path(job->cover_path);
                if (img.data != NULL) {
                    p->tracks.items[target_track].cover = LoadTextureFromImage(img);
                    p->tracks.items[target_track].has_cover = true;
                    UnloadImage(img);
                }
            }
            if (job->cover_path[0]) platform_remove_file(job->cover_path);
        } else if (!job->failed) {
            // Full job: create new track
            Music music = {0};
            unsigned char *music_data = NULL;
            if (job->wav_path[0]) {
                music = load_music_from_memory_file(job->wav_path, &music_data);
            }
            if (IsMusicValid(music)) {
                music.looping = false;
                AttachAudioStreamProcessor(music.stream, callback);
                char *path = duplicate_string(job->source_path);
                assert(path != NULL);
                Track track = { .file_path = path, .music = music, .music_data = music_data, .has_cover = false };
                nob_da_append(&p->tracks, track);
                if (job->has_cover && job->cover_path[0]) {
                    Image img = load_image_from_utf8_path(job->cover_path);
                    if (img.data != NULL) {
                        p->tracks.items[p->tracks.count - 1].cover = LoadTextureFromImage(img);
                        p->tracks.items[p->tracks.count - 1].has_cover = true;
                        UnloadImage(img);
                    }
                }
                // Auto-play first track if nothing is playing
                if (current_track() == NULL) {
                    play_track((int)(p->tracks.count - 1));
                }
                popup_tray_push(&p->pt, "Track loaded", true);
            } else {
                free(music_data);
                popup_tray_push(&p->pt, "Could not load track", false);
            }
            if (job->wav_path[0]) platform_remove_file(job->wav_path);
            if (job->cover_path[0]) platform_remove_file(job->cover_path);
        } else {
            popup_tray_push(&p->pt, "Failed to convert file", false);
        }
    }
    free(completed);
}

static void loader_stop(void)
{
    if (!loader.running) return;
    platform_mutex_lock(loader.mutex);
    loader.running = false;
    platform_condition_broadcast(loader.condition);
    platform_mutex_unlock(loader.mutex);
    platform_thread_join(loader.thread);

    for (size_t i = 0; i < loader.pending_count; ++i) {
        platform_remove_file(loader.pending[i].wav_path);
        platform_remove_file(loader.pending[i].cover_path);
        free(loader.pending[i].waveform);
    }
    for (size_t i = 0; i < loader.completed_count; ++i) {
        platform_remove_file(loader.completed[i].wav_path);
        platform_remove_file(loader.completed[i].cover_path);
        free(loader.completed[i].waveform);
    }
    free(loader.pending);
    free(loader.completed);
    platform_condition_destroy(loader.condition);
    platform_mutex_destroy(loader.mutex);
    memset(&loader, 0, sizeof(loader));
}
// ----------------------------------------------------------------------------

static void load_track_from_path(const char *file_path)
{
    if (!is_audio_extension(GetFileExtension(file_path))) {
        popup_tray_push(&p->pt, "Unsupported file format", false);
        return;
    }

    // Try native raylib load first (fast, on main thread)
    unsigned char *music_data = NULL;
    Music music = load_music_from_utf8_path(file_path, &music_data);
    if (IsMusicValid(music)) {
        music.looping = false;
        AttachAudioStreamProcessor(music.stream, callback);
        char *path = duplicate_string(file_path);
        assert(path != NULL);
        nob_da_append(&p->tracks, (CLITERAL(Track){
            .file_path = path,
            .music = music,
            .music_data = music_data,
            .has_cover = false,
        }));
        // Extract cover art in background thread
        enqueue_cover_job(file_path, (int)(p->tracks.count - 1));
        return;
    }

    // Need FFmpeg conversion — enqueue to background thread
    enqueue_load_job(file_path, true);
}

static void load_m3u_playlist(const char *file_path)
{
    FILE *f = platform_fopen(file_path, "r");
    if (!f) {
        popup_tray_push(&p->pt, "Could not open playlist", false);
        return;
    }
    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = '\0';
        if (len == 0 || line[0] == '#') continue;
        load_track_from_path(line);
    }
    fclose(f);

    if (current_track() == NULL && p->tracks.count > 0) {
        p->current_track = 0;
        PlayMusicStream(p->tracks.items[0].music);
    }
}

static void startup_autoplay(void)
{
    if (current_track() == NULL && p->tracks.count > 0) {
        play_track(0);
    }
}

static void open_files_dialog(void)
{
    char const *filter_patterns[] = {"*.wav", "*.ogg", "*.mp3", "*.qoa", "*.xm", "*.mod", "*.flac", "*.m4a", "*.aac", "*.wma", "*.aiff", "*.ape", "*.opus", "*.m3u", "*.m3u8"};
    char *result = tinyfd_openFileDialog(
        "Select music files",
        "./",
        NOB_ARRAY_LEN(filter_patterns),
        filter_patterns,
        "audio files",
        1);
    if (!result) return;
    while (*result) {
        char *next = strchr(result, '|');
        if (next) *next = '\0';
        const char *ext = GetFileExtension(result);
        if (ext && (strcmp(ext, ".m3u") == 0 || strcmp(ext, ".m3u8") == 0)) {
            load_m3u_playlist(result);
        } else {
            load_track_from_path(result);
        }
        if (!next) break;
        result = next + 1;
    }
    startup_autoplay();
}

static void format_time(char *buf, size_t buf_sz, float secs)
{
    if (secs < 0) secs = 0;
    int m = (int)(secs / 60);
    int s = (int)secs % 60;
    snprintf(buf, buf_sz, "%d:%02d", m, s);
}

static void timeline(Rectangle timeline_boundary, Track *track)
{
    DrawRectangleRec(timeline_boundary, COLOR_TIMELINE_BACKGROUND);

    // Decode and downsample the waveform on the loader thread. Decoding a long
    // track here used to stall both rendering and calls to UpdateMusicStream().
    if (p->preview_waveform_path == NULL || strcmp(p->preview_waveform_path, track->file_path) != 0) {
        unload_preview_waveform();
        p->preview_waveform_path = duplicate_string(track->file_path);
        if (p->preview_waveform_path != NULL) enqueue_waveform_job(track->file_path);
    }

    // Draw the fixed-size peak cache instead of rescanning every sample in the
    // decoded track on every frame.
    if (p->preview_waveform != NULL && p->preview_waveform_count > 0) {
        size_t peak_count = p->preview_waveform_count;
        int width = (int)timeline_boundary.width;
        float h = timeline_boundary.height;
        float mid_y = timeline_boundary.y + h / 2;
        Color wave_color = ColorAlpha(WHITE, 0.25);

        for (int x = 0; x < width; ++x) {
            size_t start = (size_t)x*peak_count/(size_t)width;
            size_t end = (size_t)(x + 1)*peak_count/(size_t)width;
            if (start >= peak_count) continue;
            if (end <= start) end = start + 1;
            if (end > peak_count) end = peak_count;

            float min_val = 0.0f, max_val = 0.0f;
            for (size_t i = start; i < end; ++i) {
                if (p->preview_waveform[i].min < min_val) min_val = p->preview_waveform[i].min;
                if (p->preview_waveform[i].max > max_val) max_val = p->preview_waveform[i].max;
            }

            float y0 = mid_y - max_val * h / 2;
            float y1 = mid_y - min_val * h / 2;
            if (y0 > y1) { float tmp = y0; y0 = y1; y1 = tmp; }
            DrawRectangle((int)timeline_boundary.x + x, (int)y0, 1, (int)(y1 - y0 + 1), wave_color);
        }
    }

    float played = GetMusicTimePlayed(track->music);
    float len = GetMusicTimeLength(track->music);
    float x = played/len*GetScreenWidth();
    Vector2 startPos = {
        .x = x,
        .y = timeline_boundary.y
    };
    Vector2 endPos = {
        .x = x,
        .y = timeline_boundary.y + timeline_boundary.height
    };
    DrawLineEx(startPos, endPos, 10, COLOR_TIMELINE_CURSOR);

    // Time labels
    {
        float fs = 18;
        char buf[32];

        // Current time at cursor
        format_time(buf, sizeof(buf), played);
        Vector2 cur_sz = MeasureTextEx(p->font, buf, fs, 0);
        float cur_x = x - cur_sz.x/2;
        if (cur_x < timeline_boundary.x) cur_x = timeline_boundary.x;
        if (cur_x + cur_sz.x > timeline_boundary.x + timeline_boundary.width)
            cur_x = timeline_boundary.x + timeline_boundary.width - cur_sz.x;
        float cur_y = timeline_boundary.y + 5;
        DrawRectangleRec((Rectangle){cur_x - 3, cur_y - 2, cur_sz.x + 6, cur_sz.y + 4}, ColorAlpha(COLOR_TIMELINE_BACKGROUND, 0.8f));
        DrawTextEx(p->font, buf, (Vector2){cur_x, cur_y}, fs, 0, COLOR_TIMELINE_CURSOR);

        // Remaining time at right edge
        format_time(buf, sizeof(buf), len - played);
        Vector2 rem_sz = MeasureTextEx(p->font, buf, fs, 0);
        float rem_x = timeline_boundary.x + timeline_boundary.width - rem_sz.x - 5;
        float rem_y = timeline_boundary.y + timeline_boundary.height - rem_sz.y - 5;
        DrawRectangleRec((Rectangle){rem_x - 3, rem_y - 2, rem_sz.x + 6, rem_sz.y + 4}, ColorAlpha(COLOR_TIMELINE_BACKGROUND, 0.8f));
        DrawTextEx(p->font, buf, (Vector2){rem_x, rem_y}, fs, 0, ColorAlpha(WHITE, 0.6f));
    }

    static bool dragging = false;
    Vector2 mouse = GetMousePosition();
    if (dragging) {
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            dragging = false;
        } else {
            float t = (mouse.x - timeline_boundary.x)/timeline_boundary.width;
            if (t < 0) t = 0;
            if (t > 1) t = 1;
            SeekMusicStream(track->music, t*len);
        }
    } else if (CheckCollisionPointRec(mouse, timeline_boundary)) {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            dragging = true;
            float t = (mouse.x - timeline_boundary.x)/timeline_boundary.width;
            SeekMusicStream(track->music, t*len);
        }
    }

    // TODO: enable the user to render a specific region instead of the whole song.
}

typedef enum {
    BS_NONE      = 0, // 00
    BS_HOVEROVER = 1, // 01
    BS_CLICKED   = 2, // 10
} Button_State;

static int button_with_id(uint64_t id, Rectangle boundary)
{
    Vector2 mouse = GetMousePosition();
    int hoverover = CheckCollisionPointRec(mouse, boundary);

    int clicked = 0;
    if (p->active_button_id == 0) {
        if (hoverover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            p->active_button_id = id;
        }
    } else if (p->active_button_id == id) {
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            p->active_button_id = 0;
            if (hoverover) clicked = 1;
        }
    }

    return (clicked<<1) | hoverover;
}

#define DJB2_INIT 5381

static uint64_t djb2(uint64_t hash, const void *buf, size_t buf_sz)
{
    const uint8_t *bytes = buf;
    for (size_t i = 0; i < buf_sz; ++i) {
        hash = hash*33 + bytes[i];
    }
    return hash;
}

static int button_with_location(const char *file, int line, Rectangle boundary)
{
    uint64_t id = DJB2_INIT;
    id = djb2(id, file, strlen(file));
    id = djb2(id, &line, sizeof(line));
    return button_with_id(id, boundary);
}

#define button(boundary) button_with_location(__FILE__, __LINE__, boundary)

// NOTE: This is literally DrawTextEx() copy-pasted from Raylib itself but with the
// max_width support and without newlines
void track_label(Font font, const char *text, Vector2 position, float fontSize, Color tint)
{
    if (font.texture.id == 0) font = GetFontDefault();  // Security check in case of not valid font

    float spacing = 0;

    int size = TextLength(text);    // Total size in bytes of the text, scanned by codepoints in loop

    int textOffsetY = 0;            // Offset between lines (on linebreak '\n')
    float textOffsetX = 0.0f;       // Offset X to next character to draw

    float scaleFactor = fontSize/font.baseSize;         // Character quad scaling factor

    for (int i = 0; i < size;)
    {
        // Get next codepoint from byte string and glyph index in font
        int codepointByteCount = 0;
        int codepoint = GetCodepointNext(&text[i], &codepointByteCount);
        int index = GetGlyphIndex(font, codepoint);

        if (codepoint == '\n') codepoint = ' '; // Treat newlines as spaces

        if ((codepoint != ' ') && (codepoint != '\t'))
        {
            DrawTextCodepoint(font, codepoint, (Vector2){ position.x + textOffsetX, position.y + textOffsetY }, fontSize, tint);
        }

        if (font.glyphs[index].advanceX == 0) textOffsetX += ((float)font.recs[index].width*scaleFactor + spacing);
        else textOffsetX += ((float)font.glyphs[index].advanceX*scaleFactor + spacing);

        i += codepointByteCount;   // Move text bytes counter to next codepoint
    }
}

#define tracks_panel(panel_boundary) \
    tracks_panel_with_location(__FILE__, __LINE__, panel_boundary)
static void tracks_panel_with_location(const char *file, int line, Rectangle panel_boundary)
{
    DrawRectangleRec(panel_boundary, COLOR_TRACK_PANEL_BACKGROUND);

    Vector2 mouse = GetMousePosition();
    bool any_mouse_press = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

    float scroll_bar_width = panel_boundary.width*0.03;
    float item_size = panel_boundary.width*0.2;
    float visible_area_size = panel_boundary.height;
    float entire_scrollable_area = item_size*p->tracks.count;

    static float panel_scroll = 0;
    static float panel_velocity = 0;
    panel_velocity *= 0.9;
    if (CheckCollisionPointRec(mouse, panel_boundary)) {
        panel_velocity += GetMouseWheelMove()*item_size*8;
    }
    panel_scroll -= panel_velocity*GetFrameTime();

    static bool scrolling = false;
    static float scrolling_mouse_offset = 0.0f;
    if (scrolling) {
        panel_scroll = (mouse.y - panel_boundary.y - scrolling_mouse_offset)/visible_area_size*entire_scrollable_area;
    }

    // Drag-and-drop state
    static ptrdiff_t drag_from = -1;
    static float drag_start_mouse_y = 0;
    static bool is_dragging = false;

    // End drag on mouse release
    if (drag_from >= 0 && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        if (is_dragging) {
            size_t from = (size_t)drag_from;
            float list_y = mouse.y - panel_boundary.y + panel_scroll - item_size*0.1f;
            ptrdiff_t raw_target = (ptrdiff_t)(list_y / item_size);
            if (raw_target < 0) raw_target = 0;
            if ((size_t)raw_target > p->tracks.count) raw_target = (ptrdiff_t)p->tracks.count;
            size_t to = (size_t)raw_target;

            if (from < to) to--;
            if (to != from && to < p->tracks.count) {
                Track tmp = p->tracks.items[from];
                if (from < to) {
                    memmove(&p->tracks.items[from], &p->tracks.items[from + 1],
                            (to - from) * sizeof(Track));
                } else {
                    memmove(&p->tracks.items[to + 1], &p->tracks.items[to],
                            (from - to) * sizeof(Track));
                }
                p->tracks.items[to] = tmp;
                if (p->current_track == (int)from) {
                    p->current_track = (int)to;
                } else if (from < to) {
                    if (p->current_track > (int)from && p->current_track <= (int)to) {
                        p->current_track--;
                    }
                } else {
                    if (p->current_track >= (int)to && p->current_track < (int)from) {
                        p->current_track++;
                    }
                }
            }
        }
        drag_from = -1;
        is_dragging = false;
    }

    float min_scroll = 0;
    if (panel_scroll < min_scroll) panel_scroll = min_scroll;
    float max_scroll = entire_scrollable_area - visible_area_size;
    if (max_scroll < 0) max_scroll = 0;
    if (panel_scroll > max_scroll) panel_scroll = max_scroll;
    float panel_padding = item_size*0.1;

    uint64_t id = DJB2_INIT;
    id = djb2(id, file, strlen(file));
    id = djb2(id, &line, sizeof(line));

    ptrdiff_t remove_index = -1;
    ptrdiff_t move_from = -1;

    for (size_t i = 0; i < p->tracks.count; ++i) {
        Rectangle item_boundary = {
            .x = panel_boundary.x + panel_padding,
            .y = i*item_size + panel_boundary.y + panel_padding - panel_scroll,
            .width = panel_boundary.width - panel_padding*2 - scroll_bar_width,
            .height = item_size - panel_padding*2,
        };

        if (item_boundary.y + item_boundary.height < panel_boundary.y ||
            item_boundary.y > panel_boundary.y + panel_boundary.height)
            continue;

        bool is_current = ((int)i == p->current_track);
        uint64_t item_id = djb2(id, &i, sizeof(i));

        // Manually compute hover (doesn't consume active_button_id)
        Rectangle clipped_item = GetCollisionRec(panel_boundary, item_boundary);
        bool item_hover = CheckCollisionPointRec(mouse, clipped_item);

        // Action button dimensions (needed for layout, before item row check)
        float cover_size = item_boundary.height * 0.75;
        float cover_pad = (item_boundary.height - cover_size) / 2;
        float text_x = item_boundary.x + cover_size + cover_pad * 3;
        float btn_w = item_hover && p->tracks.count > 1 ? item_boundary.height * 0.4 * 3 : 0;

        // Action button STATE detection (BEFORE item row, so they claim active_button_id)
        int action_bs_up = 0, action_bs_dn = 0, action_bs_rm = 0;
        if (item_hover && p->tracks.count > 1) {
            float bsize = item_boundary.height * 0.4;
            float bx = item_boundary.x + item_boundary.width - bsize * 3 - 5;
            float by = item_boundary.y + (item_boundary.height - bsize) / 2;

            if (i > 0) {
                Rectangle btn = { bx, by, bsize, bsize };
                uint64_t bid = djb2(item_id, "up", 2);
                action_bs_up = button_with_id(bid, btn);
                if (action_bs_up & BS_CLICKED) move_from = (ptrdiff_t)i;
            }
            if (i + 1 < p->tracks.count) {
                Rectangle btn = { bx + bsize, by, bsize, bsize };
                uint64_t bid = djb2(item_id, "dn", 2);
                action_bs_dn = button_with_id(bid, btn);
                if (action_bs_dn & BS_CLICKED) move_from = (ptrdiff_t)i + 1;
            }
            {
                Rectangle btn = { bx + bsize * 2, by, bsize, bsize };
                uint64_t bid = djb2(item_id, "rm", 2);
                action_bs_rm = button_with_id(bid, btn);
                if (action_bs_rm & BS_CLICKED) remove_index = (ptrdiff_t)i;
            }
        }

        // Drag start: left-click on item body (not on action buttons)
        if (drag_from < 0 && item_hover && any_mouse_press) {
            bool on_action_btn = false;
            if (p->tracks.count > 1) {
                float dbsize = item_boundary.height * 0.4f;
                float dbx = item_boundary.x + item_boundary.width - dbsize * 3.f - 5.f;
                float dby = item_boundary.y + (item_boundary.height - dbsize) / 2.f;
                if (i > 0)                on_action_btn = on_action_btn || CheckCollisionPointRec(mouse, (Rectangle){dbx, dby, dbsize, dbsize});
                if (i + 1 < p->tracks.count) on_action_btn = on_action_btn || CheckCollisionPointRec(mouse, (Rectangle){dbx + dbsize, dby, dbsize, dbsize});
                on_action_btn = on_action_btn || CheckCollisionPointRec(mouse, (Rectangle){dbx + dbsize * 2.f, dby, dbsize, dbsize});
            }
            if (!on_action_btn) {
                drag_from = (ptrdiff_t)i;
                drag_start_mouse_y = mouse.y;
            }
        }

        // Item row button (won't steal click from action buttons)
        int state = button_with_id(item_id, clipped_item);

        Color color;
        if (is_current) {
            color = COLOR_TRACK_BUTTON_SELECTED;
        } else if (state & BS_HOVEROVER) {
            color = COLOR_TRACK_BUTTON_HOVEROVER;
        } else {
            color = COLOR_TRACK_BUTTON_BACKGROUND;
        }

        if (state & BS_CLICKED && !is_current) {
            play_track((int)i);
        }

        if (is_dragging && drag_from == (ptrdiff_t)i) continue;

        DrawRectangleRounded(item_boundary, 0.2, 20, color);

        // Cover art thumbnail
        if (p->tracks.items[i].has_cover) {
            Rectangle dest = {
                .x = item_boundary.x + cover_pad,
                .y = item_boundary.y + cover_pad,
                .width = cover_size,
                .height = cover_size,
            };
            Rectangle source = { 0, 0, (float)p->tracks.items[i].cover.width, (float)p->tracks.items[i].cover.height };
            DrawTexturePro(p->tracks.items[i].cover, source, dest, (Vector2){0}, 0, WHITE);
        } else {
            Rectangle dest = {
                .x = item_boundary.x + cover_pad,
                .y = item_boundary.y + cover_pad,
                .width = cover_size,
                .height = cover_size,
            };
            DrawRectangleRounded(dest, 0.2, 10, ColorAlpha(WHITE, 0.05));
        }

        // Draw action buttons (AFTER item background)
        if (item_hover && p->tracks.count > 1) {
            float bsize = item_boundary.height * 0.4;
            float bx = item_boundary.x + item_boundary.width - bsize * 3 - 5;
            float by = item_boundary.y + (item_boundary.height - bsize) / 2;

            if (i > 0) {
                Rectangle btn = { bx, by, bsize, bsize };
                Color bc = (action_bs_up & BS_HOVEROVER) ? COLOR_ACCENT : ColorAlpha(WHITE, 0.4f);
                DrawRectangleRounded(btn, 0.3, 10, bc);
                float lw = bsize * 0.15f, cx = btn.x + btn.width / 2, cy = btn.y + btn.height / 2, ht = bsize * 0.25f;
                DrawLineEx((Vector2){cx, cy - ht}, (Vector2){cx - ht, cy}, lw, WHITE);
                DrawLineEx((Vector2){cx, cy - ht}, (Vector2){cx + ht, cy}, lw, WHITE);
            }
            if (i + 1 < p->tracks.count) {
                Rectangle btn = { bx + bsize, by, bsize, bsize };
                Color bc = (action_bs_dn & BS_HOVEROVER) ? COLOR_ACCENT : ColorAlpha(WHITE, 0.4f);
                DrawRectangleRounded(btn, 0.3, 10, bc);
                float lw = bsize * 0.15f, cx = btn.x + btn.width / 2, cy = btn.y + btn.height / 2, ht = bsize * 0.25f;
                DrawLineEx((Vector2){cx, cy + ht}, (Vector2){cx - ht, cy}, lw, WHITE);
                DrawLineEx((Vector2){cx, cy + ht}, (Vector2){cx + ht, cy}, lw, WHITE);
            }
            {
                Rectangle btn = { bx + bsize * 2, by, bsize, bsize };
                Color bc = (action_bs_rm & BS_HOVEROVER) ? (Color){255, 60, 60, 255} : ColorAlpha(WHITE, 0.4f);
                DrawRectangleRounded(btn, 0.3, 10, bc);
                float lw = bsize * 0.15f, cx = btn.x + btn.width / 2, cy = btn.y + btn.height / 2, ht = bsize * 0.25f;
                DrawLineEx((Vector2){cx - ht, cy - ht}, (Vector2){cx + ht, cy + ht}, lw, WHITE);
                DrawLineEx((Vector2){cx + ht, cy - ht}, (Vector2){cx - ht, cy + ht}, lw, WHITE);
            }
        }

        float text_padding = 5;
        float max_width = item_boundary.x + item_boundary.width - text_x - text_padding - btn_w;
        if (max_width < 10) max_width = 10;

        const char *text = GetFileName(p->tracks.items[i].file_path);
        float fontSize = item_boundary.height * 0.45;
        Vector2 size = MeasureTextEx(p->font, text, fontSize, 0);

        // Track label
        {
            Vector2 position = {
                .x = text_x,
                .y = item_boundary.y + item_boundary.height*0.5 - size.y*0.5,
            };
            if (size.x > max_width) {
                BeginScissorMode(position.x, position.y, max_width, item_boundary.height);
                if (state & BS_HOVEROVER) {
                    static float dt = 0;
                    static uint64_t hovered_label_id = 0;
                    static int px_shift = 0;
                    static bool scroll_left = true;
                    dt += GetFrameTime();
                    if (item_id != hovered_label_id) {
                        px_shift = 0;
                        scroll_left = true;
                        hovered_label_id = item_id;
                    } else {
                        if (dt > TRACKLABEL_SCROLL_SECS) {
                            dt = 0.0f;
                            if ((abs(px_shift) >= (int)(size.x - max_width + 10)) || (px_shift == 10)) {
                                scroll_left = !scroll_left;
                            }
                            scroll_left ? --px_shift : ++px_shift;
                        }
                    }
                    position.x += (float)px_shift;
                }
                track_label(p->font, text, position, fontSize, WHITE);
                EndScissorMode();
            } else {
                track_label(p->font, text, position, fontSize, WHITE);
            }
        }

        // Duration
        {
            float len = GetMusicTimeLength(p->tracks.items[i].music);
            if (len > 0) {
                char buf[32];
                format_time(buf, sizeof(buf), len);
                float fs = item_boundary.height * 0.35;
                Vector2 sz = MeasureTextEx(p->font, buf, fs, 0);
                float dx = item_boundary.x + item_boundary.width - text_padding - btn_w - sz.x - 5;
                if (dx >= text_x + 10) {
                    DrawTextEx(p->font, buf, (Vector2){dx, item_boundary.y + item_boundary.height*0.5 - sz.y*0.5}, fs, 0, ColorAlpha(WHITE, 0.4f));
                }
            }
        }
    }

    // Drag visuals
    if (drag_from >= 0 && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        if (!is_dragging && fabsf(mouse.y - drag_start_mouse_y) > item_size * 0.2f) {
            is_dragging = true;
        }
        if (is_dragging) {
            float list_y = mouse.y - (panel_boundary.y + panel_padding - panel_scroll);
            ptrdiff_t zone = (ptrdiff_t)(list_y / item_size + 0.5f);
            if (zone < 0) zone = 0;
            if ((size_t)zone > p->tracks.count) zone = (ptrdiff_t)p->tracks.count;
            if (zone == drag_from || zone == drag_from + 1) zone = -1;

            // Auto-scroll when near panel edges
            float edge_margin = item_size * 0.5f;
            if (mouse.y < panel_boundary.y + edge_margin) {
                panel_velocity = -item_size * 10;
            } else if (mouse.y > panel_boundary.y + panel_boundary.height - edge_margin) {
                panel_velocity = item_size * 10;
            }

            // Drop indicator line
            if (zone >= 0) {
                float line_y = (float)zone * item_size + panel_boundary.y + panel_padding - panel_scroll;
                Rectangle line = {
                    panel_boundary.x + panel_padding,
                    line_y - 2,
                    panel_boundary.width - panel_padding * 2 - scroll_bar_width,
                    3,
                };
                BeginScissorMode(panel_boundary.x, panel_boundary.y, panel_boundary.width - scroll_bar_width, panel_boundary.height);
                DrawRectangleRec(line, COLOR_ACCENT);
                EndScissorMode();
            }

            // Dragged item at cursor
            Track *dt = &p->tracks.items[drag_from];
            float drag_item_h = item_size - panel_padding * 2;
            float drag_item_w = panel_boundary.width - panel_padding * 2 - scroll_bar_width;
            Rectangle drag_boundary = {
                mouse.x - drag_item_w * 0.3f,
                mouse.y - drag_item_h * 0.1f,
                drag_item_w,
                drag_item_h,
            };
            if (drag_boundary.x < panel_boundary.x + panel_padding) drag_boundary.x = panel_boundary.x + panel_padding;
            if (drag_boundary.x + drag_boundary.width > panel_boundary.x + panel_boundary.width - panel_padding - scroll_bar_width)
                drag_boundary.x = panel_boundary.x + panel_boundary.width - panel_padding - scroll_bar_width - drag_boundary.width;
            BeginScissorMode(panel_boundary.x, panel_boundary.y, panel_boundary.width - scroll_bar_width, panel_boundary.height);
            DrawRectangleRounded(drag_boundary, 0.2, 20, ColorAlpha(COLOR_ACCENT, 0.7f));
            float dcover = drag_boundary.height * 0.75f;
            float dpad = (drag_boundary.height - dcover) / 2;
            if (dt->has_cover) {
                Rectangle dsrc = {0, 0, (float)dt->cover.width, (float)dt->cover.height};
                Rectangle ddest = {drag_boundary.x + dpad, drag_boundary.y + dpad, dcover, dcover};
                DrawTexturePro(dt->cover, dsrc, ddest, (Vector2){0}, 0, ColorAlpha(WHITE, 0.8f));
            }
            const char *dtext = GetFileName(dt->file_path);
            float dfs = drag_boundary.height * 0.45f;
            Vector2 dtext_sz = MeasureTextEx(p->font, dtext, dfs, 0);
            float dtx = drag_boundary.x + dcover + dpad * 3;
            float dty = drag_boundary.y + drag_boundary.height * 0.5f - dtext_sz.y * 0.5f;
            track_label(p->font, dtext, (Vector2){dtx, dty}, dfs, ColorAlpha(WHITE, 0.8f));
            EndScissorMode();
        }
    }

    // Deferred actions
    if (remove_index >= 0) {
        bool was_current = p->current_track == (int)remove_index;
        Track *t = &p->tracks.items[remove_index];
        if (t->has_cover) UnloadTexture(t->cover);
        DetachAudioStreamProcessor(t->music.stream, callback);

        // Stop crossfade if the removed track is involved
        if (p->crossfading) {
            StopMusicStream(p->crossfade_music);
            p->crossfading = false;
        }

        if (was_current) StopMusicStream(t->music);
        UnloadMusicStream(t->music);
        free(t->music_data);
        free(t->file_path);
        memmove(&p->tracks.items[remove_index], &p->tracks.items[remove_index + 1],
                (p->tracks.count - (size_t)remove_index - 1) * sizeof(Track));
        p->tracks.count--;
        if (was_current) {
            if ((size_t)remove_index < p->tracks.count) {
                SeekMusicStream(p->tracks.items[remove_index].music, 0);
                PlayMusicStream(p->tracks.items[remove_index].music);
                p->current_track = (int)remove_index;
            } else if (p->tracks.count > 0) {
                p->current_track = (int)p->tracks.count - 1;
                SeekMusicStream(p->tracks.items[p->current_track].music, 0);
                PlayMusicStream(p->tracks.items[p->current_track].music);
            } else {
                p->current_track = -1;
            }
        } else if (p->current_track > (int)remove_index) {
            p->current_track--;
        }
    }
    if (move_from >= 0) {
        size_t from = (size_t)move_from;
        size_t to = (from > 0 && from < p->tracks.count) ? from - 1 : (from + 1 < p->tracks.count ? from + 1 : from);
        if (to != from) {
            Track tmp = p->tracks.items[from];
            if (from < to) {
                memmove(&p->tracks.items[from], &p->tracks.items[from + 1],
                        (to - from) * sizeof(Track));
            } else {
                memmove(&p->tracks.items[to + 1], &p->tracks.items[to],
                        (from - to) * sizeof(Track));
            }
            p->tracks.items[to] = tmp;
            if (p->current_track == (int)from) {
                p->current_track = (int)to;
            } else if (p->current_track == (int)to) {
                p->current_track = (int)from;
            }
        }
    }

    // TODO: up and down clickable buttons on the scrollbar

    if (entire_scrollable_area > visible_area_size) { // Is scrolling needed
        float t = visible_area_size/entire_scrollable_area;
        float q = panel_scroll/entire_scrollable_area;
        Rectangle scroll_bar_area = {
            .x = panel_boundary.x + panel_boundary.width - scroll_bar_width,
            .y = panel_boundary.y,
            .width = scroll_bar_width,
            .height = panel_boundary.height,
        };
        // TODO: some sort of color for the scroll bar background
        //DrawRectangleRounded(scroll_bar_area, 0.8, 20, RED);
        Rectangle scroll_bar_boundary = {
            .x = panel_boundary.x + panel_boundary.width - scroll_bar_width,
            .y = panel_boundary.y + panel_boundary.height*q,
            .width = scroll_bar_width,
            .height = panel_boundary.height*t,
        };
        DrawRectangleRounded(scroll_bar_boundary, 0.8, 20, COLOR_TRACK_BUTTON_BACKGROUND);

        if (scrolling) {
            if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
                scrolling = false;
            }
        } else {
            if (CheckCollisionPointRec(mouse, scroll_bar_boundary)) {
                if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                    scrolling = true;
                    scrolling_mouse_offset = mouse.y - scroll_bar_boundary.y;
                }
            } else if (CheckCollisionPointRec(mouse, scroll_bar_area)) {
                if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
                    if (mouse.y < scroll_bar_boundary.y) {
                        panel_velocity += item_size*16;
                    } else if (scroll_bar_boundary.y + scroll_bar_boundary.height < mouse.y){
                        panel_velocity += -item_size*16;
                    }
                }
            }
        }
    }

}

#define fullscreen_button(preview_boundary) \
    fullscreen_button_with_loc(__FILE__, __LINE__, preview_boundary)
static int fullscreen_button_with_loc(const char *file, int line, Rectangle fullscreen_button_boundary)
{
    uint64_t id = DJB2_INIT;
    id = djb2(id, file, strlen(file));
    id = djb2(id, &line, sizeof(line));

    int state = button_with_id(id, fullscreen_button_boundary);

    float icon_size = 512;
    float scale = HUD_BUTTON_SIZE/icon_size*HUD_ICON_SCALE;
    Rectangle dest = {
        fullscreen_button_boundary.x + fullscreen_button_boundary.width/2 - icon_size*scale/2,
        fullscreen_button_boundary.y + fullscreen_button_boundary.height/2 - icon_size*scale/2,
        icon_size*scale,
        icon_size*scale
    };
    size_t icon_index;
    if (!p->fullscreen) {
        if (!(state & BS_HOVEROVER)) {
            icon_index = 0;
        } else {
            icon_index = 1;
        }
    } else {
        if (!(state & BS_HOVEROVER)) {
            icon_index = 2;
        } else {
            icon_index = 3;
        }
    }
    Rectangle source = {icon_size*icon_index, 0, icon_size, icon_size};
    DrawTexturePro(p->icon_textures[UI_ICON_FULLSCREEN], source, dest, CLITERAL(Vector2){0}, 0, ColorBrightness(WHITE, -0.10));

    if (p->fullscreen) {
        tooltip(fullscreen_button_boundary, "Collapse [F]", SIDE_TOP, false);
    } else {
        tooltip(fullscreen_button_boundary, "Expand [F]", SIDE_TOP, false);
    }

    return state;
}

static float slider_get_value(float x, float lox, float hix)
{
    if (x < lox) x = lox;
    if (x > hix) x = hix;
    x -= lox;
    x /= hix - lox;
    return x;
}

static bool horz_slider(Rectangle boundary, float *value, bool *dragging)
{
    bool updated = false;

    Vector2 mouse = GetMousePosition();

    Vector2 startPos = {
        .x = boundary.x + boundary.height/2,
        .y = boundary.y + boundary.height/2,
    };
    Vector2 endPos = {
        .x = boundary.x + boundary.width - boundary.height/2,
        .y = boundary.y + boundary.height/2,
    };
    Color color = WHITE;
    DrawLineEx(startPos, endPos, boundary.height*0.10, color);
    Vector2 center = {
        .x = startPos.x + (endPos.x - startPos.x)*(*value),
        .y = startPos.y,
    };
    float radius = boundary.height/4;
    {
        Texture2D texture = { rlGetTextureIdDefault(), 1, 1, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
        SetShaderValue(p->circle, p->circle_radius_location, (float[1]){ 0.43f }, SHADER_UNIFORM_FLOAT);
        SetShaderValue(p->circle, p->circle_power_location, (float[1]){ 2.0f }, SHADER_UNIFORM_FLOAT);
        BeginShaderMode(p->circle);
        Rectangle source = {0, 0, 1, 1};
        Rectangle dest = { center.x - radius, center.y - radius, radius*2, radius*2 };
        Vector2 origin = {0};
        DrawTexturePro(texture, source, dest, origin, 0, color);
        EndShaderMode();
    }

    if (!*dragging) {
        if (CheckCollisionPointCircle(mouse, center, radius)) {
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                *dragging = true;
            }
        } else {
            if (CheckCollisionPointRec(mouse, boundary)) {
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    *value = slider_get_value(mouse.x, startPos.x, endPos.x);
                    updated = true;
                    *dragging = true;
                }
            }
        }
    } else {
        *value = slider_get_value(mouse.x, startPos.x, endPos.x);
        updated = true;

        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            *dragging = false;
        }
    }
    return updated;
}

#define volume_slider(preview_boundary) \
    volume_slider_with_location(__FILE__, __LINE__, (preview_boundary))
static bool volume_slider_with_location(const char *file, int line, Rectangle volume_icon_boundary)
{
    Vector2 mouse = GetMousePosition();

    static int expanded = false;
    static bool dragging = false;
    static float saved_volume = 0.0f;

    Rectangle volume_slider_boundary = volume_icon_boundary;

    size_t expanded_slots = 6;
    if (expanded) volume_slider_boundary.width = expanded_slots*HUD_BUTTON_SIZE;

    expanded = dragging || CheckCollisionPointRec(mouse, volume_slider_boundary);

    float icon_size = 512;
    float scale = HUD_BUTTON_SIZE/icon_size*HUD_ICON_SCALE;
    Rectangle dest = {
        volume_slider_boundary.x + HUD_BUTTON_SIZE/2 - icon_size*scale/2,
        volume_slider_boundary.y + HUD_BUTTON_SIZE/2 - icon_size*scale/2,
        icon_size*scale,
        icon_size*scale
    };

    // TODO: animate volume slider expansion
    float volume = GetMasterVolume();

    size_t icon_index;
    if (volume <= 0) {
        icon_index = 0;
    } else {
        size_t phases = 2;
        icon_index = volume*phases;
        if (icon_index >= phases) icon_index = phases - 1;
        icon_index += 1;
    }

    Rectangle source = {icon_size*icon_index, 0, icon_size, icon_size};

    DrawTexturePro(p->icon_textures[UI_ICON_VOLUME], source, dest, CLITERAL(Vector2){0}, 0, ColorBrightness(WHITE, -0.10));

    bool updated = false;

    if (expanded) {
        Rectangle slider_boundary = {
            .x = volume_slider_boundary.x + HUD_BUTTON_SIZE,
            .y = volume_slider_boundary.y,
            .width = (expanded_slots - 1)*HUD_BUTTON_SIZE,
            .height = HUD_BUTTON_SIZE,
        };
        updated = horz_slider(slider_boundary, &volume, &dragging);
        float mouse_wheel_step = 0.05;
        float wheel_delta = GetMouseWheelMove();
        volume += wheel_delta*mouse_wheel_step;
        if (volume < 0) volume = 0;
        if (volume > 1) volume = 1;
        SetMasterVolume(volume);

        tooltip(slider_boundary, TextFormat("Volume %d%%", (int)floorf(volume*100.0f)), SIDE_TOP, dragging);
    }

    // Toggle mute

    uint64_t id = DJB2_INIT;
    id = djb2(id, file, strlen(file));
    id = djb2(id, &line, sizeof(line));
    int volume_icon_state = button_with_id(id, volume_icon_boundary);
    if (
        IsKeyPressed(KEY_TOGGLE_MUTE) ||
        (volume_icon_state & BS_CLICKED)
    ) {
        if (volume > 0) {
            saved_volume = volume;
            volume = 0;
        } else {
            volume = saved_volume;
        }
        SetMasterVolume(volume);
        updated = true;
    }

    if (volume <= 0.0) {
        tooltip(volume_icon_boundary, "Unmute [M]", SIDE_TOP, false);
    } else {
        tooltip(volume_icon_boundary, "Mute [M]", SIDE_TOP, false);
    }

    return dragging || updated;
}

static void popup_tray(Popup_Tray *pt, Rectangle preview_boundary)
{
    float dt = GetFrameTime();
    if (pt->slide > 0) {
        pt->slide -= dt;
    }
    if (pt->slide < 0) {
        pt->slide = 0;
    }

    float popup_width = 250;
    float popup_height = 75;
    float popup_padding = 20;
    for (size_t i = 0; i < pt->count; ++i) {
        Popup *it = PT_GET(pt, i);
        it->lifetime -= dt;

        float t = it->lifetime/HUD_POPUP_LIFETIME_SECS;
        float alpha = t >= 0.5f ? 1.0f : t/0.5f;

        float q = pt->slide / HUD_POPUP_SLIDEIN_SECS;

        Rectangle popup_boundary = {
            .x = preview_boundary.x + preview_boundary.width - popup_width - popup_padding,
            .y = preview_boundary.y + preview_boundary.height - (i + 1 - q)*(popup_height + popup_padding),
            .width = popup_width,
            .height = popup_height,
        };
        DrawRectangleRounded(popup_boundary, 0.3, 20, ColorAlpha(it->success ? COLOR_POPUP_SUCCESS : COLOR_POPUP_BACKGROUND, alpha));
        const char *text = it->message;
        float fontSize = popup_boundary.width*0.15;
        Vector2 size = MeasureTextEx(p->font, text, fontSize, 0);
        Vector2 position = {
            .x = popup_boundary.x + popup_boundary.width/2 - size.x/2,
            .y = popup_boundary.y + popup_boundary.height/2 - size.y/2,
        };
        DrawTextEx(p->font, text, position, fontSize, 0, ColorAlpha(WHITE, alpha));
    }

    while (pt->count > 0 && PT_LAST(pt)->lifetime <= 0) {
        pt->count -= 1;
    }
}

#define cancel_rendering_button(boundary) \
    cancel_rendering_button_with_location(__FILE__, __LINE__, boundary)
static int cancel_rendering_button_with_location(const char *file, int line, Rectangle boundary)
{
    uint64_t id = DJB2_INIT;
    id = djb2(id, file, strlen(file));
    id = djb2(id, &line, sizeof(line));

    int state = button_with_id(id, boundary);

    Color color = (state & BS_HOVEROVER) ? COLOR_TRACK_BUTTON_HOVEROVER : COLOR_TRACK_BUTTON_BACKGROUND;
    DrawRectangleRounded(boundary, 0.4, 20, color);

    float pad_x = boundary.width*0.3;
    float pad_y = boundary.height*0.3;
    float thick = boundary.width*0.10;

    {
        Vector2 startPos = {
            boundary.x + pad_x,
            boundary.y + pad_y,
        };
        Vector2 endPos = {
            boundary.x + boundary.width - pad_x,
            boundary.y + boundary.height - pad_y,
        };
        DrawLineEx(startPos, endPos, thick, COLOR_TOOLTIP_FOREGROUND);
    }

    {
        Vector2 startPos = {
            boundary.x + pad_x,
            boundary.y + boundary.height - pad_y,
        };
        Vector2 endPos = {
            boundary.x + boundary.width - pad_x,
            boundary.y + pad_y,
        };
        DrawLineEx(startPos, endPos, thick, COLOR_TOOLTIP_FOREGROUND);
    }

    return state;
}

#define play_button(track, boundary) \
    play_button_with_location(__FILE__, __LINE__, (track), (boundary))
static int play_button_with_location(const char *file, int line, Track *track, Rectangle boundary)
{
    uint64_t id = DJB2_INIT;
    id = djb2(id, file, strlen(file));
    id = djb2(id, &line, sizeof(line));

    int state = button_with_id(id, boundary);
    size_t icon_index = IsMusicStreamPlaying(track->music) ? 1 : 0;

    float icon_size = 512;
    float scale = HUD_BUTTON_SIZE/icon_size*HUD_ICON_SCALE;
    Rectangle dest = {
        boundary.x + boundary.width/2 - icon_size*scale/2,
        boundary.y + boundary.height/2 - icon_size*scale/2,
        icon_size*scale,
        icon_size*scale
    };

    Rectangle source = {icon_size*icon_index, 0, icon_size, icon_size};
    DrawTexturePro(p->icon_textures[UI_ICON_PLAY], source, dest, CLITERAL(Vector2){0}, 0, ColorBrightness(WHITE, -0.10));

    if (IsMusicStreamPlaying(track->music)) {
        tooltip(boundary, "Pause [SPACE]", SIDE_TOP, false);
    } else {
        tooltip(boundary, "Play [SPACE]", SIDE_TOP, false);
    }

    return state;
}

#define render_button(boundary) \
    render_button_with_location(__FILE__, __LINE__, (boundary))
static int render_button_with_location(const char *file, int line, Rectangle boundary)
{
    uint64_t id = DJB2_INIT;
    id = djb2(id, file, strlen(file));
    id = djb2(id, &line, sizeof(line));

    int state = button_with_id(id, boundary);
    size_t icon_index = 0;

    float icon_size = 512;
    float scale = HUD_BUTTON_SIZE/icon_size*HUD_ICON_SCALE;
    Rectangle dest = {
        boundary.x + boundary.width/2 - icon_size*scale/2,
        boundary.y + boundary.height/2 - icon_size*scale/2,
        icon_size*scale,
        icon_size*scale
    };

    Rectangle source = {icon_size*icon_index, 0, icon_size, icon_size};
    DrawTexturePro(p->icon_textures[UI_ICON_RENDER], source, dest, CLITERAL(Vector2){0}, 0, ColorBrightness(WHITE, -0.10));

    tooltip(boundary, "Render [R]", SIDE_TOP, false);

    return state;
}

#ifdef MUSIALIZER_MICROPHONE
#define microphone_button(boundary) \
    microphone_button_with_location(__FILE__, __LINE__, (boundary))
static int microphone_button_with_location(const char *file, int line, Rectangle boundary)
{
    uint64_t id = DJB2_INIT;
    id = djb2(id, file, strlen(file));
    id = djb2(id, &line, sizeof(line));

    int state = button_with_id(id, boundary);
    size_t icon_index = 0;

    float icon_size = 512;
    float scale = HUD_BUTTON_SIZE/icon_size*HUD_ICON_SCALE;
    Rectangle dest = {
        boundary.x + boundary.width/2 - icon_size*scale/2,
        boundary.y + boundary.height/2 - icon_size*scale/2,
        icon_size*scale,
        icon_size*scale
    };

    Rectangle source = {icon_size*icon_index, 0, icon_size, icon_size};
    DrawTexturePro(p->icon_textures[UI_ICON_MICROPHONE], source, dest, CLITERAL(Vector2){0}, 0, ColorBrightness(WHITE, -0.10));

    tooltip(boundary, "Microphone [C]", SIDE_TOP, false);

    return state;
}
#endif // MUSIALIZER_MICROPHONE

static void toggle_track_playing(Track *track)
{
    if (IsMusicStreamPlaying(track->music)) {
        PauseMusicStream(track->music);
    } else {
        ResumeMusicStream(track->music);
    }
}

static void start_rendering_track(Track *track)
{
    char const * filter_params[] = { "*.mp4" };
    char *output_path = tinyfd_saveFileDialog("Path to rendered video", "./", NOB_ARRAY_LEN(filter_params), filter_params, "mp4 video file");
    if (output_path == NULL) return;

    // TODO: LoadWave is pretty slow on big files
    Wave wave = load_wave_with_ffmpeg_fallback(track->file_path);
    if (wave.frameCount == 0) {
        popup_tray_push(&p->pt, "Could not decode track for rendering", false);
        return;
    }
    float *wave_samples = LoadWaveSamples(wave);
    if (wave_samples == NULL) {
        UnloadWave(wave);
        popup_tray_push(&p->pt, "Could not decode track for rendering", false);
        return;
    }

    StopMusicStream(track->music);
    fft_clean();
    p->wave = wave;
    p->wave_cursor = 0;
    p->wave_samples = wave_samples;
    // TODO: set the rendering output path based on the input path
    // Basically output into the same folder
    p->ffmpeg = ffmpeg_start_rendering(output_path, p->screen.texture.width, p->screen.texture.height, RENDER_FPS, track->file_path);
    SetTargetFPS(0);
    p->rendering = true;
    p->cancel_rendering = false;
    SetTraceLogLevel(LOG_WARNING);
}

static void finish_rendering_track(Track *track)
{
    SetTraceLogLevel(LOG_INFO);
    UnloadWave(p->wave);
    memset(&p->wave, 0, sizeof(p->wave));
    UnloadWaveSamples(p->wave_samples);
    p->wave_samples = NULL;
    SetTargetFPS(PREVIEW_FPS);
    p->rendering = false;
    fft_clean();
    PlayMusicStream(track->music);
}

#ifdef MUSIALIZER_MICROPHONE
static void start_capture(void)
{
    ma_result result = MA_SUCCESS;

    assert(!p->capturing);
    assert(!p->microphone_working);

    p->capturing = true;

    const char *recording_file_path = "recording.wav";

    drwav_data_format format = {0};
    format.container = drwav_container_riff;
    format.format = DR_WAVE_FORMAT_IEEE_FLOAT;
    format.channels = 2;
    format.sampleRate = 44100;
    format.bitsPerSample = 32;
    if (!drwav_init_file_write(&p->wav, recording_file_path, &format, NULL)) {
        TraceLog(LOG_ERROR, "DRWAVE: Failed to initialize output file %s", recording_file_path);
        return;
    }

    // TODO: let the user choose their mic
    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_capture);
    deviceConfig.capture.format = ma_format_f32;
    deviceConfig.capture.channels = 2;
    deviceConfig.sampleRate = 44100;
    deviceConfig.dataCallback = ma_callback;
    deviceConfig.pUserData = NULL;
    result = ma_device_init(NULL, &deviceConfig, &p->microphone);
    if (result != MA_SUCCESS) {
        TraceLog(LOG_ERROR, "MINIAUDIO: Failed to initialize capture device: %s", ma_result_description(result));
        drwav_uninit(&p->wav);
        return;
    }

    result = ma_device_start(&p->microphone);
    if (result != MA_SUCCESS) {
        TraceLog(LOG_ERROR, "MINIAUDIO: Failed to start device: %s", ma_result_description(result));
        ma_device_uninit(&p->microphone);
        drwav_uninit(&p->wav);
        return;
    }

    p->microphone_working = true;
}
#endif // MUSIALIZER_MICROPHONE

// TODO: adapt toolbar to narrow widths
static bool toolbar(Track *track, Rectangle boundary)
{
    bool interacted = false;
    int state = 0;

#ifdef MUSIALIZER_MICROPHONE
    size_t buttons_count = 6;
#else
    size_t buttons_count = 5;
#endif // MUSIALIZER_MICROPHONE

    if (boundary.width < HUD_BUTTON_SIZE*buttons_count) return interacted;

    DrawRectangleRec(boundary, COLOR_TRACK_PANEL_BACKGROUND);

    float x = boundary.x;

    state = play_button(track, (CLITERAL(Rectangle) {
        x,
        boundary.y,
        HUD_BUTTON_SIZE,
        HUD_BUTTON_SIZE,
    }));
    x += HUD_BUTTON_SIZE;
    if (state & BS_CLICKED) {
        interacted = true;
        toggle_track_playing(track);
    }

    state = render_button((CLITERAL(Rectangle) {
        x,
        boundary.y,
        HUD_BUTTON_SIZE,
        HUD_BUTTON_SIZE,
    }));
    x += HUD_BUTTON_SIZE;
    if (state & BS_CLICKED) {
        interacted = true;
        start_rendering_track(track);
    }

#ifdef MUSIALIZER_MICROPHONE
    state = microphone_button((CLITERAL(Rectangle) {
        x,
        boundary.y,
        HUD_BUTTON_SIZE,
        HUD_BUTTON_SIZE,
    }));
    x += HUD_BUTTON_SIZE;
    if (state & BS_CLICKED) {
        interacted = true;
        start_capture();
    }
#endif // MUSIALIZER_MICROPHONE

    state = button((CLITERAL(Rectangle) {
        x,
        boundary.y,
        HUD_BUTTON_SIZE,
        HUD_BUTTON_SIZE,
    }));
    {
        Color color = (state & BS_HOVEROVER) ? COLOR_HUD_BUTTON_HOVEROVER : COLOR_HUD_BUTTON_BACKGROUND;
        DrawRectangleRec((Rectangle){x, boundary.y, HUD_BUTTON_SIZE, HUD_BUTTON_SIZE}, color);

        float icon_size = 512;
        float scale = HUD_BUTTON_SIZE/icon_size*HUD_ICON_SCALE;
        float s = icon_size*scale;
        float t = s*0.25f;
        float cx = x + HUD_BUTTON_SIZE/2;
        float cy = boundary.y + HUD_BUTTON_SIZE/2;
        DrawLineEx((Vector2){cx - t, cy}, (Vector2){cx + t, cy}, s*0.1f, ColorBrightness(WHITE, -0.10));
        DrawLineEx((Vector2){cx, cy - t}, (Vector2){cx, cy + t}, s*0.1f, ColorBrightness(WHITE, -0.10));
        tooltip((Rectangle){x, boundary.y, HUD_BUTTON_SIZE, HUD_BUTTON_SIZE}, "Add Track", SIDE_TOP, false);
    }
    x += HUD_BUTTON_SIZE;
    if (state & BS_CLICKED) {
        interacted = true;
        open_files_dialog();
    }

    // Save playlist button
    state = button((CLITERAL(Rectangle) {
        x,
        boundary.y,
        HUD_BUTTON_SIZE,
        HUD_BUTTON_SIZE,
    }));
    {
        Color color = (state & BS_HOVEROVER) ? COLOR_HUD_BUTTON_HOVEROVER : COLOR_HUD_BUTTON_BACKGROUND;
        DrawRectangleRec((Rectangle){x, boundary.y, HUD_BUTTON_SIZE, HUD_BUTTON_SIZE}, color);
        float s = HUD_BUTTON_SIZE * 0.5f;
        float cx = x + HUD_BUTTON_SIZE/2;
        float cy = boundary.y + HUD_BUTTON_SIZE/2;
        float lw = s * 0.12f;
        // Diskette shape
        DrawRectangleLinesEx((Rectangle){cx - s/2, cy - s/2, s, s}, lw, ColorBrightness(WHITE, -0.10));
        DrawRectangle((int)(cx - s/3), (int)(cy - s/3), (int)(s*2/3), (int)(s*2/3), ColorBrightness(WHITE, -0.10));
        DrawRectangle((int)(cx - s/4), (int)(cy + s/6), (int)(s/2), (int)(s/3), ColorBrightness(WHITE, -0.20));
        tooltip((Rectangle){x, boundary.y, HUD_BUTTON_SIZE, HUD_BUTTON_SIZE}, "Save Playlist", SIDE_TOP, false);
    }
    x += HUD_BUTTON_SIZE;
    if (state & BS_CLICKED) {
        interacted = true;
        const char *save_path = tinyfd_saveFileDialog("Save Playlist", "playlist.m3u", 1, (const char *[]){ "*.m3u", "*.m3u8" }, "M3U Playlist");
        if (save_path) {
            FILE *f = platform_fopen(save_path, "w");
            if (f) {
                for (size_t i = 0; i < p->tracks.count; i++) {
                    fprintf(f, "%s\n", p->tracks.items[i].file_path);
                }
                fclose(f);
            }
        }
    }

    bool volume_slider_interacted = volume_slider((CLITERAL(Rectangle) {
        x,
        boundary.y,
        HUD_BUTTON_SIZE,
        HUD_BUTTON_SIZE,
    }));
    x += HUD_BUTTON_SIZE;
    interacted = interacted || volume_slider_interacted;

    state = fullscreen_button((CLITERAL(Rectangle) {
        boundary.x + boundary.width - HUD_BUTTON_SIZE,
        boundary.y,
        HUD_BUTTON_SIZE,
        HUD_BUTTON_SIZE,
    }));
    if (state & BS_CLICKED) {
        interacted = true;
        p->fullscreen = !p->fullscreen;
    }

    return interacted;
}

static void preview_screen(void)
{
    int w = GetScreenWidth();
    int h = GetScreenHeight();

    if (IsFileDropped()) {
        FilePathList droppedFiles = LoadDroppedFiles();
        for (size_t i = 0; i < droppedFiles.count; ++i) {
            const char *ext = GetFileExtension(droppedFiles.paths[i]);
            if (ext && (strcmp(ext, ".m3u") == 0 || strcmp(ext, ".m3u8") == 0)) {
                load_m3u_playlist(droppedFiles.paths[i]);
            } else {
                load_track_from_path(droppedFiles.paths[i]);
            }
        }
        UnloadDroppedFiles(droppedFiles);
        startup_autoplay();
    }

#ifdef MUSIALIZER_MICROPHONE
    if (IsKeyPressed(KEY_CAPTURE)) start_capture();
#endif // MUSIALIZER_MICROPHONE

    Track *track = current_track();
    if (track) { // The music is loaded and ready
        UpdateMusicStream(track->music);

        // Now-playing banner timer
        if (p->now_playing_timer > 0) {
            p->now_playing_timer -= GetFrameTime();
        }

        // Crossfade
        if (p->crossfading) {
            UpdateMusicStream(p->crossfade_music);
            p->crossfade_timer += GetFrameTime();
            float t = p->crossfade_timer / p->crossfade_duration;
            if (t >= 1.0f) {
                t = 1.0f;
                StopMusicStream(p->crossfade_music);
                p->crossfading = false;
            }
            SetMusicVolume(p->crossfade_music, GetMasterVolume() * (1.0f - t));
            SetMusicVolume(track->music, GetMasterVolume() * t);
        }

        // Auto-advance when track ends
        if (p->track_was_playing && !IsMusicStreamPlaying(track->music)) {
            float len = GetMusicTimeLength(track->music);
            float played = GetMusicTimePlayed(track->music);
            if (len > 0 && played < 0.1f) {
                if (p->repeat_mode == REPEAT_ALL) {
                    SeekMusicStream(track->music, 0);
                    PlayMusicStream(track->music);
                } else if (p->current_track + 1 < (int)p->tracks.count) {
                    play_track(p->current_track + 1);
                }
                track = current_track();
            }
        }
        p->track_was_playing = IsMusicStreamPlaying(track->music);

        if (track) {
            if (IsKeyPressed(KEY_TOGGLE_PLAY)) {
                toggle_track_playing(track);
            }

            if (IsKeyPressed(KEY_RENDER) && IS_CTRL_DOWN) {
                start_rendering_track(track);
            }

            if (IsKeyPressed(KEY_FULLSCREEN)) {
                p->fullscreen = !p->fullscreen;
            }

            if (IsKeyPressed(KEY_RIGHT)) {
                next_track();
                track = current_track();
            }

            if (IsKeyPressed(KEY_LEFT)) {
                prev_track();
                track = current_track();
            }

            if (IsKeyPressed(KEY_R)) {
                p->repeat_mode = (p->repeat_mode + 1) % 2;
                p->repeat_mode_label_timer = 1.5f;
            }

            if (IsKeyPressed(KEY_S)) {
                p->shuffle = !p->shuffle;
            }

            if (IsKeyPressed(KEY_V)) {
                p->viz_mode = (p->viz_mode + 1) % COUNT_VIZ_MODES;
            }

            if (IsKeyPressed(KEY_H)) {
                p->show_help = !p->show_help;
            }
        }

        if (track == NULL) return;
        size_t m = fft_analyze(GetFrameTime());

        float toolbar_height = HUD_BUTTON_SIZE;
        if (p->fullscreen) {
            // TODO: make timeline somehow visible in fullscreen mode (maybe miniversion of it on the toolbar)
            static float hud_timer = HUD_TIMER_SECS;

            Rectangle preview_boundary = {
                .x = 0,
                .y = 0,
                .width = w,
                .height = h,
            };

            if (hud_timer > 0.0) {
                hud_timer -= GetFrameTime();

                preview_boundary.height -= toolbar_height;
                bool interacted = toolbar(track, CLITERAL(Rectangle) {
                    .x = 0,
                    .y = preview_boundary.height,
                    .width = preview_boundary.width,
                    .height = toolbar_height,
                });

                if (interacted) hud_timer = HUD_TIMER_SECS;
            }

            Vector2 delta = GetMouseDelta();
            bool moved = fabsf(delta.x) + fabsf(delta.y) > 0.0;
            if (moved) hud_timer = HUD_TIMER_SECS;

            fft_render(preview_boundary, m);

#if 0
            // TODO: toggle track playing on right mouse click on the preview
            if (button(preview_boundary) & BS_CLICKED) {
                toggle_track_playing(track);
            }
#else
            (void) button_with_location;
#endif

            popup_tray(&p->pt, preview_boundary);

            // Now-playing banner
            if (p->now_playing_timer > 0 && track) {
                float fs = 22;
                const char *name = GetFileName(track->file_path);
                Vector2 sz = MeasureTextEx(p->font, name, fs, 0);
                float banner_h = sz.y + 20;
                float banner_y = preview_boundary.y + 10;
                float alpha = p->now_playing_timer > 0.5f ? 1.0f : p->now_playing_timer / 0.5f;
                Rectangle bg = { preview_boundary.x + preview_boundary.width/2 - sz.x/2 - 15, banner_y, sz.x + 30, banner_h };
                DrawRectangleRounded(bg, 0.3, 10, ColorAlpha(COLOR_BACKGROUND, 0.85f * alpha));
                DrawTextEx(p->font, name, (Vector2){bg.x + 15, bg.y + 10}, fs, 0, ColorAlpha(WHITE, alpha));
            }
        } else {
            float tracks_panel_width = 320.0f;
            float timeline_height = 150.0f;
            Rectangle preview_boundary = {
                .x = tracks_panel_width,
                .y = 0,
                .width = w - tracks_panel_width,
                .height = h - timeline_height - toolbar_height,
            };

#if 0
            // TODO: toggle track playing on right mouse click on the preview
            if (button(preview_boundary) & BS_CLICKED) {
                toggle_track_playing(track);
            }
#else
            (void) button_with_location;
#endif

            BeginScissorMode(preview_boundary.x, preview_boundary.y, preview_boundary.width, preview_boundary.height);
            fft_render(preview_boundary, m);
            popup_tray(&p->pt, preview_boundary);
            EndScissorMode();

            // Now-playing banner
            if (p->now_playing_timer > 0 && track) {
                float fs = 22;
                const char *name = GetFileName(track->file_path);
                Vector2 sz = MeasureTextEx(p->font, name, fs, 0);
                float banner_h = sz.y + 20;
                float banner_y = preview_boundary.y + 10;
                float alpha = p->now_playing_timer > 0.5f ? 1.0f : p->now_playing_timer / 0.5f;
                Rectangle bg = { preview_boundary.x + preview_boundary.width/2 - sz.x/2 - 15, banner_y, sz.x + 30, banner_h };
                DrawRectangleRounded(bg, 0.3, 10, ColorAlpha(COLOR_BACKGROUND, 0.85f * alpha));
                DrawTextEx(p->font, name, (Vector2){bg.x + 15, bg.y + 10}, fs, 0, ColorAlpha(WHITE, alpha));
            }

            float eq_section_height = 160.0f;
            tracks_panel((CLITERAL(Rectangle) {
                .x = 0,
                .y = 0,
                .width = tracks_panel_width,
                .height = h - timeline_height - eq_section_height,
            }));
            track = current_track();
            if (track == NULL) return;

            // EQ Section
            {
                float eq_y = h - timeline_height - eq_section_height;
                DrawRectangle(0, eq_y, tracks_panel_width, eq_section_height, COLOR_TRACK_PANEL_BACKGROUND);

                float title_size = 20;
                DrawTextEx(p->font, "Equalizer", (Vector2){10, eq_y + 5}, title_size, 0, WHITE);

                float reset_size = 16;
                Vector2 reset_txt = MeasureTextEx(p->font, "Reset", reset_size, 0);
                float reset_pad = 10;
                Rectangle reset_boundary = {
                    tracks_panel_width - reset_txt.x - reset_pad,
                    eq_y + 5,
                    reset_txt.x + reset_pad,
                    reset_txt.y,
                };
                int reset_state = button(reset_boundary);
                Color reset_color = (reset_state & BS_HOVEROVER) ? COLOR_ACCENT : ColorAlpha(WHITE, 0.5f);
                DrawTextEx(p->font, "Reset", (Vector2){reset_boundary.x + reset_pad/2, reset_boundary.y}, reset_size, 0, reset_color);
                if (reset_state & BS_CLICKED) {
                    p->eq_low = 0.5f;
                    p->eq_mid = 0.5f;
                    p->eq_high = 0.5f;
                }

                float slider_x = 10;
                float slider_w = tracks_panel_width - 20;
                float slider_h = 25;
                float label_size = 14;
                float row_h = label_size + 2 + slider_h + 4;
                float y0 = eq_y + 30;

                float y = y0;
                DrawTextEx(p->font, "Low", (Vector2){slider_x, y}, label_size, 0, ColorAlpha(WHITE, 0.6f));
                y += label_size + 2;
                horz_slider((Rectangle){slider_x, y, slider_w, slider_h}, &p->eq_low, &p->eq_low_drag);

                y = y0 + row_h;
                DrawTextEx(p->font, "Mid", (Vector2){slider_x, y}, label_size, 0, ColorAlpha(WHITE, 0.6f));
                y += label_size + 2;
                horz_slider((Rectangle){slider_x, y, slider_w, slider_h}, &p->eq_mid, &p->eq_mid_drag);

                y = y0 + row_h * 2;
                DrawTextEx(p->font, "High", (Vector2){slider_x, y}, label_size, 0, ColorAlpha(WHITE, 0.6f));
                y += label_size + 2;
                horz_slider((Rectangle){slider_x, y, slider_w, slider_h}, &p->eq_high, &p->eq_high_drag);
            }

            timeline(CLITERAL(Rectangle) {
                .x = 0,
                .y = h - timeline_height,
                .width = w,
                .height = timeline_height,
            }, track);

            toolbar(track, CLITERAL(Rectangle) {
                .x = tracks_panel_width,
                .y = preview_boundary.height,
                .width = preview_boundary.width,
                .height = toolbar_height,
            });
        }
    } else { // We are waiting for the user to Drag&Drop the Music
        const char *label = "Click to Select File";
        int font_size = p->font.baseSize;
        Color color = WHITE;
        Vector2 size = MeasureTextEx(p->font, label, font_size, 0);
        Vector2 position = {
            w/2 - size.x/2,
            h/2 - size.y/2,
        };
        DrawTextEx(p->font, label, position, font_size, 0, color);

        font_size /= 2;
        label = "(or just Drag&Drop it)";
        color = WHITE;
        size = MeasureTextEx(p->font, label, font_size, 0);
        position.y += font_size*2;
        position.x = w/2 - size.x/2;
        DrawTextEx(p->font, label, position, font_size, 0, color);

        popup_tray(&p->pt, CLITERAL(Rectangle) {
            .x = 0,
            .y = 0,
            .width = w,
            .height = h,
        });

        if (button(((Rectangle) {0, 0, w, h})) & BS_CLICKED) {
            open_files_dialog();
        }
    }
}

#ifdef MUSIALIZER_MICROPHONE
static void capture_screen(void)
{
    int w = GetScreenWidth();
    int h = GetScreenHeight();

    if (p->microphone_working) {
        if (IsKeyPressed(KEY_CAPTURE) || IsKeyPressed(KEY_ESCAPE)) {
            // Microphone is working, so it needs to be uninited
            ma_device_uninit(&p->microphone);
            drwav_uninit(&p->wav);
            p->microphone_working = false;
            p->capturing = false;

            const char *recording_file_path = "recording.wav";
            Music music = LoadMusicStream(recording_file_path);
            if (IsMusicValid(music)) {
                music.looping = false;
                AttachAudioStreamProcessor(music.stream, callback);
                char *file_path = duplicate_string(recording_file_path);
                assert(file_path != NULL);
                nob_da_append(&p->tracks, (CLITERAL(Track) {
                    .file_path = file_path,
                    .music = music,
                    .music_data = NULL,
                }));
            } else {
                popup_tray_push(&p->pt, "Could not load capture", false);
            }

            if (current_track() == NULL && p->tracks.count > 0) {
                p->current_track = 0;
                PlayMusicStream(p->tracks.items[0].music);
            }
        }


        size_t m = fft_analyze(GetFrameTime());
        fft_render(CLITERAL(Rectangle) {
            0, 0, GetScreenWidth(), GetScreenHeight()
        }, m);
    } else {
        if (IsKeyPressed(KEY_ESCAPE)) {
            // Microphone is not working, so it does no need to be uninited
            p->capturing = false;
        }

        // TODO: report capture device error via the popup mechanism.
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
}
#endif // MUSIALIZER_MICROPHONE

static void rendering_screen(void)
{
    int w = GetScreenWidth();
    int h = GetScreenHeight();

    Track *track = current_track();
    NOB_ASSERT(track != NULL);
    if (p->ffmpeg == NULL) { // Starting FFmpeg process has failed for some reason
        if (IsKeyPressed(KEY_ESCAPE)) {
            finish_rendering_track(track);
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
        if (p->wave_cursor >= p->wave.frameCount && fft_settled()) { // Rendering is finished
            if (!ffmpeg_end_rendering(p->ffmpeg, false)) {
                // NOTE: Ending FFmpeg process has failed, let's mark ffmpeg handle as NULL
                // which will be interpreted as "FFmpeg Failure" on the next frame.
                //
                // It should be safe to set ffmpeg to NULL even if ffmpeg_end_rendering() failed
                // cause it should deallocate all the resources even in case of a failure.
                p->ffmpeg = NULL;
            } else {
                finish_rendering_track(track);
            }
        } else if (IsKeyPressed(KEY_ESCAPE) || p->cancel_rendering) {  // Rendering is cancelled
            ffmpeg_end_rendering(p->ffmpeg, true);
            p->ffmpeg = NULL;

            finish_rendering_track(track);
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

            {
                Rectangle boundary = {
                    .width = HUD_BUTTON_SIZE,
                    .height = HUD_BUTTON_SIZE,
                };
                boundary.x = w - boundary.width - HUD_BUTTON_SIZE*0.5;
                boundary.y = HUD_BUTTON_SIZE*0.5;
                tooltip(boundary, "Cancel [Esc]", SIDE_LEFT, false);
                if (cancel_rendering_button(boundary) & BS_CLICKED) {
                    p->cancel_rendering = true;
                }
            }

            // Rendering
            {
                size_t chunk_size = p->wave.sampleRate/RENDER_FPS;
                float *fs = (float*)p->wave_samples;
                size_t remaining = p->wave_cursor < p->wave.frameCount
                    ? p->wave.frameCount - p->wave_cursor
                    : 0;
                size_t available = remaining < chunk_size ? remaining : chunk_size;
                const float *samples = available > 0
                    ? fs + p->wave_cursor*p->wave.channels
                    : NULL;
                fft_push_frames(samples, available, p->wave.channels, true);
                fft_push_frames(NULL, chunk_size - available, 1, true);
                p->wave_cursor += chunk_size;
            }

            size_t m = fft_analyze(1.0f/RENDER_FPS);

            BeginTextureMode(p->screen);
            ClearBackground(COLOR_BACKGROUND);
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
                ffmpeg_end_rendering(p->ffmpeg, false);
                p->ffmpeg = NULL;
            }
            UnloadImage(image);
        }
    }
}

static void load_assets(void)
{
    size_t data_size = 0;
    void *data = NULL;

    const char *freesans_path = "./resources/fonts/FreeSans.ttf";
    data = plug_load_resource(freesans_path, &data_size);
    {
        // Codepoints covering Latin, Cyrillic, Greek, Armenian, and CJK
        int cp[25000];
        int cp_count = 0;
        // Basic Latin (0x20-0x7E)
        for (int i = 0x20; i <= 0x7E; i++) cp[cp_count++] = i;
        // Latin-1 Supplement (0xA0-0xFF)
        for (int i = 0xA0; i <= 0xFF; i++) cp[cp_count++] = i;
        // Latin Extended-A (0x100-0x17F)
        for (int i = 0x100; i <= 0x17F; i++) cp[cp_count++] = i;
        // Latin Extended-B (0x180-0x24F)
        for (int i = 0x180; i <= 0x24F; i++) cp[cp_count++] = i;
        // Cyrillic (0x400-0x4FF)
        for (int i = 0x400; i <= 0x4FF; i++) cp[cp_count++] = i;
        // Greek (0x370-0x3FF)
        for (int i = 0x370; i <= 0x3FF; i++) cp[cp_count++] = i;
        // Latin Extended Additional (0x1E00-0x1EFF)
        for (int i = 0x1E00; i <= 0x1EFF; i++) cp[cp_count++] = i;
        // Armenian (0x530-0x58F)
        for (int i = 0x530; i <= 0x58F; i++) cp[cp_count++] = i;
        // CJK Unified Ideographs (0x4E00-0x9FFF)
        for (int i = 0x4E00; i <= 0x9FFF; i++) cp[cp_count++] = i;
        p->font = LoadFontFromMemory(GetFileExtension(freesans_path), data, data_size, FONT_SIZE, cp, cp_count);
        GenTextureMipmaps(&p->font.texture);
        SetTextureFilter(p->font.texture, TEXTURE_FILTER_BILINEAR);
    }
    plug_free_resource(data);

    // TODO: Maybe we should try to keep compiling different versions of shaders
    // until one of them works?
    //
    // If the shader can not be compiled maybe we could fallback to software rendering
    // of the texture of a fuzzy circle? The shader does not really do anything particularly
    // special.
    data = plug_load_resource(TextFormat("./resources/shaders/glsl%d/circle.fs", GLSL_VERSION), &data_size);
        p->circle = LoadShaderFromMemory(NULL, data);
        p->circle_radius_location = GetShaderLocation(p->circle, "radius");
        p->circle_power_location = GetShaderLocation(p->circle, "power");
    plug_free_resource(data);

    for (UI_Icon icon = 0; icon < COUNT_UI_ICONS; ++icon) {
        data = plug_load_resource(icon_file_paths[icon], &data_size);
            Image image = LoadImageFromMemory(GetFileExtension(icon_file_paths[icon]), data, data_size);
                p->icon_textures[icon] = LoadTextureFromImage(image);
                GenTextureMipmaps(&p->icon_textures[icon]);
                SetTextureFilter(p->icon_textures[icon], TEXTURE_FILTER_BILINEAR);
            UnloadImage(image);
        plug_free_resource(data);
    }
}

static void unload_assets()
{
    UnloadFont(p->font);
    UnloadShader(p->circle);
    for (UI_Icon icon = 0; icon < COUNT_UI_ICONS; ++icon) {
        UnloadTexture(p->icon_textures[icon]);
    }
}

MUSIALIZER_PLUG void plug_init(void)
{
    p = malloc(sizeof(*p));
    assert(p != NULL && "Buy more RAM lol");
    memset(p, 0, sizeof(*p));

    fft_buffer_init();

    load_assets();
    p->screen = LoadRenderTexture(RENDER_WIDTH, RENDER_HEIGHT);
    p->current_track = -1;

    // TODO: restore master volume between sessions
    SetMasterVolume(0.5);
    p->eq_low = 0.5f;
    p->eq_mid = 0.5f;
    p->eq_high = 0.5f;
    p->crossfade_duration = 3.0f;
    p->track_was_playing = false;
    SetTargetFPS(PREVIEW_FPS);
}

MUSIALIZER_PLUG void plug_shutdown(void)
{
    if (p == NULL) return;

    loader_stop();
    unload_preview_waveform();

    if (p->ffmpeg != NULL) {
        ffmpeg_end_rendering(p->ffmpeg, true);
        p->ffmpeg = NULL;
    }
    if (p->wave_samples != NULL) {
        UnloadWaveSamples(p->wave_samples);
        p->wave_samples = NULL;
    }
    if (p->wave.frameCount > 0) {
        UnloadWave(p->wave);
        memset(&p->wave, 0, sizeof(p->wave));
    }

#ifdef MUSIALIZER_MICROPHONE
    if (p->microphone_working) {
        ma_device_uninit(&p->microphone);
        drwav_uninit(&p->wav);
        p->microphone_working = false;
    }
#endif

    for (size_t i = 0; i < p->tracks.count; ++i) {
        Track *track = &p->tracks.items[i];
        DetachAudioStreamProcessor(track->music.stream, callback);
        UnloadMusicStream(track->music);
        free(track->music_data);
        if (track->has_cover) UnloadTexture(track->cover);
        free(track->file_path);
    }
    free(p->tracks.items);

    UnloadRenderTexture(p->screen);
    unload_assets();
    fft_buffer_shutdown();
    free(p);
    p = NULL;
}

MUSIALIZER_PLUG void *plug_pre_reload(void)
{
    unload_preview_waveform();
    loader_stop();
    for (size_t i = 0; i < p->tracks.count; ++i) {
        Track *it = &p->tracks.items[i];
        DetachAudioStreamProcessor(it->music.stream, callback);
    }
    fft_buffer_shutdown();
    unload_assets();
    return p;
}

MUSIALIZER_PLUG void plug_post_reload(void *pp)
{
    p = pp;
    fft_buffer_init();
    memset(&loader, 0, sizeof(loader));
    for (size_t i = 0; i < p->tracks.count; ++i) {
        Track *it = &p->tracks.items[i];
        AttachAudioStreamProcessor(it->music.stream, callback);
    }
    load_assets();
}

MUSIALIZER_PLUG void plug_update(void)
{
    process_completed_loads();

    BeginDrawing();
    ClearBackground(COLOR_BACKGROUND);

    begin_tooltip_frame();

    if (!p->rendering) { // We are in the Preview Mode
#ifdef MUSIALIZER_MICROPHONE
        if (p->capturing) {
            capture_screen();
        } else {
            preview_screen();
        }
#else
        preview_screen();
#endif // MUSIALIZER_MICROPHONE
    } else { // We are in the Rendering Mode
        rendering_screen();
    }

    // Keyboard shortcut overlay
    if (p->show_help) {
        const char *lines[] = {
            "Space     - Play / Pause",
            "Left      - Previous Track",
            "Right     - Next Track",
            "V         - Cycle Visualization",
            "R         - Cycle Repeat Mode",
            "Ctrl+R    - Render to Video",
            "S         - Toggle Shuffle",
            "F         - Toggle Fullscreen",
            "H         - Toggle Help",
            "",
            "Click / Drag on Timeline to Seek",
        };
        int n = sizeof(lines)/sizeof(lines[0]);
        float fs = 20;
        float line_h = fs + 6;
        float total_h = n * line_h + 20;
        float max_w = 0;
        for (int i = 0; i < n; i++) {
            Vector2 sz = MeasureTextEx(GetFontDefault(), lines[i], fs, 0);
            if (sz.x > max_w) max_w = sz.x;
        }
        float total_w = max_w + 40;
        float ox = GetScreenWidth()/2 - total_w/2;
        float oy = GetScreenHeight()/2 - total_h/2;
        DrawRectangleRounded((Rectangle){ox, oy, total_w, total_h}, 0.3, 10, ColorAlpha(COLOR_BACKGROUND, 0.92f));
        DrawRectangleRoundedLines((Rectangle){ox, oy, total_w, total_h}, 0.3, 10, ColorAlpha(WHITE, 0.1f));
        for (int i = 0; i < n; i++) {
            float y = oy + 10 + i * line_h;
            DrawTextEx(GetFontDefault(), lines[i], (Vector2){ox + 20, y}, fs, 0, ColorAlpha(WHITE, 0.85f));
        }
    }

    end_tooltip_frame();

    EndDrawing();
}

// TODO: About Page that includes current commit, version and the platforms
// We may also include licenses and contributors there.
// TODO: Actual Fullscreen Mode
// We do have fullscreen button, but apparently there is a demand on a fullscreen fullscreen mode.
// Raylib does have ToggleFullscreen(). Let's see how we can integrate it into the current UI/UX
// TODO: Adding Files by Ctrl+C, Ctrl+V
