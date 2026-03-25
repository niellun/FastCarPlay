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
    std::ostringstream oss;

    if (data && length > 0)
    {
        for (uint32_t i = 0; (i < length) && (i < max); ++i)
        {
            oss << std::setw(4) << static_cast<uint32_t>(data[i]);
        }
    }

    return oss.str();
}

inline std::string ascii(uint8_t *data, uint32_t length)
{
    std::ostringstream oss;

    if (data && length > 0)
    {
        for (uint32_t i = 0; i < length; ++i)
        {
            char ch = static_cast<char>(data[i]);
            if (ch == '\n' || ch == '\r')
                oss << '.';
            else
                oss << (std::isprint(static_cast<unsigned char>(ch)) ? ch : '.');
        }
    }

    return oss.str();
}

inline bool jsonFindString(const uint8_t *data, int size, const char *key, char *result, int len)
{
    const char *p = reinterpret_cast<const char *>(data);
    const char *end = p + size;

    int keyLen = strlen(key);

    while (p < end)
    {
        while (p < end && *p != '"')
            ++p;
        if (p >= end)
            break;
        ++p;

        if ((end - p) > keyLen &&
            strncmp(p, key, keyLen) == 0 &&
            p[keyLen] == '"')
        {
            p += keyLen + 1; 
            while (p < end && isspace((unsigned char)*p))
                ++p;
            if (p >= end || *p != ':')
                continue;
            ++p; 
            while (p < end && isspace((unsigned char)*p))
                ++p;
            if (p >= end || *p != '"')
                continue;
            ++p;

            int i = 0;
            while (p < end && *p != '"' && i + 1 < len)
                result[i++] = *p++;
            result[i] = '\0';
            return p < end && *p == '"'; 
        }

        while (p < end && *p != '"')
        {
            if (*p == '\\')
                ++p; 
            ++p;
        }
        if (p < end)
            ++p;
    }

    return false;
}

#endif /* SRC_COMMON_FUNCTIONS */
