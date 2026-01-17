#include "scanner.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <emmintrin.h>

#define unlikely(x) __builtin_expect(!!(x), 0)
#define likely(x)   __builtin_expect(!!(x), 1)

static inline void check_remainder(const fs_byte_t* data, const fs_byte_t* pattern, fs_size_t pattern_len, const fs_byte_t* limit, fs_size_t* match_count, fs_size_t* matches, const fs_byte_t* base, fs_size_t max_matches) {
    const fs_byte_t* p = data;
    while (p < limit) {
        if (p[0] == pattern[0] && memcmp(p, pattern, pattern_len) == 0) {
            matches[(*match_count)++] = (fs_size_t)(p - base);
            if (unlikely(*match_count >= max_matches)) return;
        }
        p++;
    }
}

fs_status_t fs_scan_raw(const fs_byte_t* data, fs_size_t data_len, const fs_byte_t* pattern, fs_size_t pattern_len, fs_size_t* out_matches, fs_size_t* match_count, fs_size_t max_matches) {
    if (unlikely(max_matches == 0)) {
        *match_count = 0;
        return FS_SUCCESS;
    }

    const fs_byte_t* start = data;
    const fs_byte_t* end = data + data_len;
    const fs_byte_t* limit = end - pattern_len + 1;
    
    *match_count = 0;

    if (data_len < pattern_len) return FS_SUCCESS;

    if (likely(pattern_len == 5)) {
        const fs_byte_t b0 = pattern[0];
        const fs_byte_t b1 = pattern[1];
        const fs_byte_t b2 = pattern[2];
        const fs_byte_t b3 = pattern[3];
        const fs_byte_t b4 = pattern[4];

        const __m128i b0_vec = _mm_set1_epi8(b0);

        while (start < limit && (uintptr_t)start % 16 != 0) {
            if (start[0] == b0 && start[1] == b1 && start[2] == b2 && start[3] == b3 && start[4] == b4) {
                out_matches[(*match_count)++] = (fs_size_t)(start - data); 
                if (unlikely(*match_count >= max_matches)) return FS_SUCCESS;
            }
            start++;
        }

        const fs_byte_t* simd_limit = end - 16;
        
        while (start < simd_limit) {
            __builtin_prefetch(start + 128, 0, 3); 
            
            __m128i chunk = _mm_loadu_si128((const __m128i*)start);
            __m128i cmp = _mm_cmpeq_epi8(chunk, b0_vec);
            int mask = _mm_movemask_epi8(cmp);

            while (mask != 0) {
                int offset = __builtin_ctz(mask);
                
                if (start[offset+1] == b1 && start[offset+2] == b2 && start[offset+3] == b3 && start[offset+4] == b4) {
                    out_matches[(*match_count)++] = (fs_size_t)((start + offset) - data);
                    if (unlikely(*match_count >= max_matches)) return FS_SUCCESS;
                }
                
                mask &= mask - 1;
            }

            start += 16;
        }

        check_remainder(start, pattern, pattern_len, limit, match_count, out_matches, data, max_matches);
    } 
    else {
        const fs_byte_t b0 = pattern[0];
        while (start < limit) {
            const fs_byte_t* found = memchr(start, b0, (end - start));
            if (unlikely(found == NULL || found > limit)) break;

            if (memcmp(found, pattern, pattern_len) == 0) {
                out_matches[(*match_count)++] = (fs_size_t)(found - data);
                if (unlikely(*match_count >= max_matches)) break;
            }
            start = found + 1;
        }
    }

    return FS_SUCCESS;
}

fs_status_t fs_scan_run(fastscan_ctx_t* ctx) {
    if (!ctx || !ctx->region.data) return FS_ERROR_NULL_PTR;
    if (!ctx->pattern || ctx->pattern_len == 0) return FS_ERROR_INVALID_ARG;

    fs_size_t data_len = ctx->region.size;
    fs_size_t pattern_len = ctx->pattern_len;

    if (data_len < pattern_len) {
        ctx->match_count = 0;
        return FS_SUCCESS;
    }

    ctx->matches = (fs_size_t*)malloc(sizeof(fs_size_t) * ctx->max_matches);
    if (!ctx->matches) return FS_ERROR_OUT_OF_BOUNDS;
    ctx->match_count = 0;

    const fs_byte_t* pattern = (const fs_byte_t*)ctx->pattern;
    
    return fs_scan_raw(ctx->region.data, data_len, pattern, pattern_len, ctx->matches, &ctx->match_count, ctx->max_matches);
}
