#include "pipe_listener.h"

#include <SDL2/SDL.h>

#include <cerrno>
#include <fcntl.h>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>

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
    std::cout << "[Pipe] Listening on " << _path << std::endl;
    while (_active)
    {
        int fd = open(_path, O_RDONLY);
        if (fd == -1)
        {
            std::cout << "[Pipe] Failed to open " << _path << ": " << std::strerror(errno) << std::endl;
            return;
        }

        char value;
        while (_active && read(fd, &value, 1) > 0)
        {
            std::cout << "[Pipe] Received: " << (int)value << std::endl;
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
    std::cout << "[Pipe] Finished on " << _path << std::endl;
}
