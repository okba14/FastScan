#ifndef FASTSCAN_SAFE_TYPES_H
#define FASTSCAN_SAFE_TYPES_H

#include <stddef.h>
#include <stdint.h>


typedef uint8_t  fs_byte_t;
typedef uint32_t fs_word_t;
typedef uint64_t fs_dword_t;
typedef size_t   fs_size_t;


typedef enum {
    FS_SUCCESS = 0,
    FS_ERROR_NULL_PTR,
    FS_ERROR_INVALID_ARG,
    FS_ERROR_OUT_OF_BOUNDS,
    FS_ERROR_MMAP_FAILED,
    FS_ERROR_OPEN_FAILED
} fs_status_t;

#endif // FASTSCAN_SAFE_TYPES_H
