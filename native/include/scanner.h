#ifndef FASTSCAN_SCANNER_H
#define FASTSCAN_SCANNER_H

#include "fastscan.h"
#include "safe_types.h"

fs_status_t fs_scan_run(fastscan_ctx_t* ctx);
fs_status_t fs_scan_raw(const fs_byte_t* data, fs_size_t data_len, const fs_byte_t* pattern, fs_size_t pattern_len, fs_size_t* out_matches, fs_size_t* match_count, fs_size_t max_matches);

#endif
