#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <complex.h>

#include "build/config.h"
#include "plug.h"
#include "ffmpeg.h"
#define NOB_IMPLEMENTATION
#include "nob.h"

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
#define FONT_SIZE 64

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
#define COLOR_TOOLTIP_BACKGROUND      COLOR_TRACK_PANEL_BACKGROUND
#define COLOR_TOOLTIP_FOREGROUND      WHITE
#define HUD_TIMER_SECS 1.0f
#define HUD_BUTTON_SIZE 50
#define HUD_BUTTON_MARGIN 50
#define HUD_ICON_SCALE 0.5
#define HUD_POPUP_LIFETIME_SECS 2.0f
#define HUD_POPUP_SLIDEIN_SECS 0.1f
#define TOOLTIP_PADDING 20.0f

#define KEY_TOGGLE_PLAY KEY_SPACE
#define KEY_RENDER      KEY_R
#define KEY_FULLSCREEN  KEY_F
#define KEY_CAPTURE     KEY_C
#define KEY_TOGGLE_MUTE KEY_M

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
    float lifetime;
} Popup;

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

    // FFT Analyzer
    float in_raw[FFT_SIZE];
    float in_win[FFT_SIZE];
    Float_Complex out_raw[FFT_SIZE];
    float out_log[FFT_SIZE];
    float out_smooth[FFT_SIZE];
    float out_smear[FFT_SIZE];

#ifndef MUSIALIZER_ACT_ON_PRESS
    uint64_t active_button_id;
#endif // MUSIALIZER_ACT_ON_PRESS

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

static bool fft_settled(void)
{
    float eps = 1e-3;
    for (size_t i = 0; i < FFT_SIZE; ++i) {
        if (p->out_smooth[i] > eps) return false;
        if (p->out_smear[i] > eps) return false;
    }
    return true;
}

static void fft_clean(void)
{
    memset(p->in_raw, 0, sizeof(p->in_raw));
    memset(p->in_win, 0, sizeof(p->in_win));
    memset(p->out_raw, 0, sizeof(p->out_raw));
    memset(p->out_log, 0, sizeof(p->out_log));
    memset(p->out_smooth, 0, sizeof(p->out_smooth));
    memset(p->out_smear, 0, sizeof(p->out_smear));
}

// Ported from https://rosettacode.org/wiki/Fast_Fourier_transform#Python
static void fft(float in[], size_t stride, Float_Complex out[], size_t n)
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

static size_t fft_analyze(float dt)
{
    // Apply the Hann Window on the Input - https://en.wikipedia.org/wiki/Hann_function
    for (size_t i = 0; i < FFT_SIZE; ++i) {
        float t = (float)i/(FFT_SIZE - 1);
        float hann = 0.5 - 0.5*cosf(2*PI*t);
        p->in_win[i] = p->in_raw[i]*hann;
    }

    // FFT
    fft(p->in_win, 1, p->out_raw, FFT_SIZE);

    // "Squash" into the Logarithmic Scale
    float step = 1.06;
    float lowf = 1.0f;
    size_t m = 0;
    float max_amp = 1.0f;
    for (float f = lowf; (size_t) f < FFT_SIZE/2; f = ceilf(f*step)) {
        float f1 = ceilf(f*step);
        float a = 0.0f;
        for (size_t q = (size_t) f; q < FFT_SIZE/2 && q < (size_t) f1; ++q) {
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

static void fft_render(Rectangle boundary, size_t m)
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

static void fft_push(float frame)
{
    memmove(p->in_raw, p->in_raw + 1, (FFT_SIZE - 1)*sizeof(p->in_raw[0]));
    p->in_raw[FFT_SIZE-1] = frame;
}

// TODO: make sure the audio callback is thread-safe
static void callback(void *bufferData, unsigned int frames)
{
    // https://cdecl.org/?q=float+%28*fs%29%5B2%5D
    float (*fs)[2] = bufferData;

    for (size_t i = 0; i < frames; ++i) {
        fft_push(fs[i][0]);
    }

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


static void popup_tray_push(Popup_Tray *pt)
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
    }
}

static inline float signf(float x)
{
    if (x < 0.0) return -1;
    if (x > 0.0) return 1;
    return 0.0;
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

static void timeline(Rectangle timeline_boundary, Track *track)
{
    DrawRectangleRec(timeline_boundary, COLOR_TIMELINE_BACKGROUND);

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

    Vector2 mouse = GetMousePosition();
    if (CheckCollisionPointRec(mouse, timeline_boundary)) {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            float t = (mouse.x - timeline_boundary.x)/timeline_boundary.width;
            SeekMusicStream(track->music, t*len);
        }

    }

    // TODO: enable the user to render a specific region instead of the whole song.
    // TODO: visualize sound wave on the timeline
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

#ifndef MUSIALIZER_ACT_ON_PRESS
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
#else
    (void) id;
    int clicked = hoverover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
#endif // MUSIALIZER_ACT_ON_PRESS

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
void track_label(Font font, const char *text, Vector2 position, float fontSize, Color tint, float max_width)
{
    if (font.texture.id == 0) font = GetFontDefault();  // Security check in case of not valid font

    float spacing = 0;

    int size = TextLength(text);    // Total size in bytes of the text, scanned by codepoints in loop

    int textOffsetY = 0;            // Offset between lines (on linebreak '\n')
    float textOffsetX = 0.0f;       // Offset X to next character to draw

    float scaleFactor = fontSize/font.baseSize;         // Character quad scaling factor

    for (int i = 0; i < size && textOffsetX < max_width;)
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

    float min_scroll = 0;
    if (panel_scroll < min_scroll) panel_scroll = min_scroll;
    float max_scroll = entire_scrollable_area - visible_area_size;
    if (max_scroll < 0) max_scroll = 0;
    if (panel_scroll > max_scroll) panel_scroll = max_scroll;
    float panel_padding = item_size*0.1;

    uint64_t id = DJB2_INIT;
    id = djb2(id, file, strlen(file));
    id = djb2(id, &line, sizeof(line));

    BeginScissorMode(panel_boundary.x, panel_boundary.y, panel_boundary.width, panel_boundary.height);
    for (size_t i = 0; i < p->tracks.count; ++i) {
        Rectangle item_boundary = {
            .x = panel_boundary.x + panel_padding,
            .y = i*item_size + panel_boundary.y + panel_padding - panel_scroll,
            .width = panel_boundary.width - panel_padding*2 - scroll_bar_width,
            .height = item_size - panel_padding*2,
        };
        Color color;
        if (((int) i != p->current_track)) {
            uint64_t item_id = djb2(id, &i, sizeof(i));

            int state = button_with_id(item_id, GetCollisionRec(panel_boundary, item_boundary));
            if (state & BS_HOVEROVER) {
                color = COLOR_TRACK_BUTTON_HOVEROVER;
            } else {
                color = COLOR_TRACK_BUTTON_BACKGROUND;
            }
            if (state & BS_CLICKED) {
                Track *track = current_track();
                if (track) StopMusicStream(track->music);
                PlayMusicStream(p->tracks.items[i].music);
                p->current_track = i;
            }
        } else {
            color = COLOR_TRACK_BUTTON_SELECTED;
        }
        // TODO: enable MSAA so the rounded rectangles look better
        // That triggers an old raylib bug with circles tho, so we will have to look into that
        DrawRectangleRounded(item_boundary, 0.2, 20, color);

        const char *text = GetFileName(p->tracks.items[i].file_path);
        float fontSize = item_boundary.height*0.5;
        float text_padding = item_boundary.width*0.05;
        Vector2 size = MeasureTextEx(p->font, text, fontSize, 0);
        Vector2 position = {
            .x = item_boundary.x + text_padding,
            .y = item_boundary.y + item_boundary.height*0.5 - size.y*0.5,
        };
        // TODO: use SDF fonts
        // TODO: we need a better indication that the label was cut out because of the overflow
        // I was think about some sort of gradient. Ideally, we need to scroll the label on hover.
        track_label(p->font, text, position, fontSize, WHITE, item_boundary.width - text_padding*2);
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

    EndScissorMode();
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
        DrawRectangleRounded(popup_boundary, 0.3, 20, ColorAlpha(COLOR_POPUP_BACKGROUND, alpha));
        const char *text = "Could not load file";
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
    p->cancel_rendering = false;
    SetTraceLogLevel(LOG_WARNING);
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
    size_t buttons_count = 5;
#else
    size_t buttons_count = 4;
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
        // TODO: loading files synchronously like that actually blocks the UI thread
        // Maybe we should do that in a separate thread.
        for (size_t i = 0; i < droppedFiles.count; ++i) {
            Music music = LoadMusicStream(droppedFiles.paths[i]);
            if (IsMusicReady(music)) {
                AttachAudioStreamProcessor(music.stream, callback);
                char *file_path = strdup(droppedFiles.paths[i]);
                assert(file_path != NULL);
                nob_da_append(&p->tracks, (CLITERAL(Track) {
                    .file_path = file_path,
                    .music = music,
                }));
            } else {
                popup_tray_push(&p->pt);
            }
        }
        UnloadDroppedFiles(droppedFiles);

        if (current_track() == NULL && p->tracks.count > 0) {
            p->current_track = 0;
            PlayMusicStream(p->tracks.items[0].music);
        }
    }

#ifdef MUSIALIZER_MICROPHONE
    if (IsKeyPressed(KEY_CAPTURE)) start_capture();
#endif // MUSIALIZER_MICROPHONE

    Track *track = current_track();
    if (track) { // The music is loaded and ready
        UpdateMusicStream(track->music);

        if (IsKeyPressed(KEY_TOGGLE_PLAY)) {
            toggle_track_playing(track);
        }

        if (IsKeyPressed(KEY_RENDER)) {
            start_rendering_track(track);
        }

        if (IsKeyPressed(KEY_FULLSCREEN)) {
            p->fullscreen = !p->fullscreen;
        }

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

            tracks_panel((CLITERAL(Rectangle) {
                .x = 0,
                .y = 0,
                .width = tracks_panel_width,
                .height = h - timeline_height,
            }));

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
        const char *label = "Drag&Drop Music Here";
        Color color = WHITE;
        Vector2 size = MeasureTextEx(p->font, label, p->font.baseSize, 0);
        Vector2 position = {
            w/2 - size.x/2,
            h/2 - size.y/2,
        };
        DrawTextEx(p->font, label, position, p->font.baseSize, 0, color);
        popup_tray(&p->pt, CLITERAL(Rectangle) {
            .x = 0,
            .y = 0,
            .width = w,
            .height = h,
        });
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
            if (IsMusicReady(music)) {
                AttachAudioStreamProcessor(music.stream, callback);
                char *file_path = strdup(recording_file_path);
                assert(file_path != NULL);
                nob_da_append(&p->tracks, (CLITERAL(Track) {
                    .file_path = file_path,
                    .music = music,
                }));
            } else {
                popup_tray_push(&p->pt);
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
        if (p->wave_cursor >= p->wave.frameCount && fft_settled()) { // Rendering is finished
            if (!ffmpeg_end_rendering(p->ffmpeg, false)) {
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
        } else if (IsKeyPressed(KEY_ESCAPE) || p->cancel_rendering) {  // Rendering is cancelled
            ffmpeg_end_rendering(p->ffmpeg, true);
            p->ffmpeg = NULL;

            SetTraceLogLevel(LOG_INFO);
            UnloadWave(p->wave);
            UnloadWaveSamples(p->wave_samples);
            p->rendering = false;
            fft_clean();
            PlayMusicStream(track->music);
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

    const char *alegreya_path = "./resources/fonts/Alegreya-Regular.ttf";
    data = plug_load_resource(alegreya_path, &data_size);
        p->font = LoadFontFromMemory(GetFileExtension(alegreya_path), data, data_size, FONT_SIZE, NULL, 0);
        GenTextureMipmaps(&p->font.texture);
        SetTextureFilter(p->font.texture, TEXTURE_FILTER_BILINEAR);
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

    load_assets();
    p->screen = LoadRenderTexture(RENDER_WIDTH, RENDER_HEIGHT);
    p->current_track = -1;

    // TODO: restore master volume between sessions
    SetMasterVolume(0.5);
}

MUSIALIZER_PLUG void *plug_pre_reload(void)
{
    for (size_t i = 0; i < p->tracks.count; ++i) {
        Track *it = &p->tracks.items[i];
        DetachAudioStreamProcessor(it->music.stream, callback);
    }
    unload_assets();
    return p;
}

MUSIALIZER_PLUG void plug_post_reload(void *pp)
{
    p = pp;
    for (size_t i = 0; i < p->tracks.count; ++i) {
        Track *it = &p->tracks.items[i];
        AttachAudioStreamProcessor(it->music.stream, callback);
    }
    load_assets();
}

MUSIALIZER_PLUG void plug_update(void)
{
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

    end_tooltip_frame();

    EndDrawing();
}

// TODO: About Page that includes current commit, version and the platforms
// We may also include licenses and contributors there.
// TODO: Actual Fullscreen Mode
// We do have fullscreen button, but apparently there is a demand on a fullscreen fullscreen mode.
// Raylib does have ToggleFullscreen(). Let's see how we can integrate it into the current UI/UX
// TODO: Adding Files by Ctrl+C, Ctrl+V
