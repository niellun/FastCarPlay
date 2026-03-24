#ifndef SRC_HELPER_THREADING
#define SRC_HELPER_THREADING

#include <thread>

#if defined(__linux__) || defined(__APPLE__)
    #include <pthread.h>
#endif

#if defined(__linux__)
    #include <sched.h>
#elif defined(_WIN32)
    #include <windows.h>
#endif

inline void setThreadName(const char* name)
{
#if defined(__linux__)
    pthread_setname_np(pthread_self(), name);

#elif defined(__APPLE__)
    pthread_setname_np(name);

#elif defined(_WIN32)
    // Windows 10+
    using SetThreadDescriptionFunc = HRESULT(WINAPI*)(HANDLE, PCWSTR);
    static auto func = (SetThreadDescriptionFunc)
        GetProcAddress(GetModuleHandleA("Kernel32.dll"), "SetThreadDescription");

    if (func)
    {
        wchar_t wname[64];
        mbstowcs(wname, name, 63);
        wname[63] = 0;
        func(GetCurrentThread(), wname);
    }

#else
    (void)name;
#endif
}

enum class ThreadPriority
{
    Low,
    Normal,
    High,
    Realtime
};

inline bool setThreadPriority(ThreadPriority prio)
{
#if defined(__linux__)

    int policy = SCHED_OTHER;
    sched_param param{};

    switch (prio)
    {
        case ThreadPriority::Low:
        case ThreadPriority::Normal:
            policy = SCHED_OTHER;
            param.sched_priority = 0;
            break;

        case ThreadPriority::High:
            policy = SCHED_FIFO;
            param.sched_priority = 10;
            break;

        case ThreadPriority::Realtime:
            policy = SCHED_FIFO;
            param.sched_priority = 50;
            break;
    }

    return pthread_setschedparam(pthread_self(), policy, &param) == 0;

#elif defined(__APPLE__)

    qos_class_t qos = QOS_CLASS_DEFAULT;

    switch (prio)
    {
        case ThreadPriority::Low:      qos = QOS_CLASS_BACKGROUND; break;
        case ThreadPriority::Normal:   qos = QOS_CLASS_DEFAULT; break;
        case ThreadPriority::High:     qos = QOS_CLASS_USER_INITIATED; break;
        case ThreadPriority::Realtime: qos = QOS_CLASS_USER_INTERACTIVE; break;
    }

    return pthread_set_qos_class_self_np(qos, 0) == 0;

#elif defined(_WIN32)

    int winPrio = THREAD_PRIORITY_NORMAL;

    switch (prio)
    {
        case ThreadPriority::Low:      winPrio = THREAD_PRIORITY_BELOW_NORMAL; break;
        case ThreadPriority::Normal:   winPrio = THREAD_PRIORITY_NORMAL; break;
        case ThreadPriority::High:     winPrio = THREAD_PRIORITY_ABOVE_NORMAL; break;
        case ThreadPriority::Realtime: winPrio = THREAD_PRIORITY_TIME_CRITICAL; break;
    }

    return SetThreadPriority(GetCurrentThread(), winPrio);

#else

    (void)prio;
    return false;

#endif
}

#endif /* SRC_HELPER_THREADING */
