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

#include <sstream>
#include <iomanip>

inline std::string bytes(uint8_t *data, uint32_t length, uint16_t max)
{
    std::ostringstream out;

    if (data && length >= 4)
    {
        for (size_t i = 0; (i < length) && (i < max); ++i)
        {
            out << std::setw(4) << static_cast<uint32_t>(data[i]);
        }
    }

    return out.str();
}

#endif /* SRC_COMMON_FUNCTIONS */
