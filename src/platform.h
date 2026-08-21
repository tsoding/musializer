#ifndef MUSIALIZER_PLATFORM_H_
#define MUSIALIZER_PLATFORM_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

typedef struct Platform_Condition Platform_Condition;
typedef struct Platform_Mutex Platform_Mutex;
typedef struct Platform_Thread Platform_Thread;

typedef void *(*Platform_Thread_Function)(void *arg);

Platform_Mutex *platform_mutex_create(void);
void platform_mutex_destroy(Platform_Mutex *mutex);
void platform_mutex_lock(Platform_Mutex *mutex);
bool platform_mutex_try_lock(Platform_Mutex *mutex);
void platform_mutex_unlock(Platform_Mutex *mutex);

Platform_Condition *platform_condition_create(void);
void platform_condition_destroy(Platform_Condition *condition);
void platform_condition_wait(Platform_Condition *condition, Platform_Mutex *mutex);
void platform_condition_signal(Platform_Condition *condition);
void platform_condition_broadcast(Platform_Condition *condition);

Platform_Thread *platform_thread_start(Platform_Thread_Function function, void *arg);
void platform_thread_join(Platform_Thread *thread);

// Creates an empty, uniquely named file in the system temporary directory.
// `suffix` should include its leading dot (for example, ".wav").
bool platform_make_temp_file(char *path, size_t path_capacity, const char *prefix, const char *suffix);
bool platform_remove_file(const char *path);
FILE *platform_fopen(const char *path, const char *mode);
bool platform_read_entire_file(const char *path, unsigned char **data, size_t *size);

// Runs a NULL-terminated argument vector and waits for it to finish. argv[0]
// is both the executable name and the first argument passed to the process.
bool platform_run_command(const char *const argv[], bool quiet);

#endif // MUSIALIZER_PLATFORM_H_
