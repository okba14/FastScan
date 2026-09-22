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
    fs_size_t* match_buffer;
    fs_size_t buffer_capacity;
    volatile int should_exit;
} worker_context_t;

struct fs_thread_pool {
    worker_context_t workers[FS_MAX_POOL_WORKERS];
    int worker_count;
    int is_initialized;
};

static fs_thread_pool_t g_pool = {0};
static CRITICAL_SECTION g_dispatch_cs;
static int g_cs_initialized = 0;
static INIT_ONCE g_init_once = INIT_ONCE_STATIC_INIT;

static void execute_worker_task(worker_context_t* w, fs_worker_task_t* td) {
    if (!w || !td || td->chunk_start >= td->global_size || td->pattern_len == 0) return;

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

    // Use reusable per-worker buffer to eliminate massive heap allocation churn
    if (w->buffer_capacity < td->max_collect) {
        fs_size_t* new_buf = (fs_size_t*)realloc(w->match_buffer, td->max_collect * sizeof(fs_size_t));
        if (!new_buf) return;
        w->match_buffer = new_buf;
        w->buffer_capacity = td->max_collect;
    }

    fs_size_t raw_count = 0;
    fs_scan_raw(start, effective_len, td->pattern, pat_len, w->match_buffer, &raw_count, td->max_collect, &td->cpu);

    fs_size_t valid_count = 0;
    fs_size_t chunk_span = chunk_end - chunk_start;
    for (fs_size_t j = 0; j < raw_count; j++) {
        if (w->match_buffer[j] < chunk_span) {
            w->match_buffer[valid_count++] = chunk_start + w->match_buffer[j];
        }
    }

    // Allocate exact needed size for output
    if (valid_count > 0) {
        td->matches = (fs_size_t*)malloc(valid_count * sizeof(fs_size_t));
        if (td->matches) {
            memcpy(td->matches, w->match_buffer, valid_count * sizeof(fs_size_t));
            td->count = valid_count;
        } else {
            td->count = 0;
        }
    } else {
        td->matches = NULL;
        td->count = 0;
    }
}

static DWORD WINAPI persistent_worker_proc(LPVOID arg) {
    worker_context_t* w = (worker_context_t*)arg;

    while (1) {
        WaitForSingleObject(w->start_event, INFINITE);
        if (w->should_exit) break;

        if (w->task) {
            execute_worker_task(w, w->task);
            w->task = NULL;
        }

        SetEvent(w->done_event);
    }
    return 0;
}

static BOOL CALLBACK init_pool_windows_cb(PINIT_ONCE InitOnce, PVOID Parameter, PVOID *Context) {
    (void)InitOnce; (void)Parameter; (void)Context;

    InitializeCriticalSection(&g_dispatch_cs);
    g_cs_initialized = 1;

    int ncores = fs_platform_get_cpu_cores();
    int count = ncores > 0 ? (ncores > FS_MAX_POOL_WORKERS ? FS_MAX_POOL_WORKERS : ncores) : 1;

    g_pool.worker_count = count;

    for (int i = 0; i < count; i++) {
        g_pool.workers[i].start_event = CreateEventW(NULL, FALSE, FALSE, NULL);
        g_pool.workers[i].done_event = CreateEventW(NULL, FALSE, FALSE, NULL);
        g_pool.workers[i].should_exit = 0;
        g_pool.workers[i].task = NULL;
        g_pool.workers[i].match_buffer = NULL;
        g_pool.workers[i].buffer_capacity = 0;
        g_pool.workers[i].thread = CreateThread(NULL, 0, persistent_worker_proc, &g_pool.workers[i], 0, NULL);
    }

    g_pool.is_initialized = 1;
    return TRUE;
}

fs_thread_pool_t* fs_thread_pool_get_global(void) {
    InitOnceExecuteOnce(&g_init_once, init_pool_windows_cb, NULL, NULL);
    return &g_pool;
}

int fs_thread_pool_get_worker_count(void) {
    fs_thread_pool_t* pool = fs_thread_pool_get_global();
    return (pool && pool->worker_count > 0) ? pool->worker_count : 1;
}

int fs_thread_pool_dispatch(fs_worker_task_t* tasks, int task_count) {
    fs_thread_pool_t* pool = fs_thread_pool_get_global();
    if (!pool || task_count <= 0 || !tasks) return -1;

    // Mutex lock guarantees that concurrent requests NEVER race on worker contexts or stack memory
    EnterCriticalSection(&g_dispatch_cs);

    HANDLE done_handles[FS_MAX_POOL_WORKERS];
    int tasks_processed = 0;

    // Batching loop: NEVER drops a task; executes all chunks in pool batches
    while (tasks_processed < task_count) {
        int batch_size = task_count - tasks_processed;
        if (batch_size > pool->worker_count) {
            batch_size = pool->worker_count;
        }

        for (int i = 0; i < batch_size; i++) {
            pool->workers[i].task = &tasks[tasks_processed + i];
            done_handles[i] = pool->workers[i].done_event;
            SetEvent(pool->workers[i].start_event);
        }

        WaitForMultipleObjects((DWORD)batch_size, done_handles, TRUE, INFINITE);
        tasks_processed += batch_size;
    }

    LeaveCriticalSection(&g_dispatch_cs);
    return task_count;
}

void fs_thread_pool_shutdown(void) {
    if (!g_pool.is_initialized) return;

    if (g_cs_initialized) {
        EnterCriticalSection(&g_dispatch_cs);
    }

    for (int i = 0; i < g_pool.worker_count; i++) {
        g_pool.workers[i].should_exit = 1;
        SetEvent(g_pool.workers[i].start_event);
        WaitForSingleObject(g_pool.workers[i].thread, 2000);
        CloseHandle(g_pool.workers[i].thread);
        CloseHandle(g_pool.workers[i].start_event);
        CloseHandle(g_pool.workers[i].done_event);
        if (g_pool.workers[i].match_buffer) {
            free(g_pool.workers[i].match_buffer);
            g_pool.workers[i].match_buffer = NULL;
        }
    }

    g_pool.is_initialized = 0;
    if (g_cs_initialized) {
        LeaveCriticalSection(&g_dispatch_cs);
        DeleteCriticalSection(&g_dispatch_cs);
        g_cs_initialized = 0;
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
    fs_size_t* match_buffer;
    fs_size_t buffer_capacity;
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
static pthread_mutex_t g_dispatch_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t g_init_once = PTHREAD_ONCE_INIT;

static void execute_worker_task(posix_worker_t* w, fs_worker_task_t* td) {
    if (!w || !td || td->chunk_start >= td->global_size || td->pattern_len == 0) return;

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

    // Reuse per-worker buffer
    if (w->buffer_capacity < td->max_collect) {
        fs_size_t* new_buf = (fs_size_t*)realloc(w->match_buffer, td->max_collect * sizeof(fs_size_t));
        if (!new_buf) return;
        w->match_buffer = new_buf;
        w->buffer_capacity = td->max_collect;
    }

    fs_size_t raw_count = 0;
    fs_scan_raw(start, effective_len, td->pattern, pat_len, w->match_buffer, &raw_count, td->max_collect, &td->cpu);

    fs_size_t valid_count = 0;
    fs_size_t chunk_span = chunk_end - chunk_start;
    for (fs_size_t j = 0; j < raw_count; j++) {
        if (w->match_buffer[j] < chunk_span) {
            w->match_buffer[valid_count++] = chunk_start + w->match_buffer[j];
        }
    }

    if (valid_count > 0) {
        td->matches = (fs_size_t*)malloc(valid_count * sizeof(fs_size_t));
        if (td->matches) {
            memcpy(td->matches, w->match_buffer, valid_count * sizeof(fs_size_t));
            td->count = valid_count;
        } else {
            td->count = 0;
        }
    } else {
        td->matches = NULL;
        td->count = 0;
    }
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
            execute_worker_task(w, t);
        }

        pthread_mutex_lock(&w->mutex);
        w->has_task = 0;
        w->is_done = 1;
        pthread_cond_signal(&w->done_cond);
        pthread_mutex_unlock(&w->mutex);
    }
    return NULL;
}

static void init_pool_posix_cb(void) {
    int ncores = fs_platform_get_cpu_cores();
    int count = ncores > 0 ? (ncores > FS_MAX_POOL_WORKERS ? FS_MAX_POOL_WORKERS : ncores) : 1;

    g_pool.worker_count = count;

    for (int i = 0; i < count; i++) {
        pthread_mutex_init(&g_pool.workers[i].mutex, NULL);
        pthread_cond_init(&g_pool.workers[i].start_cond, NULL);
        pthread_cond_init(&g_pool.workers[i].done_cond, NULL);
        g_pool.workers[i].should_exit = 0;
        g_pool.workers[i].has_task = 0;
        g_pool.workers[i].is_done = 0;
        g_pool.workers[i].task = NULL;
        g_pool.workers[i].match_buffer = NULL;
        g_pool.workers[i].buffer_capacity = 0;
        pthread_create(&g_pool.workers[i].thread, NULL, posix_worker_proc, &g_pool.workers[i]);
    }

    g_pool.is_initialized = 1;
}

fs_thread_pool_t* fs_thread_pool_get_global(void) {
    pthread_once(&g_init_once, init_pool_posix_cb);
    return &g_pool;
}

int fs_thread_pool_dispatch(fs_worker_task_t* tasks, int task_count) {
    fs_thread_pool_t* pool = fs_thread_pool_get_global();
    if (!pool || task_count <= 0 || !tasks) return -1;

    pthread_mutex_lock(&g_dispatch_mutex);

    int tasks_processed = 0;

    // Batching loop: NEVER drops a task; executes all chunks in pool batches
    while (tasks_processed < task_count) {
        int batch_size = task_count - tasks_processed;
        if (batch_size > pool->worker_count) {
            batch_size = pool->worker_count;
        }

        for (int i = 0; i < batch_size; i++) {
            pthread_mutex_lock(&pool->workers[i].mutex);
            pool->workers[i].task = &tasks[tasks_processed + i];
            pool->workers[i].has_task = 1;
            pool->workers[i].is_done = 0;
            pthread_cond_signal(&pool->workers[i].start_cond);
            pthread_mutex_unlock(&pool->workers[i].mutex);
        }

        for (int i = 0; i < batch_size; i++) {
            pthread_mutex_lock(&pool->workers[i].mutex);
            while (!pool->workers[i].is_done) {
                pthread_cond_wait(&pool->workers[i].done_cond, &pool->workers[i].mutex);
            }
            pthread_mutex_unlock(&pool->workers[i].mutex);
        }

        tasks_processed += batch_size;
    }

    pthread_mutex_unlock(&g_dispatch_mutex);
    return task_count;
}

void fs_thread_pool_shutdown(void) {
    if (!g_pool.is_initialized) return;

    pthread_mutex_lock(&g_dispatch_mutex);

    for (int i = 0; i < g_pool.worker_count; i++) {
        pthread_mutex_lock(&g_pool.workers[i].mutex);
        g_pool.workers[i].should_exit = 1;
        pthread_cond_signal(&g_pool.workers[i].start_cond);
        pthread_mutex_unlock(&g_pool.workers[i].mutex);

        pthread_join(g_pool.workers[i].thread, NULL);
        pthread_mutex_destroy(&g_pool.workers[i].mutex);
        pthread_cond_destroy(&g_pool.workers[i].start_cond);
        pthread_cond_destroy(&g_pool.workers[i].done_cond);
        if (g_pool.workers[i].match_buffer) {
            free(g_pool.workers[i].match_buffer);
            g_pool.workers[i].match_buffer = NULL;
        }
    }

    g_pool.is_initialized = 0;
    pthread_mutex_unlock(&g_dispatch_mutex);
    memset(&g_pool, 0, sizeof(fs_thread_pool_t));
}

#endif
