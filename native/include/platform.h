#ifndef FASTSCAN_PLATFORM_H
#define FASTSCAN_PLATFORM_H

#include "safe_types.h"

#if defined(_WIN32) || defined(_WIN64)
    #define FS_PLATFORM_WINDOWS 1
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #include <io.h>
#elif defined(__APPLE__)
    #define FS_PLATFORM_MACOS 1
    #define FS_PLATFORM_POSIX 1
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <sys/types.h>
    #include <sys/sysctl.h>
    #include <fcntl.h>
    #include <unistd.h>
    #include <pthread.h>
    #include <signal.h>
    #include <setjmp.h>
#elif defined(__linux__)
    #define FS_PLATFORM_LINUX 1
    #define FS_PLATFORM_POSIX 1
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <sys/types.h>
    #include <fcntl.h>
    #include <unistd.h>
    #include <pthread.h>
    #include <signal.h>
    #include <setjmp.h>
#else
    #define FS_PLATFORM_POSIX 1
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <fcntl.h>
    #include <unistd.h>
    #include <pthread.h>
#endif

// Thread Abstraction
#if defined(FS_PLATFORM_WINDOWS)
    typedef HANDLE fs_thread_t;
    typedef DWORD (WINAPI *fs_thread_func_t)(LPVOID);
#else
    typedef pthread_t fs_thread_t;
    typedef void* (*fs_thread_func_t)(void*);
#endif

// Memory Region across platforms
typedef struct {
    const fs_byte_t* data;
    fs_size_t size;
#if defined(FS_PLATFORM_WINDOWS)
    HANDLE h_file;
    HANDLE h_map;
#else
    int fd;
#endif
} fs_region_t;

// Platform helper prototypes
int fs_platform_get_cpu_cores(void);
fs_status_t fs_platform_map_file(const char* filepath, fs_region_t* region);
void fs_platform_unmap_file(fs_region_t* region);

int fs_platform_thread_create(fs_thread_t* thread, fs_thread_func_t func, void* arg);
int fs_platform_thread_join(fs_thread_t thread);

#endif // FASTSCAN_PLATFORM_H
