#include "pipe_listener.h"

#include <SDL2/SDL.h>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdexcept>

#include "common/logger.h"

PipeListener::PipeListener(const char *path)
    : _path(path), _active(false)
{
    if (path == nullptr)
        return;
    unlink(_path);
    if (mkfifo(_path, 0666) == -1 && errno != EEXIST)
        throw std::runtime_error(std::string("[Pipe] Failed to create FIFO ") + _path + ": " + std::strerror(errno));

    _active = true;
    _thread = std::thread(&PipeListener::loop, this);
}

PipeListener::~PipeListener()
{
    if (!_active)
        return;
    // Signal the listening thread to exit by setting the active flag first.
    _active = false;
    int tmp = open(_path, O_WRONLY | O_NONBLOCK);
    if (tmp >= 0)
    {
        write(tmp, "\0", 1);
        close(tmp);
    }
    if (_thread.joinable())
        _thread.join();
    unlink(_path);
}

void PipeListener::loop()
{
    log_i("Listening on %s", _path);
    while (_active)
    {
        int fd = open(_path, O_RDONLY);
        if (fd == -1)
        {
            log_e("Failed to open %s: %s", _path, std::strerror(errno));
            return;
        }

        char value;
        while (_active && read(fd, &value, 1) > 0)
        {
            log_d("Received: %d", static_cast<int>(value));
            if (value != 0)
            {
                SDL_Event e{};
                e.type = SDL_KEYDOWN;
                e.key.state = SDL_RELEASED;
                e.key.repeat = 0;
                e.key.keysym.sym = static_cast<SDL_Keycode>(value);
                e.key.keysym.scancode = SDL_GetScancodeFromKey(e.key.keysym.sym);
                SDL_PushEvent(&e);
            }
        }

        if (fd >= 0)
            close(fd);
    }
    log_v("Finished on %s", _path);
}
