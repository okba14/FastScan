#include "../include/mmap_reader.h"
#include "../include/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(FS_PLATFORM_WINDOWS)

int fs_platform_get_cpu_cores(void) {
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    return sysinfo.dwNumberOfProcessors > 0 ? (int)sysinfo.dwNumberOfProcessors : 1;
}

int fs_platform_thread_create(fs_thread_t* thread, fs_thread_func_t func, void* arg) {
    *thread = CreateThread(NULL, 0, func, arg, 0, NULL);
    return (*thread != NULL) ? 0 : -1;
}

int fs_platform_thread_join(fs_thread_t thread) {
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    return 0;
}

fs_status_t fs_mmap_open(const char* filepath, fs_region_t* region) {
    if (!filepath || !region) return FS_ERROR_NULL_PTR;

    region->data = NULL;
    region->size = 0;
    region->h_file = INVALID_HANDLE_VALUE;
    region->h_map = NULL;

    // Convert UTF-8 filepath to wide string (supports long paths and unicode)
    int wlen = MultiByteToWideChar(CP_UTF8, 0, filepath, -1, NULL, 0);
    if (wlen <= 0) return FS_ERROR_OPEN_FAILED;

    wchar_t* wpath = (wchar_t*)malloc(wlen * sizeof(wchar_t));
    if (!wpath) return FS_ERROR_OUT_OF_BOUNDS;

    MultiByteToWideChar(CP_UTF8, 0, filepath, -1, wpath, wlen);

    HANDLE h_file = CreateFileW(
        wpath,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, // Sharing permits active log readers
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        NULL
    );
    free(wpath);

    if (h_file == INVALID_HANDLE_VALUE) {
        return FS_ERROR_OPEN_FAILED;
    }

    LARGE_INTEGER file_size;
    if (!GetFileSizeEx(h_file, &file_size)) {
        CloseHandle(h_file);
        return FS_ERROR_OPEN_FAILED;
    }

    fs_size_t size = (fs_size_t)file_size.QuadPart;
    if (size == 0) {
        region->h_file = h_file;
        return FS_SUCCESS;
    }

    HANDLE h_map = CreateFileMappingW(
        h_file,
        NULL,
        PAGE_READONLY,
        0,
        0,
        NULL
    );

    if (!h_map) {
        CloseHandle(h_file);
        return FS_ERROR_MMAP_FAILED;
    }

    void* map_view = MapViewOfFile(
        h_map,
        FILE_MAP_READ,
        0,
        0,
        0
    );

    if (!map_view) {
        CloseHandle(h_map);
        CloseHandle(h_file);
        return FS_ERROR_MMAP_FAILED;
    }

    region->data = (const fs_byte_t*)map_view;
    region->size = size;
    region->h_file = h_file;
    region->h_map = h_map;

    return FS_SUCCESS;
}

void fs_mmap_close(fs_region_t* region) {
    if (!region) return;

    if (region->data) {
        UnmapViewOfFile(region->data);
        region->data = NULL;
    }

    if (region->h_map) {
        CloseHandle(region->h_map);
        region->h_map = NULL;
    }

    if (region->h_file != INVALID_HANDLE_VALUE) {
        CloseHandle(region->h_file);
        region->h_file = INVALID_HANDLE_VALUE;
    }

    region->size = 0;
}

fs_status_t fs_get_file_size(const char* filepath, fs_size_t* out_size) {
    if (!filepath || !out_size) return FS_ERROR_NULL_PTR;

    int wlen = MultiByteToWideChar(CP_UTF8, 0, filepath, -1, NULL, 0);
    if (wlen <= 0) return FS_ERROR_OPEN_FAILED;

    wchar_t* wpath = (wchar_t*)malloc(wlen * sizeof(wchar_t));
    if (!wpath) return FS_ERROR_OUT_OF_BOUNDS;
    MultiByteToWideChar(CP_UTF8, 0, filepath, -1, wpath, wlen);

    HANDLE h_file = CreateFileW(wpath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    free(wpath);

    if (h_file == INVALID_HANDLE_VALUE) return FS_ERROR_OPEN_FAILED;

    LARGE_INTEGER file_size;
    BOOL res = GetFileSizeEx(h_file, &file_size);
    CloseHandle(h_file);

    if (!res) return FS_ERROR_OPEN_FAILED;
    *out_size = (fs_size_t)file_size.QuadPart;
    return FS_SUCCESS;
}

#else // POSIX (Linux, macOS, BSD)

int fs_platform_get_cpu_cores(void) {
#if defined(FS_PLATFORM_MACOS)
    int count = 1;
    size_t size = sizeof(count);
    if (sysctlbyname("hw.logicalcpu", &count, &size, NULL, 0) == 0 && count > 0) {
        return count;
    }
    return 1;
#else
    long nproc = sysconf(_SC_NPROCESSORS_ONLN);
    return nproc > 0 ? (int)nproc : 1;
#endif
}

int fs_platform_thread_create(fs_thread_t* thread, fs_thread_func_t func, void* arg) {
    return pthread_create(thread, NULL, (void* (*)(void*))func, arg);
}

int fs_platform_thread_join(fs_thread_t thread) {
    return pthread_join(thread, NULL);
}

fs_status_t fs_mmap_open(const char* filepath, fs_region_t* region) {
    if (!filepath || !region) return FS_ERROR_NULL_PTR;

    region->data = NULL;
    region->size = 0;
    region->fd = -1;

    int fd = open(filepath, O_RDONLY);
    if (fd == -1) return FS_ERROR_OPEN_FAILED;

    struct stat st;
    if (fstat(fd, &st) != 0) {
        close(fd);
        return FS_ERROR_OPEN_FAILED;
    }

    fs_size_t size = (fs_size_t)st.st_size;
    if (size == 0) {
        region->fd = fd;
        return FS_SUCCESS;
    }

    // Safe mmap: MAP_PRIVATE without MAP_POPULATE to avoid OOM crashes on huge files (> RAM)
    int flags = MAP_PRIVATE;
    void* map = mmap(NULL, size, PROT_READ, flags, fd, 0);
    if (map == MAP_FAILED) {
        close(fd);
        return FS_ERROR_MMAP_FAILED;
    }

#if defined(MADV_SEQUENTIAL)
    madvise(map, size, MADV_SEQUENTIAL);
#endif

    region->data = (const fs_byte_t*)map;
    region->size = size;
    region->fd = fd;

    return FS_SUCCESS;
}

void fs_mmap_close(fs_region_t* region) {
    if (!region) return;

    if (region->data && region->size > 0) {
#if defined(MADV_DONTNEED)
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

fs_status_t fs_get_file_size(const char* filepath, fs_size_t* out_size) {
    if (!filepath || !out_size) return FS_ERROR_NULL_PTR;

    struct stat st;
    if (stat(filepath, &st) != 0) return FS_ERROR_OPEN_FAILED;

    *out_size = (fs_size_t)st.st_size;
    return FS_SUCCESS;
}

#endif
