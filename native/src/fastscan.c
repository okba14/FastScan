#include <stdlib.h>
#include <string.h>
#include <pthread.h> 
#include <unistd.h>  
#include <emmintrin.h>
#include "fastscan.h"
#include "mmap_reader.h"
#include "scanner.h"

#define INITIAL_THREAD_CAPACITY 4096

typedef struct {
    const fs_byte_t* start;
    fs_size_t size;
    const fs_byte_t* pattern;
    fs_size_t pattern_len;
    
    fs_size_t* matches;
    fs_size_t count;
    fs_size_t capacity;
    fs_size_t max_collect; 
    
    fs_size_t true_chunk_start;
    const fs_byte_t* global_start;
} __attribute__((aligned(64))) thread_data_t;

static int grow_buffer(thread_data_t* td) {
    if (td->count >= td->max_collect) return -1; 
    fs_size_t new_cap = td->capacity == 0 ? INITIAL_THREAD_CAPACITY : td->capacity * 2;
    if (new_cap > td->max_collect) new_cap = td->max_collect; 
    
    fs_size_t* new_buf = (fs_size_t*)realloc(td->matches, new_cap * sizeof(fs_size_t));
    if (!new_buf) return -1;
    td->matches = new_buf;
    td->capacity = new_cap;
    return 0;
}

static inline int verify_simd(const fs_byte_t* str, const fs_byte_t* pattern, fs_size_t len) {
    if (len <= 16) {
        __m128i p_vec = _mm_loadu_si128((const __m128i*)pattern);
        __m128i s_vec = _mm_loadu_si128((const __m128i*)str);
        __m128i cmp = _mm_cmpeq_epi8(p_vec, s_vec);
        unsigned int mask = _mm_movemask_epi8(cmp);
        unsigned int target_mask = (1U << len) - 1;
        return (mask & target_mask) == target_mask;
    }
    return memcmp(str, pattern, len) == 0;
}

void* worker_thread(void* arg) {
    thread_data_t* td = (thread_data_t*)arg;
    
    const fs_byte_t* p = td->start;
    const fs_byte_t* global_start = td->global_start;
    const fs_byte_t* limit = td->start + td->size - td->pattern_len + 1;
    const fs_byte_t* overlap_end = global_start + td->true_chunk_start;
    
    __m128i first_vec = _mm_set1_epi8(td->pattern[0]);
    fs_size_t pat_len = td->pattern_len;

    // PHASE 1: Overlap Region
    while (p < limit && p < overlap_end) {
        if (*p == td->pattern[0]) {
            if (verify_simd(p, td->pattern, pat_len)) {
                fs_size_t abs_off = (fs_size_t)(p - global_start);
                if (abs_off >= td->true_chunk_start) {
                    if (td->count >= td->capacity) { if(grow_buffer(td)) goto cleanup; }
                    td->matches[td->count++] = abs_off;
                }
            }
        }
        p++;
    }

    // PHASE 2: Main Chunk
    while ((uintptr_t)p % 16 != 0 && p < limit) {
        if (*p == td->pattern[0] && verify_simd(p, td->pattern, pat_len)) {
             if (td->count >= td->capacity) { if(grow_buffer(td)) goto cleanup; }
             td->matches[td->count++] = (fs_size_t)(p - global_start);
        }
        p++;
    }

    const fs_byte_t* simd_limit = limit - 16;
    while (p < simd_limit) {
        __m128i chunk = _mm_load_si128((const __m128i*)p);
        int mask = _mm_movemask_epi8(_mm_cmpeq_epi8(chunk, first_vec));

        while (mask) {
            int offset = __builtin_ctz(mask);
            const fs_byte_t* candidate = p + offset;

            if (verify_simd(candidate, td->pattern, pat_len)) {
                if (td->count >= td->capacity) { if(grow_buffer(td)) goto cleanup; }
                td->matches[td->count++] = (fs_size_t)(candidate - global_start);
            }
            mask &= mask - 1;
        }
        p += 16;
        

        if (td->count >= td->max_collect) goto cleanup;
    }

    while (p < limit) {
        if (*p == td->pattern[0] && verify_simd(p, td->pattern, pat_len)) {
             if (td->count >= td->capacity) { if(grow_buffer(td)) goto cleanup; }
             td->matches[td->count++] = (fs_size_t)(p - global_start);
        }
        p++;
    }

cleanup:
    return NULL;
}

fs_status_t fastscan_init(fastscan_ctx_t* ctx, const char* pattern, fs_size_t max_results) {
    if (!ctx || !pattern) return FS_ERROR_NULL_PTR;
    
    memset(ctx, 0, sizeof(fastscan_ctx_t));
    
    ctx->pattern = pattern;
    ctx->pattern_len = strlen(pattern);
    ctx->max_matches = max_results;
    ctx->is_initialized = 1;

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

    if (total_size < (256 * 1024)) { 
        ctx->matches = (fs_size_t*)malloc(sizeof(fs_size_t) * ctx->max_matches);
        if (!ctx->matches) return FS_ERROR_OUT_OF_BOUNDS;
        return fs_scan_raw(ctx->region.data, total_size, pattern, pattern_len, ctx->matches, &ctx->match_count, ctx->max_matches);
    }

    long nproc = sysconf(_SC_NPROCESSORS_ONLN);
    
    int nth = nproc > 1 ? (int)nproc - 1 : 1;
    
    pthread_t threads[nth];
    thread_data_t tds[nth];
    
    fs_size_t chunk_sz = ctx->region.size / nth;
    
    for (int i = 0; i < nth; i++) {
        tds[i].global_start = ctx->region.data;
        tds[i].pattern = (const fs_byte_t*)ctx->pattern;
        tds[i].pattern_len = ctx->pattern_len;
        tds[i].matches = NULL;
        tds[i].count = 0;
        tds[i].capacity = 0;

        tds[i].max_collect = ctx->max_matches; 
        
        fs_size_t start_off = i * chunk_sz;
        fs_size_t end_off = (i == nth - 1) ? ctx->region.size : (i + 1) * chunk_sz;
        
        tds[i].true_chunk_start = start_off;
        
        fs_size_t read_start = (i == 0) ? 0 : (start_off - pattern_len + 1);
        fs_size_t read_end = (i == nth - 1) ? ctx->region.size : (end_off + pattern_len - 1);
        
        if (read_end > ctx->region.size) read_end = ctx->region.size;
        
        tds[i].start = ctx->region.data + read_start;
        tds[i].size = read_end - read_start;
        
        pthread_create(&threads[i], NULL, worker_thread, &tds[i]);
    }
    
    fs_size_t total = 0;
    for (int i = 0; i < nth; i++) {
        pthread_join(threads[i], NULL);
        total += tds[i].count;
    }
    
    fs_size_t final_cnt = total > ctx->max_matches ? ctx->max_matches : total;
    ctx->matches = (fs_size_t*)malloc(final_cnt * sizeof(fs_size_t));
    if (!ctx->matches) {
         for(int i=0; i<nth; i++) free(tds[i].matches);
         return FS_ERROR_OUT_OF_BOUNDS;
    }
    
    ctx->match_count = 0;
    for (int i = 0; i < nth; i++) {
        for (fs_size_t j = 0; j < tds[i].count; j++) {
            if (ctx->match_count >= final_cnt) break;
            ctx->matches[ctx->match_count++] = tds[i].matches[j];
        }
        free(tds[i].matches);
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
