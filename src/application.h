#ifndef SRC_APPLICATION
#define SRC_APPLICATION

#include <atomic>

#include <SDL2/SDL.h>

#include "protocol/protocol_const.h"

#include "protocol/connection.h"
#include "pipe_listener.h"
#include "renderer.h"

#define TOAST_TIME 3

class Application
{
public:
    Application(/* args */);
    ~Application();

    void start(const char *title);

    // Requests a clean shutdown; safe to call from a signal handler
    void stop() { _active.store(false, std::memory_order_release); }

private:
    struct State
    {
        bool dirty = false;
        bool frameRendered = false;
        int requestFrame = 0;
        bool fullscreen = false;
        bool mouseDown = false;
        int8_t latestState = PROTOCOL_STATUS_UNKNOWN;
        uint32_t showToast = false;
        std::string toast = "";
    };

    bool setAudioDriver();
    int processKey(SDL_Keysym key);
    bool processSystemEvent(const SDL_Event &e);
    bool processFrameEvents(AtomicQueue<Message> &queue, Renderer &renderer);
    const std::string status() const;

    void loop();

    SDL_Window *_window;
    SDL_Renderer *_renderer;
    PipeListener *_keyListener;
    std::atomic<bool> _active;
    SDL_DisplayMode _displayMode;
    State _state;
    int _width;
    int _height;
    bool _debug;
};

#endif /* SRC_APPLICATION */
