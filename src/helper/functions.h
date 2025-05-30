#ifndef SRC_HELPER_FUNCTIONS
#define SRC_HELPER_FUNCTIONS

#include <iostream>
#include <SDL2/SDL.h>

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

inline void execute(const char *path)
{
    if (!path || *path == '\0')
    {
        throw std::invalid_argument("Program path cannot be empty");
    }

    std::system(path);
}

inline const std::string avErrorText(int code)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
    if (av_strerror(code, buf, sizeof(buf)) == 0)
        return buf;
    return "Unknown error";
}

inline void pushEvent(Uint32 evt, int code)
{
    if (evt == (Uint32)-1)
        return;
    SDL_Event event;
    SDL_memset(&event, 0, sizeof(event));
    event.type = evt;
    event.user.type = evt;
    event.user.code = code;
    SDL_PushEvent(&event);
}

#endif /* SRC_HELPER_FUNCTIONS */
