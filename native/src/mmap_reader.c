#include "mmap_reader.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

// Linux specific headers for advanced hints
#ifdef __linux__
#include <fcntl.h> 
#endif

fs_status_t fs_mmap_open(const char* filepath, fs_region_t* region) {
    if (!filepath || !region) return FS_ERROR_NULL_PTR;

    // 1. Optimized Syscalls: Open first, then fstat
    // Reduces race conditions and saves one syscall context switch
    int fd = open(filepath, O_RDONLY);
    if (fd == -1) return FS_ERROR_OPEN_FAILED;

    struct stat st;
    if (fstat(fd, &st) != 0) {
        close(fd);
        return FS_ERROR_OPEN_FAILED;
    }

    fs_size_t size = (fs_size_t)st.st_size;

    if (size == 0) {
        region->data = NULL;
        region->size = 0;
        region->fd = fd; 
        return FS_SUCCESS;
    }

    // 2. Aggressive mmap flags
    int flags = MAP_PRIVATE;

#ifdef __linux__
    // MAP_POPULATE: Pre-fault pages. 
    // Kernel loads file into RAM immediately during mmap.
    // Eliminates page faults during scanning loop.
    flags |= MAP_POPULATE;
#endif

    void* map = mmap(NULL, size, PROT_READ, flags, fd, 0);
    
    if (map == MAP_FAILED) {
        close(fd);
        return FS_ERROR_MMAP_FAILED;
    }


#ifdef __linux__
    
    madvise(map, size, MADV_SEQUENTIAL | MADV_WILLNEED);
    

    madvise(map, size, MADV_HUGEPAGE);

    
    if (size > 0) {
        readahead(fd, 0, size);
    }
#endif

    region->data = (const fs_byte_t*)map;
    region->size = size;
    region->fd = fd;

    return FS_SUCCESS;
}

void fs_mmap_close(fs_region_t* region) {
    if (!region) return;

    if (region->data && region->size > 0) {

        #ifdef __linux__
        madvise((void*)region->data, region->size, MADV_DONTNEED);
        #endif
        
        munmap((void*)region->data, region->size);
        region->data = NULL;
    }

    if (region->fd != -1) {
        close(region->fd);
        region->fd = -1;
    }

    region->size = 0;
}
