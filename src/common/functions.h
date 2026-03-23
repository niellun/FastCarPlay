#ifndef SRC_COMMON_FUNCTIONS
#define SRC_COMMON_FUNCTIONS

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

#include <SDL2/SDL.h>

#include "common/threading.h"

extern "C"
{
#include <libavutil/error.h>
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

#endif /* SRC_COMMON_FUNCTIONS */
