#ifndef SRC_HELPER_FUNCTIONS
#define SRC_HELPER_FUNCTIONS

#if defined(__linux__) || defined(__APPLE__)
#include <pthread.h>
#endif

inline void setThreadName(const char *name)
{
#if defined(__linux__)
    pthread_setname_np(pthread_self(), name); // Linux: OK (limit 16 chars including null)
#elif defined(__APPLE__)
    pthread_setname_np(name); // macOS: only current thread, OK
#else
    (void)name; // suppress unused warning
#endif
}

inline void disable_cout()
{
    std::cout.setstate(std::ios_base::failbit);
}

inline void write_uint32_le(uint8_t *dst, uint32_t value)
{
    dst[0] = value & 0xFF;
    dst[1] = (value >> 8) & 0xFF;
    dst[2] = (value >> 16) & 0xFF;
    dst[3] = (value >> 24) & 0xFF;
}

inline void execute(const char* path) {
    if (!path || *path == '\0') {
        throw std::invalid_argument("Program path cannot be empty");
    }

    std::system(path);
}

#endif /* SRC_HELPER_FUNCTIONS */
