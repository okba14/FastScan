#ifndef FASTSCAN_SCANNER_H
#define FASTSCAN_SCANNER_H

#include "fastscan.h"
#include "safe_types.h"
#include "cpu_features.h"

// Multi-pattern match descriptor
typedef struct {
    fs_size_t pattern_index;
    fs_size_t offset;
} fs_multi_match_t;

// Single-pattern scanner prototypes
fs_status_t fs_scan_run(fastscan_ctx_t* ctx);
fs_status_t fs_scan_raw(const fs_byte_t* data, fs_size_t data_len, const fs_byte_t* pattern, fs_size_t pattern_len, fs_size_t* out_matches, fs_size_t* match_count, fs_size_t max_matches, const fs_cpu_features_t* cpu);

// Vectorized SIMD engines
fs_status_t fs_scan_avx512(const fs_byte_t* data, fs_size_t data_len, const fs_byte_t* pattern, fs_size_t pattern_len, fs_size_t* out_matches, fs_size_t* match_count, fs_size_t max_matches);
fs_status_t fs_scan_avx2(const fs_byte_t* data, fs_size_t data_len, const fs_byte_t* pattern, fs_size_t pattern_len, fs_size_t* out_matches, fs_size_t* match_count, fs_size_t max_matches);
fs_status_t fs_scan_sse2(const fs_byte_t* data, fs_size_t data_len, const fs_byte_t* pattern, fs_size_t pattern_len, fs_size_t* out_matches, fs_size_t* match_count, fs_size_t max_matches);
fs_status_t fs_scan_scalar(const fs_byte_t* data, fs_size_t data_len, const fs_byte_t* pattern, fs_size_t pattern_len, fs_size_t* out_matches, fs_size_t* match_count, fs_size_t max_matches);

// Multi-pattern single-pass scanner
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
);

// Native zero-syscall context extraction
fs_status_t fs_extract_context(const fs_byte_t* data, fs_size_t total_size, fs_size_t offset, fs_size_t pattern_len, fs_size_t before_bytes, fs_size_t after_bytes, const char** out_snippet, fs_size_t* out_snippet_len);

// Vectorized line & column indexer
fs_status_t fs_calculate_line_col(const fs_byte_t* data, fs_size_t total_size, fs_size_t offset, fs_size_t* out_line, fs_size_t* out_col);

#endif // FASTSCAN_SCANNER_H
