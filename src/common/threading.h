#ifndef SRC_HELPER_THREADING
#define SRC_HELPER_THREADING

#if defined(__linux__) || defined(__APPLE__)
#include <pthread.h>
#endif

extern "C"
{
#include <libavutil/error.h>
}

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

#endif /* SRC_HELPER_THREADING */
