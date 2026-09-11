#ifndef FASTSCAN_THREAD_POOL_H
#define FASTSCAN_THREAD_POOL_H

#include "safe_types.h"
#include "platform.h"
#include "cpu_features.h"

#define FS_MAX_POOL_WORKERS 32

typedef struct {
    const fs_byte_t* global_start;
    fs_size_t global_size;
    fs_size_t chunk_start;
    fs_size_t chunk_end;

    const fs_byte_t* pattern;
    fs_size_t pattern_len;

    fs_size_t* matches;
    fs_size_t count;
    fs_size_t max_collect;

    fs_cpu_features_t cpu;
} fs_worker_task_t;

typedef struct fs_thread_pool fs_thread_pool_t;

fs_thread_pool_t* fs_thread_pool_get_global(void);
int fs_thread_pool_dispatch(fs_worker_task_t* tasks, int task_count);
void fs_thread_pool_shutdown(void);

#endif // FASTSCAN_THREAD_POOL_H
