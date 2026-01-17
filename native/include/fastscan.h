#ifndef FASTSCAN_FASTSCAN_H
#define FASTSCAN_FASTSCAN_H

#include "safe_types.h"
#include "config.h"


typedef struct {
    const fs_byte_t* data; 
    fs_size_t size;        
    int fd;                
} fs_region_t;


typedef struct {
    // Input
    const char* pattern;
    fs_size_t pattern_len;


    fs_region_t region;

    
    fs_size_t* matches;
    fs_size_t match_count;
    fs_size_t max_matches;


    int is_initialized;
} fastscan_ctx_t;


fs_status_t fastscan_init(fastscan_ctx_t* ctx, const char* pattern, fs_size_t max_results);
fs_status_t fastscan_load_file(fastscan_ctx_t* ctx, const char* filepath);
fs_status_t fastscan_execute(fastscan_ctx_t* ctx);
void fastscan_destroy(fastscan_ctx_t* ctx);

#endif // FASTSCAN_FASTSCAN_H
