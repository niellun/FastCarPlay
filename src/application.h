#ifndef SRC_APPLICATION
#define SRC_APPLICATION

#include <SDL2/SDL.h>

#include "helper/protocol_const.h"

#include "protocol.h"
#include "renderer.h"

class Application
{
public:
    Application(/* args */);
    ~Application();

    void start(const char *title);

private:
    struct State
    {
        bool connected = false;
        bool dirty = false;
        bool frameRendered = false;
        int requestFrame = 0;
        bool showVideo = true;
        bool fullscreen = false;
        bool mouseDown = false;
        int8_t deviceStatus = PROTOCOL_STATUS_INITIALISING;
        bool flushBuffers = false;
    };

    bool setAudioDriver();
    int processKey(SDL_Keysym key);
    bool processSystemEvent(const SDL_Event &e);
    bool processFrameEvents(Protocol &protocol, Renderer &renderer);

    void loop();

    SDL_Window *_window;
    SDL_Renderer *_renderer;
    bool _active;
    Uint32 _evtBase;
    SDL_DisplayMode _displayMode;
    State _state;
    int _width;
    int _height;
};

#endif /* SRC_APPLICATION */
