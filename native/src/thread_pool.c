#include "../include/thread_pool.h"
#include "../include/scanner.h"
#include <stdlib.h>
#include <string.h>

#if defined(FS_PLATFORM_WINDOWS)

typedef struct {
    HANDLE thread;
    HANDLE start_event;
    HANDLE done_event;
    fs_worker_task_t* task;
    volatile int should_exit;
} worker_context_t;

struct fs_thread_pool {
    worker_context_t workers[FS_MAX_POOL_WORKERS];
    int worker_count;
    int is_initialized;
};

static fs_thread_pool_t g_pool = {0};

static void execute_worker_task(fs_worker_task_t* td) {
    if (!td || td->chunk_start >= td->global_size || td->pattern_len == 0) return;

    const fs_byte_t* global_start = td->global_start;
    fs_size_t total_size = td->global_size;
    fs_size_t pat_len = td->pattern_len;
    fs_size_t chunk_start = td->chunk_start;
    fs_size_t chunk_end = td->chunk_end;

    const fs_byte_t* start = global_start + chunk_start;

    fs_size_t read_end = chunk_end + pat_len - 1;
    if (read_end > total_size) read_end = total_size;

    fs_size_t effective_len = (read_end >= chunk_start) ? (read_end - chunk_start) : 0;
    if (effective_len < pat_len) return;

    td->matches = (fs_size_t*)malloc(td->max_collect * sizeof(fs_size_t));
    if (!td->matches) return;

    fs_size_t raw_count = 0;
    fs_scan_raw(start, effective_len, td->pattern, pat_len, td->matches, &raw_count, td->max_collect, &td->cpu);

    fs_size_t valid_count = 0;
    fs_size_t chunk_span = chunk_end - chunk_start;
    for (fs_size_t j = 0; j < raw_count; j++) {
        if (td->matches[j] < chunk_span) {
            td->matches[valid_count++] = chunk_start + td->matches[j];
        }
    }
    td->count = valid_count;
}

static DWORD WINAPI persistent_worker_proc(LPVOID arg) {
    worker_context_t* w = (worker_context_t*)arg;

    while (1) {
        WaitForSingleObject(w->start_event, INFINITE);
        if (w->should_exit) break;

        if (w->task) {
            execute_worker_task(w->task);
            w->task = NULL;
        }

        SetEvent(w->done_event);
    }
    return 0;
}

fs_thread_pool_t* fs_thread_pool_get_global(void) {
    if (g_pool.is_initialized) return &g_pool;

    int ncores = fs_platform_get_cpu_cores();
    int count = ncores > 1 ? ncores - 1 : 1;
    if (count > FS_MAX_POOL_WORKERS) count = FS_MAX_POOL_WORKERS;

    g_pool.worker_count = count;

    for (int i = 0; i < count; i++) {
        g_pool.workers[i].start_event = CreateEventW(NULL, FALSE, FALSE, NULL);
        g_pool.workers[i].done_event = CreateEventW(NULL, FALSE, FALSE, NULL);
        g_pool.workers[i].should_exit = 0;
        g_pool.workers[i].task = NULL;
        g_pool.workers[i].thread = CreateThread(NULL, 0, persistent_worker_proc, &g_pool.workers[i], 0, NULL);
    }

    g_pool.is_initialized = 1;
    return &g_pool;
}

int fs_thread_pool_dispatch(fs_worker_task_t* tasks, int task_count) {
    fs_thread_pool_t* pool = fs_thread_pool_get_global();
    if (!pool || task_count <= 0) return -1;

    if (task_count > pool->worker_count) task_count = pool->worker_count;

    HANDLE done_handles[FS_MAX_POOL_WORKERS];

    for (int i = 0; i < task_count; i++) {
        pool->workers[i].task = &tasks[i];
        done_handles[i] = pool->workers[i].done_event;
        SetEvent(pool->workers[i].start_event);
    }

    WaitForMultipleObjects((DWORD)task_count, done_handles, TRUE, INFINITE);
    return task_count;
}

void fs_thread_pool_shutdown(void) {
    if (!g_pool.is_initialized) return;

    for (int i = 0; i < g_pool.worker_count; i++) {
        g_pool.workers[i].should_exit = 1;
        SetEvent(g_pool.workers[i].start_event);
        WaitForSingleObject(g_pool.workers[i].thread, 2000);
        CloseHandle(g_pool.workers[i].thread);
        CloseHandle(g_pool.workers[i].start_event);
        CloseHandle(g_pool.workers[i].done_event);
    }

    memset(&g_pool, 0, sizeof(fs_thread_pool_t));
}

#else // POSIX (Linux / macOS)

#include <pthread.h>

typedef struct {
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t start_cond;
    pthread_cond_t done_cond;
    fs_worker_task_t* task;
    volatile int has_task;
    volatile int is_done;
    volatile int should_exit;
} posix_worker_t;

struct fs_thread_pool {
    posix_worker_t workers[FS_MAX_POOL_WORKERS];
    int worker_count;
    int is_initialized;
};

static fs_thread_pool_t g_pool = {0};

static void execute_worker_task(fs_worker_task_t* td) {
    if (!td || td->chunk_start >= td->global_size || td->pattern_len == 0) return;

    const fs_byte_t* global_start = td->global_start;
    fs_size_t total_size = td->global_size;
    fs_size_t pat_len = td->pattern_len;
    fs_size_t chunk_start = td->chunk_start;
    fs_size_t chunk_end = td->chunk_end;

    const fs_byte_t* start = global_start + chunk_start;

    fs_size_t read_end = chunk_end + pat_len - 1;
    if (read_end > total_size) read_end = total_size;

    fs_size_t effective_len = (read_end >= chunk_start) ? (read_end - chunk_start) : 0;
    if (effective_len < pat_len) return;

    td->matches = (fs_size_t*)malloc(td->max_collect * sizeof(fs_size_t));
    if (!td->matches) return;

    fs_size_t raw_count = 0;
    fs_scan_raw(start, effective_len, td->pattern, pat_len, td->matches, &raw_count, td->max_collect, &td->cpu);

    fs_size_t valid_count = 0;
    fs_size_t chunk_span = chunk_end - chunk_start;
    for (fs_size_t j = 0; j < raw_count; j++) {
        if (td->matches[j] < chunk_span) {
            td->matches[valid_count++] = chunk_start + td->matches[j];
        }
    }
    td->count = valid_count;
}

static void* posix_worker_proc(void* arg) {
    posix_worker_t* w = (posix_worker_t*)arg;

    while (1) {
        pthread_mutex_lock(&w->mutex);
        while (!w->has_task && !w->should_exit) {
            pthread_cond_wait(&w->start_cond, &w->mutex);
        }
        if (w->should_exit) {
            pthread_mutex_unlock(&w->mutex);
            break;
        }

        fs_worker_task_t* t = w->task;
        pthread_mutex_unlock(&w->mutex);

        if (t) {
            execute_worker_task(t);
        }

        pthread_mutex_lock(&w->mutex);
        w->has_task = 0;
        w->is_done = 1;
        pthread_cond_signal(&w->done_cond);
        pthread_mutex_unlock(&w->mutex);
    }
    return NULL;
}

fs_thread_pool_t* fs_thread_pool_get_global(void) {
    if (g_pool.is_initialized) return &g_pool;

    int ncores = fs_platform_get_cpu_cores();
    int count = ncores > 1 ? ncores - 1 : 1;
    if (count > FS_MAX_POOL_WORKERS) count = FS_MAX_POOL_WORKERS;

    g_pool.worker_count = count;

    for (int i = 0; i < count; i++) {
        pthread_mutex_init(&g_pool.workers[i].mutex, NULL);
        pthread_cond_init(&g_pool.workers[i].start_cond, NULL);
        pthread_cond_init(&g_pool.workers[i].done_cond, NULL);
        g_pool.workers[i].should_exit = 0;
        g_pool.workers[i].has_task = 0;
        g_pool.workers[i].is_done = 0;
        g_pool.workers[i].task = NULL;
        pthread_create(&g_pool.workers[i].thread, NULL, posix_worker_proc, &g_pool.workers[i]);
    }

    g_pool.is_initialized = 1;
    return &g_pool;
}

int fs_thread_pool_dispatch(fs_worker_task_t* tasks, int task_count) {
    fs_thread_pool_t* pool = fs_thread_pool_get_global();
    if (!pool || task_count <= 0) return -1;

    if (task_count > pool->worker_count) task_count = pool->worker_count;

    for (int i = 0; i < task_count; i++) {
        pthread_mutex_lock(&pool->workers[i].mutex);
        pool->workers[i].task = &tasks[i];
        pool->workers[i].has_task = 1;
        pool->workers[i].is_done = 0;
        pthread_cond_signal(&pool->workers[i].start_cond);
        pthread_mutex_unlock(&pool->workers[i].mutex);
    }

    for (int i = 0; i < task_count; i++) {
        pthread_mutex_lock(&pool->workers[i].mutex);
        while (!pool->workers[i].is_done) {
            pthread_cond_wait(&pool->workers[i].done_cond, &pool->workers[i].mutex);
        }
        pthread_mutex_unlock(&pool->workers[i].mutex);
    }

    return task_count;
}

void fs_thread_pool_shutdown(void) {
    if (!g_pool.is_initialized) return;

    for (int i = 0; i < g_pool.worker_count; i++) {
        pthread_mutex_lock(&g_pool.workers[i].mutex);
        g_pool.workers[i].should_exit = 1;
        pthread_cond_signal(&g_pool.workers[i].start_cond);
        pthread_mutex_unlock(&g_pool.workers[i].mutex);

        pthread_join(g_pool.workers[i].thread, NULL);
        pthread_mutex_destroy(&g_pool.workers[i].mutex);
        pthread_cond_destroy(&g_pool.workers[i].start_cond);
        pthread_cond_destroy(&g_pool.workers[i].done_cond);
    }

    memset(&g_pool, 0, sizeof(fs_thread_pool_t));
}

#endif
