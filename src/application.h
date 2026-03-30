#ifndef SRC_APPLICATION
#define SRC_APPLICATION

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
    bool _active;
    SDL_DisplayMode _displayMode;
    State _state;
    int _width;
    int _height;
    bool _debug;
};

#endif /* SRC_APPLICATION */
