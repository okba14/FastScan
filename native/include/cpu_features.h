#ifndef FASTSCAN_CPU_FEATURES_H
#define FASTSCAN_CPU_FEATURES_H

#include <stdint.h>

typedef struct {
    int has_sse2;
    int has_avx2;
    int has_avx512;
    int has_neon;
} fs_cpu_features_t;

#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
    #include <intrin.h>
    static inline fs_cpu_features_t fs_detect_cpu_features(void) {
        fs_cpu_features_t f = {0};
        int info[4];
        __cpuid(info, 0);
        int nIds = info[0];

        if (nIds >= 1) {
            __cpuid(info, 1);
            f.has_sse2 = (info[3] & (1 << 26)) != 0;
            int osxsave = (info[2] & (1 << 27)) != 0;
            int avx = (info[2] & (1 << 28)) != 0;

            if (osxsave && avx && nIds >= 7) {
                unsigned long long xcrFeatureMask = _xgetbv(0);
                if ((xcrFeatureMask & 6) == 6) { // XMM and YMM state enabled
                    __cpuidex(info, 7, 0);
                    f.has_avx2 = (info[1] & (1 << 5)) != 0;
                    if ((xcrFeatureMask & 0xE6) == 0xE6) { // ZMM state enabled
                        f.has_avx512 = (info[1] & (1 << 16)) != 0;
                    }
                }
            }
        }
        return f;
    }
#elif defined(__GNUC__) || defined(__clang__)
    #if defined(__i386__) || defined(__x86_64__)
        #include <cpuid.h>
        static inline fs_cpu_features_t fs_detect_cpu_features(void) {
            fs_cpu_features_t f = {0};
            __builtin_cpu_init();
            f.has_sse2 = __builtin_cpu_supports("sse2");
            f.has_avx2 = __builtin_cpu_supports("avx2");
            f.has_avx512 = __builtin_cpu_supports("avx512f");
            return f;
        }
    #elif defined(__aarch64__) || defined(_M_ARM64)
        static inline fs_cpu_features_t fs_detect_cpu_features(void) {
            fs_cpu_features_t f = {0};
            f.has_neon = 1;
            return f;
        }
    #else
        static inline fs_cpu_features_t fs_detect_cpu_features(void) {
            fs_cpu_features_t f = {0};
            return f;
        }
    #endif
#else
    static inline fs_cpu_features_t fs_detect_cpu_features(void) {
        fs_cpu_features_t f = {0};
        f.has_sse2 = 1; // Standard baseline for modern x64
        return f;
    }
#endif

#endif // FASTSCAN_CPU_FEATURES_H
