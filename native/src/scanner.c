#include "../include/scanner.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    #include <immintrin.h>
    #if defined(_MSC_VER)
        #include <intrin.h>
    #endif
#endif

#if defined(__GNUC__) || defined(__clang__)
    #define unlikely(x) __builtin_expect(!!(x), 0)
    #define likely(x)   __builtin_expect(!!(x), 1)
#else
    #define unlikely(x) (x)
    #define likely(x)   (x)
#endif

#define FS_PREFETCH_DIST 512

static inline int verify_match(const fs_byte_t* str, const fs_byte_t* pattern, fs_size_t len) {
    if (len == 1) return 1;
    if (len == 2) return str[1] == pattern[1];
    return memcmp(str + 1, pattern + 1, len - 1) == 0;
}

// Scalar fallback scanner
fs_status_t fs_scan_scalar(const fs_byte_t* data, fs_size_t data_len, const fs_byte_t* pattern, fs_size_t pattern_len, fs_size_t* out_matches, fs_size_t* match_count, fs_size_t max_matches) {
    if (unlikely(max_matches == 0 || data_len < pattern_len || pattern_len == 0 || !data || !pattern || !out_matches || !match_count)) {
        if (match_count) *match_count = 0;
        return FS_SUCCESS;
    }
    *match_count = 0;

    const fs_byte_t* start = data;
    const fs_byte_t* limit = data + data_len - pattern_len + 1;
    fs_byte_t first = pattern[0];

    if (pattern_len == 1) {
        while (start < limit) {
            if (*start == first) {
                out_matches[(*match_count)++] = (fs_size_t)(start - data);
                if (*match_count >= max_matches) return FS_SUCCESS;
            }
            start++;
        }
        return FS_SUCCESS;
    }

    fs_byte_t last = pattern[pattern_len - 1];
    fs_size_t p_last_offset = pattern_len - 1;

    while (start < limit) {
        if (*start == first && *(start + p_last_offset) == last) {
            if (verify_match(start, pattern, pattern_len)) {
                out_matches[(*match_count)++] = (fs_size_t)(start - data);
                if (*match_count >= max_matches) return FS_SUCCESS;
            }
        }
        start++;
    }

    return FS_SUCCESS;
}

// AVX-512 High-Throughput 128-Byte Unrolled Scanner
#if defined(__AVX512F__) && defined(__AVX512BW__)
fs_status_t fs_scan_avx512(const fs_byte_t* data, fs_size_t data_len, const fs_byte_t* pattern, fs_size_t pattern_len, fs_size_t* out_matches, fs_size_t* match_count, fs_size_t max_matches) {
    if (unlikely(max_matches == 0 || data_len < pattern_len || pattern_len == 0 || !data || !pattern || !out_matches || !match_count)) {
        if (match_count) *match_count = 0;
        return FS_SUCCESS;
    }
    *match_count = 0;

    const fs_byte_t* start = data;
    const fs_byte_t* limit = data + data_len - pattern_len + 1;
    const fs_byte_t* avx512_limit = data + data_len - 128;

    const __m512i first_vec = _mm512_set1_epi8((char)pattern[0]);
    const fs_size_t last_idx = pattern_len - 1;
    const __m512i last_vec = _mm512_set1_epi8((char)pattern[last_idx]);

    // 2x Unrolled 128-byte AVX-512 loop
    while (start <= avx512_limit && start < limit) {
        _mm_prefetch((const char*)(start + FS_PREFETCH_DIST), _MM_HINT_T0);

        __m512i chunk0 = _mm512_loadu_si512((const void*)start);
        __m512i chunk1 = _mm512_loadu_si512((const void*)(start + 64));

        __mmask64 mask0 = _mm512_cmpeq_epi8_mask(chunk0, first_vec);
        __mmask64 mask1 = _mm512_cmpeq_epi8_mask(chunk1, first_vec);

        if (pattern_len > 1) {
            if (mask0 != 0 && start + last_idx + 64 <= data + data_len) {
                __m512i last0 = _mm512_loadu_si512((const void*)(start + last_idx));
                mask0 &= _mm512_cmpeq_epi8_mask(last0, last_vec);
            }
            if (mask1 != 0 && start + 64 + last_idx + 64 <= data + data_len) {
                __m512i last1 = _mm512_loadu_si512((const void*)(start + 64 + last_idx));
                mask1 &= _mm512_cmpeq_epi8_mask(last1, last_vec);
            }
        }

        while (mask0 != 0) {
            int offset = __builtin_ctzll(mask0);
            const fs_byte_t* candidate = start + offset;
            if (candidate < limit) {
                if (verify_match(candidate, pattern, pattern_len)) {
                    out_matches[(*match_count)++] = (fs_size_t)(candidate - data);
                    if (unlikely(*match_count >= max_matches)) return FS_SUCCESS;
                }
            }
            mask0 &= mask0 - 1;
        }

        while (mask1 != 0) {
            int offset = __builtin_ctzll(mask1);
            const fs_byte_t* candidate = start + 64 + offset;
            if (candidate < limit) {
                if (verify_match(candidate, pattern, pattern_len)) {
                    out_matches[(*match_count)++] = (fs_size_t)(candidate - data);
                    if (unlikely(*match_count >= max_matches)) return FS_SUCCESS;
                }
            }
            mask1 &= mask1 - 1;
        }

        start += 128;
    }

    // Single 64-byte AVX-512 block
    const fs_byte_t* avx512_single_limit = data + data_len - 64;
    while (start <= avx512_single_limit && start < limit) {
        __m512i chunk = _mm512_loadu_si512((const void*)start);
        __mmask64 mask = _mm512_cmpeq_epi8_mask(chunk, first_vec);

        if (pattern_len > 1 && mask != 0 && start + last_idx + 64 <= data + data_len) {
            __m512i chunk_last = _mm512_loadu_si512((const void*)(start + last_idx));
            mask &= _mm512_cmpeq_epi8_mask(chunk_last, last_vec);
        }

        while (mask != 0) {
            int offset = __builtin_ctzll(mask);
            const fs_byte_t* candidate = start + offset;
            if (candidate < limit) {
                if (verify_match(candidate, pattern, pattern_len)) {
                    out_matches[(*match_count)++] = (fs_size_t)(candidate - data);
                    if (unlikely(*match_count >= max_matches)) return FS_SUCCESS;
                }
            }
            mask &= mask - 1;
        }
        start += 64;
    }

    // Scalar tail cleanup
    while (start < limit) {
        if (*start == pattern[0]) {
            if (pattern_len == 1 || *(start + last_idx) == pattern[last_idx]) {
                if (verify_match(start, pattern, pattern_len)) {
                    out_matches[(*match_count)++] = (fs_size_t)(start - data);
                    if (*match_count >= max_matches) return FS_SUCCESS;
                }
            }
        }
        start++;
    }

    return FS_SUCCESS;
}
#else
fs_status_t fs_scan_avx512(const fs_byte_t* data, fs_size_t data_len, const fs_byte_t* pattern, fs_size_t pattern_len, fs_size_t* out_matches, fs_size_t* match_count, fs_size_t max_matches) {
    return fs_scan_avx2(data, data_len, pattern, pattern_len, out_matches, match_count, max_matches);
}
#endif

// AVX2 High-Throughput Unrolled Scanner (64 bytes per iteration)
#if defined(__AVX2__) || (defined(_MSC_VER) && (defined(__AVX2__) || defined(_M_X64)))
fs_status_t fs_scan_avx2(const fs_byte_t* data, fs_size_t data_len, const fs_byte_t* pattern, fs_size_t pattern_len, fs_size_t* out_matches, fs_size_t* match_count, fs_size_t max_matches) {
    if (unlikely(max_matches == 0 || data_len < pattern_len || pattern_len == 0 || !data || !pattern || !out_matches || !match_count)) {
        if (match_count) *match_count = 0;
        return FS_SUCCESS;
    }
    *match_count = 0;

    const fs_byte_t* start = data;
    const fs_byte_t* limit = data + data_len - pattern_len + 1;
    const fs_byte_t* avx_limit = data + data_len - 64;

    const __m256i first_vec = _mm256_set1_epi8((char)pattern[0]);
    const fs_size_t last_idx = pattern_len - 1;
    const __m256i last_vec = _mm256_set1_epi8((char)pattern[last_idx]);

    // 2x Unrolled 64-byte AVX2 main loop
    while (start <= avx_limit && start < limit) {
        _mm_prefetch((const char*)(start + FS_PREFETCH_DIST), _MM_HINT_T0);

        __m256i chunk0 = _mm256_loadu_si256((const __m256i*)start);
        __m256i chunk1 = _mm256_loadu_si256((const __m256i*)(start + 32));

        __m256i cmp0 = _mm256_cmpeq_epi8(chunk0, first_vec);
        __m256i cmp1 = _mm256_cmpeq_epi8(chunk1, first_vec);

        unsigned int mask0 = (unsigned int)_mm256_movemask_epi8(cmp0);
        unsigned int mask1 = (unsigned int)_mm256_movemask_epi8(cmp1);

        if (pattern_len == 1) {
            while (mask0 != 0) {
#if defined(_MSC_VER) && !defined(__clang__)
                unsigned long offset;
                _BitScanForward(&offset, mask0);
#else
                int offset = __builtin_ctz(mask0);
#endif
                out_matches[(*match_count)++] = (fs_size_t)(start + offset - data);
                if (unlikely(*match_count >= max_matches)) return FS_SUCCESS;
                mask0 &= mask0 - 1;
            }

            while (mask1 != 0) {
#if defined(_MSC_VER) && !defined(__clang__)
                unsigned long offset;
                _BitScanForward(&offset, mask1);
#else
                int offset = __builtin_ctz(mask1);
#endif
                out_matches[(*match_count)++] = (fs_size_t)(start + 32 + offset - data);
                if (unlikely(*match_count >= max_matches)) return FS_SUCCESS;
                mask1 &= mask1 - 1;
            }
        } else {
            if (mask0 != 0 && start + last_idx + 32 <= data + data_len) {
                __m256i last0 = _mm256_loadu_si256((const __m256i*)(start + last_idx));
                mask0 &= (unsigned int)_mm256_movemask_epi8(_mm256_cmpeq_epi8(last0, last_vec));
            }
            if (mask1 != 0 && start + 32 + last_idx + 32 <= data + data_len) {
                __m256i last1 = _mm256_loadu_si256((const __m256i*)(start + 32 + last_idx));
                mask1 &= (unsigned int)_mm256_movemask_epi8(_mm256_cmpeq_epi8(last1, last_vec));
            }

            while (mask0 != 0) {
#if defined(_MSC_VER) && !defined(__clang__)
                unsigned long offset;
                _BitScanForward(&offset, mask0);
#else
                int offset = __builtin_ctz(mask0);
#endif
                const fs_byte_t* candidate = start + offset;
                if (candidate < limit) {
                    if (verify_match(candidate, pattern, pattern_len)) {
                        out_matches[(*match_count)++] = (fs_size_t)(candidate - data);
                        if (unlikely(*match_count >= max_matches)) return FS_SUCCESS;
                    }
                }
                mask0 &= mask0 - 1;
            }

            while (mask1 != 0) {
#if defined(_MSC_VER) && !defined(__clang__)
                unsigned long offset;
                _BitScanForward(&offset, mask1);
#else
                int offset = __builtin_ctz(mask1);
#endif
                const fs_byte_t* candidate = start + 32 + offset;
                if (candidate < limit) {
                    if (verify_match(candidate, pattern, pattern_len)) {
                        out_matches[(*match_count)++] = (fs_size_t)(candidate - data);
                        if (unlikely(*match_count >= max_matches)) return FS_SUCCESS;
                    }
                }
                mask1 &= mask1 - 1;
            }
        }

        start += 64;
    }

    // Single AVX2 block (32-byte) loop
    const fs_byte_t* avx_single_limit = data + data_len - 32;
    while (start <= avx_single_limit && start < limit) {
        __m256i chunk = _mm256_loadu_si256((const __m256i*)start);
        unsigned int mask = (unsigned int)_mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk, first_vec));

        if (pattern_len > 1 && mask != 0 && start + last_idx + 32 <= data + data_len) {
            __m256i chunk_last = _mm256_loadu_si256((const __m256i*)(start + last_idx));
            mask &= (unsigned int)_mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk_last, last_vec));
        }

        while (mask != 0) {
#if defined(_MSC_VER) && !defined(__clang__)
            unsigned long offset;
            _BitScanForward(&offset, mask);
#else
            int offset = __builtin_ctz(mask);
#endif
            const fs_byte_t* candidate = start + offset;
            if (candidate < limit) {
                if (verify_match(candidate, pattern, pattern_len)) {
                    out_matches[(*match_count)++] = (fs_size_t)(candidate - data);
                    if (unlikely(*match_count >= max_matches)) return FS_SUCCESS;
                }
            }
            mask &= mask - 1;
        }
        start += 32;
    }

    // Scalar tail cleanup
    while (start < limit) {
        if (*start == pattern[0]) {
            if (pattern_len == 1 || *(start + last_idx) == pattern[last_idx]) {
                if (verify_match(start, pattern, pattern_len)) {
                    out_matches[(*match_count)++] = (fs_size_t)(start - data);
                    if (*match_count >= max_matches) return FS_SUCCESS;
                }
            }
        }
        start++;
    }

    return FS_SUCCESS;
}
#else
fs_status_t fs_scan_avx2(const fs_byte_t* data, fs_size_t data_len, const fs_byte_t* pattern, fs_size_t pattern_len, fs_size_t* out_matches, fs_size_t* match_count, fs_size_t max_matches) {
    return fs_scan_sse2(data, data_len, pattern, pattern_len, out_matches, match_count, max_matches);
}
#endif

// SSE2 Vectorized Scanner (32 bytes per iteration unrolled)
fs_status_t fs_scan_sse2(const fs_byte_t* data, fs_size_t data_len, const fs_byte_t* pattern, fs_size_t pattern_len, fs_size_t* out_matches, fs_size_t* match_count, fs_size_t max_matches) {
#if defined(__SSE2__) || defined(_M_X64) || defined(_M_IX86)
    if (unlikely(max_matches == 0 || data_len < pattern_len || pattern_len == 0 || !data || !pattern || !out_matches || !match_count)) {
        if (match_count) *match_count = 0;
        return FS_SUCCESS;
    }
    *match_count = 0;

    const fs_byte_t* start = data;
    const fs_byte_t* limit = data + data_len - pattern_len + 1;
    const fs_byte_t* sse_limit = data + data_len - 32;

    const __m128i first_vec = _mm_set1_epi8((char)pattern[0]);
    const fs_size_t last_idx = pattern_len - 1;
    const __m128i last_vec = _mm_set1_epi8((char)pattern[last_idx]);

    while (start <= sse_limit && start < limit) {
        _mm_prefetch((const char*)(start + FS_PREFETCH_DIST), _MM_HINT_T0);

        __m128i chunk0 = _mm_loadu_si128((const __m128i*)start);
        __m128i chunk1 = _mm_loadu_si128((const __m128i*)(start + 16));

        unsigned int mask0 = (unsigned int)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk0, first_vec));
        unsigned int mask1 = (unsigned int)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk1, first_vec));

        if (pattern_len > 1) {
            if (mask0 != 0 && start + last_idx + 16 <= data + data_len) {
                __m128i chunk_last0 = _mm_loadu_si128((const __m128i*)(start + last_idx));
                mask0 &= (unsigned int)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk_last0, last_vec));
            }
            if (mask1 != 0 && start + 16 + last_idx + 16 <= data + data_len) {
                __m128i chunk_last1 = _mm_loadu_si128((const __m128i*)(start + 16 + last_idx));
                mask1 &= (unsigned int)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk_last1, last_vec));
            }
        }

        while (mask0 != 0) {
#if defined(_MSC_VER) && !defined(__clang__)
            unsigned long offset;
            _BitScanForward(&offset, mask0);
#else
            int offset = __builtin_ctz(mask0);
#endif
            const fs_byte_t* candidate = start + offset;
            if (candidate < limit) {
                if (verify_match(candidate, pattern, pattern_len)) {
                    out_matches[(*match_count)++] = (fs_size_t)(candidate - data);
                    if (unlikely(*match_count >= max_matches)) return FS_SUCCESS;
                }
            }
            mask0 &= mask0 - 1;
        }

        while (mask1 != 0) {
#if defined(_MSC_VER) && !defined(__clang__)
            unsigned long offset;
            _BitScanForward(&offset, mask1);
#else
            int offset = __builtin_ctz(mask1);
#endif
            const fs_byte_t* candidate = start + 16 + offset;
            if (candidate < limit) {
                if (verify_match(candidate, pattern, pattern_len)) {
                    out_matches[(*match_count)++] = (fs_size_t)(candidate - data);
                    if (unlikely(*match_count >= max_matches)) return FS_SUCCESS;
                }
            }
            mask1 &= mask1 - 1;
        }

        start += 32;
    }

    // Scalar cleanup tail
    while (start < limit) {
        if (*start == pattern[0]) {
            if (pattern_len == 1 || *(start + last_idx) == pattern[last_idx]) {
                if (verify_match(start, pattern, pattern_len)) {
                    out_matches[(*match_count)++] = (fs_size_t)(start - data);
                    if (*match_count >= max_matches) return FS_SUCCESS;
                }
            }
        }
        start++;
    }

    return FS_SUCCESS;
#else
    return fs_scan_scalar(data, data_len, pattern, pattern_len, out_matches, match_count, max_matches);
#endif
}

// ARM NEON Vectorized Scanner (32 bytes per iteration)
#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(_M_ARM64)
#include <arm_neon.h>

fs_status_t fs_scan_neon(const fs_byte_t* data, fs_size_t data_len, const fs_byte_t* pattern, fs_size_t pattern_len, fs_size_t* out_matches, fs_size_t* match_count, fs_size_t max_matches) {
    if (unlikely(max_matches == 0 || data_len < pattern_len || pattern_len == 0 || !data || !pattern || !out_matches || !match_count)) {
        if (match_count) *match_count = 0;
        return FS_SUCCESS;
    }
    *match_count = 0;

    const fs_byte_t* start = data;
    const fs_byte_t* limit = data + data_len - pattern_len + 1;
    const fs_byte_t* neon_limit = data + data_len - 32;

    uint8x16_t first_vec = vdupq_n_u8(pattern[0]);
    fs_size_t last_idx = pattern_len - 1;
    uint8x16_t last_vec = vdupq_n_u8(pattern[last_idx]);

    while (start <= neon_limit && start < limit) {
        uint8x16_t chunk0 = vld1q_u8(start);
        uint8x16_t chunk1 = vld1q_u8(start + 16);

        uint8x16_t cmp0 = vceqq_u8(chunk0, first_vec);
        uint8x16_t cmp1 = vceqq_u8(chunk1, first_vec);

        if (pattern_len > 1) {
            if (start + last_idx + 16 <= data + data_len) {
                uint8x16_t last0 = vld1q_u8(start + last_idx);
                cmp0 = vandq_u8(cmp0, vceqq_u8(last0, last_vec));
            }
            if (start + 16 + last_idx + 16 <= data + data_len) {
                uint8x16_t last1 = vld1q_u8(start + 16 + last_idx);
                cmp1 = vandq_u8(cmp1, vceqq_u8(last1, last_vec));
            }
        }

        uint8x8_t narrow0 = vmovn_u16(vreinterpretq_u16_u8(cmp0));
        if (vget_lane_u64(vreinterpret_u64_u8(narrow0), 0) != 0) {
            uint8_t res[16];
            vst1q_u8(res, cmp0);
            for (int i = 0; i < 16; i++) {
                if (res[i] != 0) {
                    const fs_byte_t* candidate = start + i;
                    if (candidate < limit && verify_match(candidate, pattern, pattern_len)) {
                        out_matches[(*match_count)++] = (fs_size_t)(candidate - data);
                        if (unlikely(*match_count >= max_matches)) return FS_SUCCESS;
                    }
                }
            }
        }

        uint8x8_t narrow1 = vmovn_u16(vreinterpretq_u16_u8(cmp1));
        if (vget_lane_u64(vreinterpret_u64_u8(narrow1), 0) != 0) {
            uint8_t res[16];
            vst1q_u8(res, cmp1);
            for (int i = 0; i < 16; i++) {
                if (res[i] != 0) {
                    const fs_byte_t* candidate = start + 16 + i;
                    if (candidate < limit && verify_match(candidate, pattern, pattern_len)) {
                        out_matches[(*match_count)++] = (fs_size_t)(candidate - data);
                        if (unlikely(*match_count >= max_matches)) return FS_SUCCESS;
                    }
                }
            }
        }

        start += 32;
    }

    while (start < limit) {
        if (*start == pattern[0]) {
            if (pattern_len == 1 || *(start + last_idx) == pattern[last_idx]) {
                if (verify_match(start, pattern, pattern_len)) {
                    out_matches[(*match_count)++] = (fs_size_t)(start - data);
                    if (*match_count >= max_matches) return FS_SUCCESS;
                }
            }
        }
        start++;
    }

    return FS_SUCCESS;
}
#else
fs_status_t fs_scan_neon(const fs_byte_t* data, fs_size_t data_len, const fs_byte_t* pattern, fs_size_t pattern_len, fs_size_t* out_matches, fs_size_t* match_count, fs_size_t max_matches) {
    return fs_scan_scalar(data, data_len, pattern, pattern_len, out_matches, match_count, max_matches);
}
#endif

#if defined(FS_PLATFORM_POSIX)
static __thread sigjmp_buf g_fs_posix_sigbus_jmp;
static __thread volatile sig_atomic_t g_fs_posix_sigbus_active = 0;

static void fs_posix_sigbus_handler(int sig, siginfo_t* info, void* uctx) {
    (void)sig; (void)info; (void)uctx;
    if (g_fs_posix_sigbus_active) {
        siglongjmp(g_fs_posix_sigbus_jmp, 1);
    }
}
#endif

// Unified raw scanner with dynamic runtime CPU dispatch (AVX-512 -> AVX2 -> NEON -> SSE2 -> Scalar)
fs_status_t fs_scan_raw(const fs_byte_t* data, fs_size_t data_len, const fs_byte_t* pattern, fs_size_t pattern_len, fs_size_t* out_matches, fs_size_t* match_count, fs_size_t max_matches, const fs_cpu_features_t* cpu) {
    if (unlikely(!data || !pattern || pattern_len == 0 || max_matches == 0 || data_len < pattern_len || !out_matches || !match_count)) {
        if (match_count) *match_count = 0;
        return FS_SUCCESS;
    }

#if defined(_MSC_VER)
    __try {
#if defined(__AVX512F__) && defined(__AVX512BW__)
        if (cpu && cpu->has_avx512) {
            return fs_scan_avx512(data, data_len, pattern, pattern_len, out_matches, match_count, max_matches);
        }
#endif
        if (cpu && cpu->has_avx2) {
            return fs_scan_avx2(data, data_len, pattern, pattern_len, out_matches, match_count, max_matches);
        } else if (cpu && cpu->has_neon) {
            return fs_scan_neon(data, data_len, pattern, pattern_len, out_matches, match_count, max_matches);
        } else if (cpu && cpu->has_sse2) {
            return fs_scan_sse2(data, data_len, pattern, pattern_len, out_matches, match_count, max_matches);
        }
        return fs_scan_scalar(data, data_len, pattern, pattern_len, out_matches, match_count, max_matches);
    }
    __except ((GetExceptionCode() == 0xC0000006L /* STATUS_IN_PAGE_ERROR */ || GetExceptionCode() == 0xC0000005L /* STATUS_ACCESS_VIOLATION */) ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        if (match_count) *match_count = 0;
        return FS_ERROR_FILE_TRUNCATED;
    }
#elif defined(FS_PLATFORM_POSIX)
    struct sigaction sa, old_sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = fs_posix_sigbus_handler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGBUS, &sa, &old_sa);

    g_fs_posix_sigbus_active = 1;
    if (sigsetjmp(g_fs_posix_sigbus_jmp, 1) != 0) {
        g_fs_posix_sigbus_active = 0;
        sigaction(SIGBUS, &old_sa, NULL);
        if (match_count) *match_count = 0;
        return FS_ERROR_FILE_TRUNCATED;
    }

    fs_status_t status = FS_SUCCESS;
#if defined(__AVX512F__) && defined(__AVX512BW__)
    if (cpu && cpu->has_avx512) {
        status = fs_scan_avx512(data, data_len, pattern, pattern_len, out_matches, match_count, max_matches);
    } else
#endif
    if (cpu && cpu->has_avx2) {
        status = fs_scan_avx2(data, data_len, pattern, pattern_len, out_matches, match_count, max_matches);
    } else if (cpu && cpu->has_neon) {
        status = fs_scan_neon(data, data_len, pattern, pattern_len, out_matches, match_count, max_matches);
    } else if (cpu && cpu->has_sse2) {
        status = fs_scan_sse2(data, data_len, pattern, pattern_len, out_matches, match_count, max_matches);
    } else {
        status = fs_scan_scalar(data, data_len, pattern, pattern_len, out_matches, match_count, max_matches);
    }

    g_fs_posix_sigbus_active = 0;
    sigaction(SIGBUS, &old_sa, NULL);
    return status;
#else
#if defined(__AVX512F__) && defined(__AVX512BW__)
    if (cpu && cpu->has_avx512) {
        return fs_scan_avx512(data, data_len, pattern, pattern_len, out_matches, match_count, max_matches);
    }
#endif
    if (cpu && cpu->has_avx2) {
        return fs_scan_avx2(data, data_len, pattern, pattern_len, out_matches, match_count, max_matches);
    } else if (cpu && cpu->has_neon) {
        return fs_scan_neon(data, data_len, pattern, pattern_len, out_matches, match_count, max_matches);
    } else if (cpu && cpu->has_sse2) {
        return fs_scan_sse2(data, data_len, pattern, pattern_len, out_matches, match_count, max_matches);
    }
    return fs_scan_scalar(data, data_len, pattern, pattern_len, out_matches, match_count, max_matches);
#endif
}

// Multi-pattern Single-Pass Scanning Engine
fs_status_t fs_scan_multi_raw(
    const fs_byte_t* data, 
    fs_size_t data_len, 
    const char** patterns, 
    const fs_size_t* pattern_lens, 
    fs_size_t pattern_count, 
    fs_multi_match_t* out_matches, 
    fs_size_t* match_count, 
    fs_size_t max_matches, 
    const fs_cpu_features_t* cpu
) {
    if (!data || !patterns || !pattern_lens || pattern_count == 0 || max_matches == 0) {
        *match_count = 0;
        return FS_SUCCESS;
    }

    // Build 256-entry bitmap of initial characters
    uint8_t first_byte_map[256];
    memset(first_byte_map, 0, sizeof(first_byte_map));

    fs_size_t min_len = pattern_lens[0];
    for (fs_size_t i = 0; i < pattern_count; i++) {
        uint8_t c = (uint8_t)patterns[i][0];
        first_byte_map[c] = 1;
        if (pattern_lens[i] < min_len) min_len = pattern_lens[i];
    }

    const fs_byte_t* start = data;
    const fs_byte_t* limit = data + data_len - min_len + 1;
    *match_count = 0;

    while (start < limit) {
        uint8_t c = *start;
        if (first_byte_map[c]) {
            fs_size_t remaining = (fs_size_t)(data + data_len - start);

            for (fs_size_t p = 0; p < pattern_count; p++) {
                fs_size_t plen = pattern_lens[p];
                if (plen <= remaining && patterns[p][0] == (char)c) {
                    if (verify_match(start, (const fs_byte_t*)patterns[p], plen)) {
                        out_matches[*match_count].pattern_index = p;
                        out_matches[*match_count].offset = (fs_size_t)(start - data);
                        (*match_count)++;
                        if (*match_count >= max_matches) return FS_SUCCESS;
                    }
                }
            }
        }
        start++;
    }

    return FS_SUCCESS;
}

// Vectorized Line and Column Indexer (Counts \n up to offset at 40+ GB/s)
fs_status_t fs_calculate_line_col(const fs_byte_t* data, fs_size_t total_size, fs_size_t offset, fs_size_t* out_line, fs_size_t* out_col) {
    if (!data || offset > total_size || !out_line || !out_col) return FS_ERROR_INVALID_ARG;

    fs_size_t lines = 1;
    const fs_byte_t* p = data;
    const fs_byte_t* target = data + offset;
    const fs_byte_t* last_newline = data;

#if defined(__AVX2__) || (defined(_MSC_VER) && (defined(__AVX2__) || defined(_M_X64)))
    const __m256i nl_vec = _mm256_set1_epi8('\n');
    while (p + 32 <= target) {
        __m256i chunk = _mm256_loadu_si256((const __m256i*)p);
        unsigned int mask = (unsigned int)_mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk, nl_vec));
        if (mask != 0) {
#if defined(_MSC_VER) && !defined(__clang__)
            lines += __popcnt(mask);
            unsigned long last_bit_idx;
            _BitScanReverse(&last_bit_idx, mask);
            int last_bit = (int)last_bit_idx;
#else
            lines += __builtin_popcount(mask);
            int last_bit = 31 - __builtin_clz(mask);
#endif
            last_newline = p + last_bit;
        }
        p += 32;
    }
#endif

    while (p < target) {
        if (*p == '\n') {
            lines++;
            last_newline = p;
        }
        p++;
    }

    *out_line = lines;
    *out_col = (last_newline == data && *data != '\n') ? (offset + 1) : (offset - (last_newline - data));
    return FS_SUCCESS;
}

// Vectorized Batch Line & Column Indexer (Linear O(N) single-pass across all sorted match offsets)
fs_status_t fs_calculate_line_col_batch(const fs_byte_t* data, fs_size_t total_size, const fs_size_t* offsets, fs_size_t offset_count, fs_size_t* out_lines, fs_size_t* out_cols) {
    if (!data || !offsets || !out_lines || !out_cols) return FS_ERROR_INVALID_ARG;
    if (offset_count == 0) return FS_SUCCESS;

    fs_size_t current_line = 1;
    const fs_byte_t* last_newline = data;
    const fs_byte_t* p = data;

    for (fs_size_t i = 0; i < offset_count; i++) {
        fs_size_t target_offset = offsets[i];
        if (target_offset > total_size) target_offset = total_size;
        const fs_byte_t* target = data + target_offset;

#if defined(__AVX2__) || (defined(_MSC_VER) && (defined(__AVX2__) || defined(_M_X64)))
        const __m256i nl_vec = _mm256_set1_epi8('\n');
        while (p + 32 <= target) {
            __m256i chunk = _mm256_loadu_si256((const __m256i*)p);
            unsigned int mask = (unsigned int)_mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk, nl_vec));
            if (mask != 0) {
#if defined(_MSC_VER) && !defined(__clang__)
                current_line += __popcnt(mask);
                unsigned long last_bit_idx;
                _BitScanReverse(&last_bit_idx, mask);
                last_newline = p + (int)last_bit_idx;
#else
                current_line += __builtin_popcount(mask);
                int last_bit = 31 - __builtin_clz(mask);
                last_newline = p + last_bit;
#endif
            }
            p += 32;
        }
#endif

        while (p < target) {
            if (*p == '\n') {
                current_line++;
                last_newline = p;
            }
            p++;
        }

        out_lines[i] = current_line;
        out_cols[i] = (last_newline == data && *data != '\n') ? (target_offset + 1) : (target_offset - (last_newline - data));
    }

    return FS_SUCCESS;
}

// Native Context Extractor: Extracts text snippet directly from mapped memory (Zero Syscalls)
fs_status_t fs_extract_context(const fs_byte_t* data, fs_size_t total_size, fs_size_t offset, fs_size_t pattern_len, fs_size_t before_bytes, fs_size_t after_bytes, const char** out_snippet, fs_size_t* out_snippet_len) {
    if (!data || !out_snippet || !out_snippet_len || offset >= total_size) {
        return FS_ERROR_INVALID_ARG;
    }

    fs_size_t start_pos = (offset > before_bytes) ? (offset - before_bytes) : 0;
    fs_size_t end_pos = (offset + pattern_len + after_bytes < total_size) ? (offset + pattern_len + after_bytes) : total_size;

    *out_snippet = (const char*)(data + start_pos);
    *out_snippet_len = end_pos - start_pos;

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
    return fs_scan_raw(ctx->region.data, data_len, pattern, pattern_len, ctx->matches, &ctx->match_count, ctx->max_matches, &ctx->cpu);
}
