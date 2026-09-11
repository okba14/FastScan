#include "../include/fastscan.h"
#include "../include/mmap_reader.h"
#include "../include/scanner.h"
#include "../include/platform.h"
#include "../include/thread_pool.h"
#include <stdlib.h>
#include <string.h>

#define FS_MIN_CHUNK_PER_THREAD (16 * 1024 * 1024)

fs_status_t fastscan_init(fastscan_ctx_t* ctx, const char* pattern, fs_size_t max_results) {
    if (!ctx || !pattern) return FS_ERROR_NULL_PTR;

    memset(ctx, 0, sizeof(fastscan_ctx_t));
    ctx->pattern = pattern;
    ctx->pattern_len = strlen(pattern);
    ctx->max_matches = (max_results == 0) ? FS_DEFAULT_MAX_MATCHES : max_results;
    ctx->cpu = fs_detect_cpu_features();
    ctx->is_initialized = 1;

    // Ensure persistent pool is warmed up in background
    fs_thread_pool_get_global();

    return FS_SUCCESS;
}

fs_status_t fastscan_load_file(fastscan_ctx_t* ctx, const char* filepath) {
    if (!ctx) return FS_ERROR_NULL_PTR;
    return fs_mmap_open(filepath, &ctx->region);
}

fs_status_t fastscan_execute(fastscan_ctx_t* ctx) {
    if (!ctx || !ctx->is_initialized) return FS_ERROR_NULL_PTR;

    fs_size_t total_size = ctx->region.size;
    const fs_byte_t* pattern = (const fs_byte_t*)ctx->pattern;
    const fs_size_t pattern_len = ctx->pattern_len;

    if (total_size < pattern_len) {
        ctx->match_count = 0;
        ctx->matches = NULL;
        return FS_SUCCESS;
    }

    int ncores = fs_platform_get_cpu_cores();

    // Scale threads according to data size to ensure thread dispatch overhead never exceeds scan time
    int max_threads = ncores > 1 ? (ncores > 16 ? 16 : ncores) : 1;
    int needed_threads = (int)(total_size / FS_MIN_CHUNK_PER_THREAD);
    if (needed_threads < 1) needed_threads = 1;
    if (needed_threads > max_threads) needed_threads = max_threads;

    // Single-threaded fast path: zero dispatch, maximum L1/L2 cache locality
    if (needed_threads <= 1) {
        ctx->matches = (fs_size_t*)malloc(sizeof(fs_size_t) * ctx->max_matches);
        if (!ctx->matches) return FS_ERROR_OUT_OF_BOUNDS;
        ctx->match_count = 0;
        return fs_scan_raw(ctx->region.data, total_size, pattern, pattern_len, ctx->matches, &ctx->match_count, ctx->max_matches, &ctx->cpu);
    }

    int nth = needed_threads;
    fs_worker_task_t tasks[FS_MAX_POOL_WORKERS];
    fs_size_t chunk_sz = total_size / nth;

    for (int i = 0; i < nth; i++) {
        tasks[i].global_start = ctx->region.data;
        tasks[i].global_size = total_size;
        tasks[i].chunk_start = (fs_size_t)i * chunk_sz;
        tasks[i].chunk_end = (i == nth - 1) ? total_size : (fs_size_t)(i + 1) * chunk_sz;
        tasks[i].pattern = pattern;
        tasks[i].pattern_len = pattern_len;
        tasks[i].matches = NULL;
        tasks[i].count = 0;
        tasks[i].max_collect = ctx->max_matches;
        tasks[i].cpu = ctx->cpu;
    }

    // Ultra-fast sub-microsecond dispatch to warm persistent thread pool
    fs_thread_pool_dispatch(tasks, nth);

    fs_size_t total_found = 0;
    for (int i = 0; i < nth; i++) {
        total_found += tasks[i].count;
    }

    fs_size_t final_cnt = total_found > ctx->max_matches ? ctx->max_matches : total_found;
    if (final_cnt == 0) {
        ctx->matches = NULL;
        ctx->match_count = 0;
        for (int i = 0; i < nth; i++) {
            if (tasks[i].matches) free(tasks[i].matches);
        }
        return FS_SUCCESS;
    }

    ctx->matches = (fs_size_t*)malloc(final_cnt * sizeof(fs_size_t));
    if (!ctx->matches) {
        for (int i = 0; i < nth; i++) {
            if (tasks[i].matches) free(tasks[i].matches);
        }
        return FS_ERROR_OUT_OF_BOUNDS;
    }

    ctx->match_count = 0;
    for (int i = 0; i < nth; i++) {
        if (tasks[i].matches) {
            for (fs_size_t j = 0; j < tasks[i].count; j++) {
                if (ctx->match_count >= final_cnt) break;
                ctx->matches[ctx->match_count++] = tasks[i].matches[j];
            }
            free(tasks[i].matches);
        }
    }

    return FS_SUCCESS;
}

void fastscan_destroy(fastscan_ctx_t* ctx) {
    if (!ctx) return;

    fs_mmap_close(&ctx->region);

    if (ctx->matches) {
        free(ctx->matches);
        ctx->matches = NULL;
    }

    ctx->match_count = 0;
    ctx->is_initialized = 0;
}
