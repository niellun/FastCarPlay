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

#endif /* SRC_HELPER_FUNCTIONS */