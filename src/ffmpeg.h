#ifndef FFMPEG_H_
#define FFMPEG_H_

#include <stddef.h>
#include <stdbool.h>


typedef void FFMPEG;

FFMPEG *ffmpeg_start_rendering(size_t width, size_t height, size_t fps, const char *sound_file_path);
bool ffmpeg_send_frame_flipped(FFMPEG *ffmpeg, void *data, size_t width, size_t height);
bool ffmpeg_end_rendering(FFMPEG *ffmpeg);
char *get_file_name_as_mp4(const char *sound_file_path);
void _get_file_name_tests();
#endif // FFMPEG_H_
