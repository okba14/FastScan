#include "scanner.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <emmintrin.h> // SSE2 Support (Safe for all x64)

#define unlikely(x) __builtin_expect(!!(x), 0)
#define likely(x)   __builtin_expect(!!(x), 1)

#define PREFETCH_DIST 320

// Branchless, Vectorized Verification for lengths <= 16 using SSE2
static inline int verify_sse2(const fs_byte_t* str, const fs_byte_t* pattern, fs_size_t len) {
    // Load 16 bytes
    __m128i p_vec = _mm_loadu_si128((const __m128i*)pattern);
    __m128i s_vec = _mm_loadu_si128((const __m128i*)str);
    
    // Compare all 16 bytes
    __m128i cmp = _mm_cmpeq_epi8(p_vec, s_vec);
    
    // Get mask
    unsigned int mask = _mm_movemask_epi8(cmp);
    
    // Create a target mask based on length
    unsigned int target_mask = (len >= 16) ? 0xFFFF : ((1U << len) - 1);
    
    return (mask & target_mask) == target_mask;
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

    // 1. Setup Vectors (SSE2)
    const __m128i first_byte_vec = _mm_set1_epi8(pattern[0]);
    
    // Two-byte prefilter optimization
    const __m128i second_byte_vec = (pattern_len > 1) ? 
        _mm_set1_epi8(pattern[1]) : _mm_set1_epi8(0);

    const fs_byte_t* sse_limit = end - 16;

    // 2. Main SSE2 Loop (Process 16 bytes per iteration)
    while (start < sse_limit) {
        __builtin_prefetch(start + PREFETCH_DIST, 0, 3);

        __m128i chunk = _mm_loadu_si128((const __m128i*)start);
        
        __m128i eq_first = _mm_cmpeq_epi8(chunk, first_byte_vec);
        unsigned int mask = _mm_movemask_epi8(eq_first);

        if (pattern_len > 1) {
            __m128i next_chunk = _mm_loadu_si128((const __m128i*)(start + 1));
            __m128i eq_second = _mm_cmpeq_epi8(next_chunk, second_byte_vec);
            unsigned int mask2 = _mm_movemask_epi8(eq_second);
            mask &= mask2;
        }

        while (mask != 0) {
            int offset = __builtin_ctz(mask);
            const fs_byte_t* candidate = start + offset;

            if (candidate < limit) {
                int is_match = 0;
                
                if (pattern_len <= 16) {
                    is_match = verify_sse2(candidate, pattern, pattern_len);
                } else {
                    is_match = memcmp(candidate, pattern, pattern_len) == 0;
                }

                if (is_match) {
                    if (*match_count < max_matches) {
                        out_matches[(*match_count)++] = (fs_size_t)(candidate - data);
                    } else {
                        return FS_SUCCESS;
                    }
                }
            }
            
            mask &= mask - 1;
        }

        start += 16;
    }

    // 3. Scalar Tail Cleanup
    while (start < limit) {
        if (start[0] == pattern[0]) {
            if (pattern_len == 1 || start[1] == pattern[1]) {
                 int is_match = 0;
                 if (pattern_len <= 16) is_match = verify_sse2(start, pattern, pattern_len);
                 else is_match = memcmp(start, pattern, pattern_len) == 0;

                 if (is_match) {
                    if (*match_count < max_matches) {
                        out_matches[(*match_count)++] = (fs_size_t)(start - data);
                    } else {
                        break;
                    }
                 }
            }
        }
        start++;
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
