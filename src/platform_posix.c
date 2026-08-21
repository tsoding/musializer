#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "platform.h"

struct Platform_Mutex {
    pthread_mutex_t value;
};

struct Platform_Condition {
    pthread_cond_t value;
};

struct Platform_Thread {
    pthread_t value;
};

Platform_Mutex *platform_mutex_create(void)
{
    Platform_Mutex *mutex = malloc(sizeof(*mutex));
    if (mutex == NULL) return NULL;
    if (pthread_mutex_init(&mutex->value, NULL) != 0) {
        free(mutex);
        return NULL;
    }
    return mutex;
}

void platform_mutex_destroy(Platform_Mutex *mutex)
{
    if (mutex == NULL) return;
    pthread_mutex_destroy(&mutex->value);
    free(mutex);
}

void platform_mutex_lock(Platform_Mutex *mutex)
{
    pthread_mutex_lock(&mutex->value);
}

bool platform_mutex_try_lock(Platform_Mutex *mutex)
{
    return pthread_mutex_trylock(&mutex->value) == 0;
}

void platform_mutex_unlock(Platform_Mutex *mutex)
{
    pthread_mutex_unlock(&mutex->value);
}

Platform_Condition *platform_condition_create(void)
{
    Platform_Condition *condition = malloc(sizeof(*condition));
    if (condition == NULL) return NULL;
    if (pthread_cond_init(&condition->value, NULL) != 0) {
        free(condition);
        return NULL;
    }
    return condition;
}

void platform_condition_destroy(Platform_Condition *condition)
{
    if (condition == NULL) return;
    pthread_cond_destroy(&condition->value);
    free(condition);
}

void platform_condition_wait(Platform_Condition *condition, Platform_Mutex *mutex)
{
    pthread_cond_wait(&condition->value, &mutex->value);
}

void platform_condition_signal(Platform_Condition *condition)
{
    pthread_cond_signal(&condition->value);
}

void platform_condition_broadcast(Platform_Condition *condition)
{
    pthread_cond_broadcast(&condition->value);
}

Platform_Thread *platform_thread_start(Platform_Thread_Function function, void *arg)
{
    Platform_Thread *thread = malloc(sizeof(*thread));
    if (thread == NULL) return NULL;
    if (pthread_create(&thread->value, NULL, function, arg) != 0) {
        free(thread);
        return NULL;
    }
    return thread;
}

void platform_thread_join(Platform_Thread *thread)
{
    if (thread == NULL) return;
    pthread_join(thread->value, NULL);
    free(thread);
}

bool platform_make_temp_file(char *path, size_t path_capacity, const char *prefix, const char *suffix)
{
    if (path == NULL || path_capacity == 0) return false;
    path[0] = '\0';
    if (prefix == NULL) prefix = "musializer";
    if (suffix == NULL) suffix = "";

    const char *temp_directory = getenv("TMPDIR");
    if (temp_directory == NULL || temp_directory[0] == '\0') temp_directory = "/tmp";

    char base_path[4096];
    int length = snprintf(base_path, sizeof(base_path), "%s/%s_XXXXXX", temp_directory, prefix);
    if (length < 0 || (size_t)length >= sizeof(base_path)) return false;

    int file = mkstemp(base_path);
    if (file < 0) return false;
    close(file);

    length = snprintf(path, path_capacity, "%s%s", base_path, suffix);
    if (length < 0 || (size_t)length >= path_capacity) {
        remove(base_path);
        path[0] = '\0';
        return false;
    }

    if (suffix[0] != '\0' && rename(base_path, path) != 0) {
        remove(base_path);
        path[0] = '\0';
        return false;
    }
    return true;
}

bool platform_remove_file(const char *path)
{
    if (path == NULL || path[0] == '\0') return true;
    return remove(path) == 0 || errno == ENOENT;
}

FILE *platform_fopen(const char *path, const char *mode)
{
    return fopen(path, mode);
}

bool platform_read_entire_file(const char *path, unsigned char **data, size_t *size)
{
    if (data == NULL || size == NULL) return false;
    *data = NULL;
    *size = 0;

    FILE *file = platform_fopen(path, "rb");
    if (file == NULL) return false;
    if (fseek(file, 0, SEEK_END) != 0) goto fail;
    long file_size = ftell(file);
    if (file_size < 0 || fseek(file, 0, SEEK_SET) != 0) goto fail;

    unsigned char *contents = malloc(file_size > 0 ? (size_t)file_size : 1);
    if (contents == NULL) goto fail;
    if (file_size > 0 && fread(contents, 1, (size_t)file_size, file) != (size_t)file_size) {
        free(contents);
        goto fail;
    }

    fclose(file);
    *data = contents;
    *size = (size_t)file_size;
    return true;

fail:
    fclose(file);
    return false;
}

bool platform_run_command(const char *const argv[], bool quiet)
{
    if (argv == NULL || argv[0] == NULL) return false;

    pid_t child = fork();
    if (child < 0) return false;
    if (child == 0) {
        if (quiet) {
            int null_file = open("/dev/null", O_WRONLY);
            if (null_file >= 0) {
                dup2(null_file, STDOUT_FILENO);
                dup2(null_file, STDERR_FILENO);
                close(null_file);
            }
        }
        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }

    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) return false;
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
