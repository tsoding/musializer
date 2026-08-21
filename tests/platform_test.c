#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/platform.h"

typedef struct {
    Platform_Mutex *mutex;
    Platform_Condition *condition;
    bool ready;
} Thread_State;

static void *test_thread(void *arg)
{
    Thread_State *state = arg;
    platform_mutex_lock(state->mutex);
    state->ready = true;
    platform_condition_signal(state->condition);
    platform_mutex_unlock(state->mutex);
    return NULL;
}

int main(int argc, char **argv)
{
    const char *tricky_argument = "spaces, \"quotes\", and a trailing slash\\";
    if (argc == 3 && strcmp(argv[1], "--child") == 0) {
        return strcmp(argv[2], tricky_argument) == 0 ? 0 : 1;
    }

    Thread_State state = {0};
    state.mutex = platform_mutex_create();
    state.condition = platform_condition_create();
    assert(state.mutex != NULL);
    assert(state.condition != NULL);

    platform_mutex_lock(state.mutex);
    Platform_Thread *thread = platform_thread_start(test_thread, &state);
    assert(thread != NULL);
    while (!state.ready) platform_condition_wait(state.condition, state.mutex);
    platform_mutex_unlock(state.mutex);
    platform_thread_join(thread);

    assert(platform_mutex_try_lock(state.mutex));
    platform_mutex_unlock(state.mutex);

    platform_condition_destroy(state.condition);
    platform_mutex_destroy(state.mutex);

    char temp_path[4096];
    assert(platform_make_temp_file(temp_path, sizeof(temp_path), "musializer_test", ".wav"));
    size_t path_length = strlen(temp_path);
    assert(path_length >= 4);
    assert(strcmp(temp_path + path_length - 4, ".wav") == 0);

    FILE *temp_file = platform_fopen(temp_path, "wb");
    assert(temp_file != NULL);
    const char contents[] = "temporary file contents";
    size_t written = fwrite(contents, 1, sizeof(contents), temp_file);
    assert(written == sizeof(contents));
    fclose(temp_file);

    unsigned char *loaded_contents = NULL;
    size_t loaded_size = 0;
    assert(platform_read_entire_file(temp_path, &loaded_contents, &loaded_size));
    assert(loaded_size == sizeof(contents));
    assert(memcmp(loaded_contents, contents, sizeof(contents)) == 0);
    free(loaded_contents);
    assert(platform_remove_file(temp_path));

    const char *const child_argv[] = {argv[0], "--child", tricky_argument, NULL};
    assert(platform_run_command(child_argv, true));

    puts("platform tests passed");
    return 0;
}
