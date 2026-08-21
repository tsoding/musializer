#ifndef MUSIALIZER_WIN32_UTF8_H_
#define MUSIALIZER_WIN32_UTF8_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <wchar.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

static inline wchar_t *win32_utf8_to_utf16(const char *text)
{
    if (text == NULL) return NULL;

    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    if (length <= 0) return NULL;

    wchar_t *result = malloc((size_t)length*sizeof(*result));
    if (result == NULL) return NULL;

    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, result, length) <= 0) {
        free(result);
        return NULL;
    }
    return result;
}

static inline bool win32_utf16_to_utf8(const wchar_t *text, char *result, size_t result_capacity)
{
    if (text == NULL || result == NULL || result_capacity == 0 || result_capacity > INT_MAX) return false;
    int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1,
                                     result, (int)result_capacity, NULL, NULL);
    return length > 0;
}

typedef struct {
    wchar_t *items;
    size_t count;
    size_t capacity;
} Win32_Wide_String;

static inline bool win32_wide_string_reserve(Win32_Wide_String *string, size_t extra)
{
    if (extra > SIZE_MAX - string->count) return false;
    size_t needed = string->count + extra;
    if (needed <= string->capacity) return true;

    size_t capacity = string->capacity ? string->capacity : 256;
    while (capacity < needed) {
        if (capacity > SIZE_MAX/2) {
            capacity = needed;
            break;
        }
        capacity *= 2;
    }

    wchar_t *items = realloc(string->items, capacity*sizeof(*items));
    if (items == NULL) return false;
    string->items = items;
    string->capacity = capacity;
    return true;
}

static inline bool win32_wide_string_append_char(Win32_Wide_String *string, wchar_t value)
{
    if (!win32_wide_string_reserve(string, 1)) return false;
    string->items[string->count++] = value;
    return true;
}

static inline bool win32_wide_string_append_repeat(Win32_Wide_String *string, wchar_t value, size_t count)
{
    if (!win32_wide_string_reserve(string, count)) return false;
    for (size_t i = 0; i < count; ++i) string->items[string->count++] = value;
    return true;
}

// Quotes arguments according to CommandLineToArgvW's parsing rules. Windows
// CreateProcess receives one command-line string rather than an argv array.
static inline wchar_t *win32_command_line_from_utf8_argv(const char *const argv[])
{
    if (argv == NULL || argv[0] == NULL) return NULL;

    Win32_Wide_String command = {0};
    for (size_t arg_index = 0; argv[arg_index] != NULL; ++arg_index) {
        wchar_t *arg = win32_utf8_to_utf16(argv[arg_index]);
        if (arg == NULL) goto fail;

        if (arg_index > 0 && !win32_wide_string_append_char(&command, L' ')) {
            free(arg);
            goto fail;
        }
        if (!win32_wide_string_append_char(&command, L'\"')) {
            free(arg);
            goto fail;
        }

        const wchar_t *cursor = arg;
        while (*cursor != L'\0') {
            size_t backslashes = 0;
            while (*cursor == L'\\') {
                ++backslashes;
                ++cursor;
            }

            if (*cursor == L'\"') {
                if (!win32_wide_string_append_repeat(&command, L'\\', backslashes*2 + 1) ||
                    !win32_wide_string_append_char(&command, *cursor++)) {
                    free(arg);
                    goto fail;
                }
            } else if (*cursor == L'\0') {
                if (!win32_wide_string_append_repeat(&command, L'\\', backslashes*2)) {
                    free(arg);
                    goto fail;
                }
            } else {
                if (!win32_wide_string_append_repeat(&command, L'\\', backslashes) ||
                    !win32_wide_string_append_char(&command, *cursor++)) {
                    free(arg);
                    goto fail;
                }
            }
        }

        free(arg);
        if (!win32_wide_string_append_char(&command, L'\"')) goto fail;
    }

    if (!win32_wide_string_append_char(&command, L'\0')) goto fail;
    return command.items;

fail:
    free(command.items);
    return NULL;
}

#endif // MUSIALIZER_WIN32_UTF8_H_
