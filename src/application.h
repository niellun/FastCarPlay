#ifndef SRC_APPLICATION
#define SRC_APPLICATION

#include <SDL2/SDL.h>

#include "protocol/protocol_const.h"

#include "connector.h"
#include "renderer.h"

#define REDRAW_REQUEST 5

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
        bool showVideo = true;
        bool fullscreen = false;
        bool mouseDown = false;
        int8_t previousdeviceStatus = PROTOCOL_STATUS_INITIALISING;        
        atomic<int8_t> deviceStatus = PROTOCOL_STATUS_INITIALISING;
    };

    bool setAudioDriver();
    int processKey(SDL_Keysym key);
    bool processSystemEvent(const SDL_Event &e);
    bool processFrameEvents(AtomicQueue<Message> &queue, Renderer &renderer);

    void loop();

    SDL_Window *_window;
    SDL_Renderer *_renderer;
    bool _active;
    SDL_DisplayMode _displayMode;
    State _state;
    int _width;
    int _height;
};

#endif /* SRC_APPLICATION */
