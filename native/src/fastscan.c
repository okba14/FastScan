#include <stdlib.h>
#include <string.h>
#include <pthread.h> 
#include <unistd.h>  
#include <errno.h>

#include "fastscan.h"
#include "mmap_reader.h"
#include "scanner.h"

// Structure to hold data for each worker thread
typedef struct {
    const fs_byte_t* start;       
    fs_size_t size;               
    const fs_byte_t* pattern;     
    fs_size_t pattern_len;         
    

    fs_size_t* local_matches;
    fs_size_t local_count;
    fs_size_t max_matches;
    

    fs_size_t true_chunk_start;    
} thread_data_t;


void* worker_thread(void* arg) {
    thread_data_t* data = (thread_data_t*)arg;
    

    fs_scan_raw(data->start, data->size, data->pattern, data->pattern_len, 
                data->local_matches, &data->local_count, data->max_matches);
                
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

    
    if (total_size < (1024 * 1024)) {
        ctx->matches = (fs_size_t*)malloc(sizeof(fs_size_t) * ctx->max_matches);
        if (!ctx->matches) return FS_ERROR_OUT_OF_BOUNDS;
        
        return fs_scan_raw(ctx->region.data, total_size, pattern, pattern_len, 
                          ctx->matches, &ctx->match_count, ctx->max_matches);
    }

    
    

    long num_cores = sysconf(_SC_NPROCESSORS_ONLN);
    int num_threads = (num_cores > 0) ? (int)num_cores : 4; 
    
    fs_size_t chunk_size = total_size / num_threads;
    
    pthread_t threads[num_threads];
    thread_data_t t_data[num_threads];
    
    
    fs_size_t per_thread_max = (ctx->max_matches / num_threads) + 1000; 
    
    const fs_byte_t* global_start = ctx->region.data;
    

    int threads_created = 0;

    
    for (int i = 0; i < num_threads; i++) {

        fs_size_t logical_start = i * chunk_size;
        fs_size_t logical_end = (i == num_threads - 1) ? total_size : (i + 1) * chunk_size;
        
        t_data[i].pattern = pattern;
        t_data[i].pattern_len = pattern_len;
        t_data[i].true_chunk_start = logical_start; 
        
        
        
        if (i == 0) {
            t_data[i].start = global_start;
            t_data[i].size = logical_end + (pattern_len - 1); 
        } else if (i == num_threads - 1) {

            t_data[i].start = global_start + (logical_start - (pattern_len - 1)); 
            t_data[i].size = total_size - (logical_start - (pattern_len - 1));
        } else {

            t_data[i].start = global_start + (logical_start - (pattern_len - 1));
            t_data[i].size = (logical_end - logical_start) + (pattern_len - 1) + (pattern_len - 1);
        }
        

        if ((t_data[i].start - global_start) + t_data[i].size > total_size) {
             t_data[i].size = total_size - (t_data[i].start - global_start);
        }


        t_data[i].local_matches = (fs_size_t*)malloc(sizeof(fs_size_t) * per_thread_max);
        t_data[i].local_count = 0;
        t_data[i].max_matches = per_thread_max;
        
        if (!t_data[i].local_matches) {

             for(int k=0; k<threads_created; k++) {
                 pthread_join(threads[k], NULL);
                 free(t_data[k].local_matches);
             }
             return FS_ERROR_OUT_OF_BOUNDS;
        }
        
        // Create the thread
        if (pthread_create(&threads[i], NULL, worker_thread, &t_data[i]) != 0) {

             for(int k=0; k<threads_created; k++) {
                 pthread_join(threads[k], NULL);
                 free(t_data[k].local_matches);
             }
             free(t_data[i].local_matches); 
             return FS_ERROR_OPEN_FAILED; 
        }
        threads_created++;
    }

    
    fs_size_t total_found = 0;
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
        total_found += t_data[i].local_count;
    }

    
    fs_size_t final_count = (total_found > ctx->max_matches) ? ctx->max_matches : total_found;

    // Allocate final buffer
    ctx->matches = (fs_size_t*)malloc(sizeof(fs_size_t) * final_count);
    if (!ctx->matches) {
        // Cleanup
        for(int i=0; i<num_threads; i++) free(t_data[i].local_matches);
        return FS_ERROR_OUT_OF_BOUNDS;
    }
    
    ctx->match_count = final_count;
    
    
    fs_size_t current_offset = 0;
    for (int i = 0; i < num_threads; i++) {
        
        
        
        for (fs_size_t j = 0; j < t_data[i].local_count; j++) {
            

            fs_size_t abs_offset = t_data[i].local_matches[j] + (t_data[i].start - global_start);
            
            
          
            
            if (abs_offset >= t_data[i].true_chunk_start) {
                ctx->matches[current_offset++] = abs_offset;
                

                if (current_offset >= final_count) break;
            }
        }
        

        if (current_offset >= final_count) break;
        
        free(t_data[i].local_matches); 
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
