#include "mmap_reader.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

fs_status_t fs_get_file_size(const char* filepath, fs_size_t* out_size) {
    if (!filepath || !out_size) return FS_ERROR_NULL_PTR;

    struct stat st;
    if (stat(filepath, &st) != 0) {
        return FS_ERROR_OPEN_FAILED;
    }

    *out_size = (fs_size_t)st.st_size;
    return FS_SUCCESS;
}

fs_status_t fs_mmap_open(const char* filepath, fs_region_t* region) {
    if (!filepath || !region) return FS_ERROR_NULL_PTR;

    // 1. Get file size
    fs_size_t size = 0;
    fs_status_t status = fs_get_file_size(filepath, &size);
    if (status != FS_SUCCESS) return status;

    if (size == 0) {
        region->data = NULL;
        region->size = 0;
        region->fd = -1;
        return FS_SUCCESS; 
    }


    int fd = open(filepath, O_RDONLY);
    if (fd == -1) {
        return FS_ERROR_OPEN_FAILED;
    }


    void* map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED) {
        close(fd);
        return FS_ERROR_MMAP_FAILED;
    }


#ifdef __linux__
    posix_madvise(map, size, POSIX_MADV_SEQUENTIAL);
#endif


    region->data = (const fs_byte_t*)map;
    region->size = size;
    region->fd = fd;

    return FS_SUCCESS;
}

void fs_mmap_close(fs_region_t* region) {
    if (!region) return;

    if (region->data && region->size > 0) {
        munmap((void*)region->data, region->size);
        region->data = NULL;
    }

    if (region->fd != -1) {
        close(region->fd);
        region->fd = -1;
    }

    region->size = 0;
}
