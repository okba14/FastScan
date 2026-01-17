#ifndef FASTSCAN_MMAP_READER_H
#define FASTSCAN_MMAP_READER_H

#include "fastscan.h"


fs_status_t fs_mmap_open(const char* filepath, fs_region_t* region);


void fs_mmap_close(fs_region_t* region);


fs_status_t fs_get_file_size(const char* filepath, fs_size_t* out_size);

#endif // FASTSCAN_MMAP_READER_H
