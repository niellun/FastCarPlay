#include "application.h"

#include <SDL2/SDL_ttf.h>

#include "struct/video_buffer.h"

#include "settings.h"
#include "interface.h"
#include "decoder.h"
#include "pcm_audio.h"
#include "pipe_listener.h"

#define EVT_STATUS_OFFSET 0
#define EVT_PHONE_OFFSET 1

static KeySetting<int> *keyMap[] = {
    &Settings::keySiri,
    &Settings::keyNightOn,
    &Settings::keyNightOff,
    &Settings::keyLeft,
    &Settings::keyRight,
    &Settings::keyEnter,
    &Settings::keyEnterUp,
    &Settings::keyBack,
    &Settings::keyUp,
    &Settings::keyDown,
    &Settings::keyHome,
    &Settings::keyPlay,
    &Settings::keyPause,
    &Settings::keyPlayPause,
    &Settings::keyNext,
    &Settings::keyPrev,
    &Settings::keyAccept,
    &Settings::keyReject,
    &Settings::keyVideoFocus,
    &Settings::keyVideoRelease,
    &Settings::keyNavFocus,
    &Settings::keyNavRelease
};

static constexpr size_t keyMapSize = sizeof(keyMap) / sizeof(keyMap[0]);

Application::Application(/* args */) : _window(nullptr),
                                       _renderer(nullptr),
                                       _active(true)
{
    std::cout << "[App] Creating" << std::endl;

    if (!setAudioDriver())
        throw std::runtime_error("Unsupported audio driver " + std::string(Settings::audioDriver.value));

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO) != 0)
        throw std::runtime_error(std::string("SDL initialisation failed > ") + SDL_GetError());

    if (TTF_Init() != 0)
    {
        SDL_Quit();
        throw std::runtime_error(std::string("TTF initialisation failed > ") + TTF_GetError());
    }

    if (SDL_GetCurrentDisplayMode(0, &_displayMode) != 0)
    {
        TTF_Quit();
        SDL_Quit();
        throw std::runtime_error(std::string("SDL get display mode failed > ") + SDL_GetError());
    }

    std::cout << "[App] SDL screen: "
              << _displayMode.w << "x" << _displayMode.h << "@" << _displayMode.refresh_rate
              << ", audio: " << SDL_GetCurrentAudioDriver() << std::endl;
}

Application::~Application()
{
    std::cout << "[App] Destroying" << std::endl;
    if (_renderer != nullptr)
        SDL_DestroyRenderer(_renderer);
    if (_window != nullptr)
        SDL_DestroyWindow(_window);
    TTF_Quit();
    SDL_Quit();
    std::cout << "[App] Finished" << std::endl;
}

void Application::start(const char *title)
{
    std::cout << "[App] Initialising" << std::endl;

    // Create SDL window centered on screen
    if (Settings::fastScale)
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    else
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best");

    // Prepare window, show it in headless to avoid blinking, otherwise hidden untill iniailised
    bool fullsize = Settings::isFullscreen() || Settings::isHeadless();
    _width = fullsize ? _displayMode.w : Settings::width;
    _height = fullsize ? _displayMode.h : Settings::height;
    _window = SDL_CreateWindow(title,
                               SDL_WINDOWPOS_CENTERED,
                               SDL_WINDOWPOS_CENTERED,
                               _width,
                               _height,
                               SDL_WINDOW_RESIZABLE | (Settings::isHeadless() ? 0 : SDL_WINDOW_HIDDEN));

    if (!_window)
        throw std::runtime_error(std::string("SDL can't create window > ") + SDL_GetError());

    if (!Settings::cursor)
        SDL_ShowCursor(SDL_DISABLE);

    // Create accelerated renderer for the window
    Uint32 flags = SDL_RENDERER_ACCELERATED;
    if (Settings::vsync)
        flags |= SDL_RENDERER_PRESENTVSYNC;
    _renderer = SDL_CreateRenderer(_window, -1, flags);
    if (!_renderer)
        throw std::runtime_error(std::string("SDL can't create renderer > ") + SDL_GetError());

    // Register additional events
    _evtBase = SDL_RegisterEvents(2);
    if (_evtBase == (Uint32)-1)
        throw std::runtime_error(std::string("Can't register custom events > ") + SDL_GetError());

    std::cout << "[App] Starting" << std::endl;
    loop();
    std::cout << "[App] Stopped" << std::endl;
}

bool Application::setAudioDriver()
{
    if (Settings::audioDriver.value.length() < 2)
        return true;

    for (int i = 0; i < SDL_GetNumAudioDrivers(); ++i)
    {
        if (SDL_GetAudioDriver(i) == Settings::audioDriver.value)
        {
            SDL_setenv("SDL_AUDIODRIVER", Settings::audioDriver.value.c_str(), 1);
            return true;
        }
    }
    return false;
}

int Application::processKey(SDL_Keysym key)
{
    for (uint8_t i = 0; i < keyMapSize; i++)
    {
        if (keyMap[i]->value == key.sym)
        {
            return keyMap[i]->key;
        }
    }
    std::cout << "[App] Unmapped key " << key.sym << std::endl;
    return 0;
}

bool Application::processSystemEvent(const SDL_Event &e)
{
    if (e.type == SDL_QUIT)
    {
        _active = false;
        return true;
    }

    if (e.type == SDL_WINDOWEVENT)
    {
        if (e.window.event == SDL_WINDOWEVENT_RESIZED)
        {
            SDL_GetWindowSize(_window, &_width, &_height);
            _state.dirty = true;
        }
        return true;
    }

    if (e.type == SDL_KEYDOWN)
    {
        switch (e.key.keysym.sym)
        {
        case SDLK_f:
        {
            if (Settings::isHeadless())
                return true;
            _state.fullscreen = !_state.fullscreen; // Toggle fullscreen mode
            SDL_SetWindowFullscreen(_window, _state.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
            SDL_SetWindowBordered(_window, _state.fullscreen ? SDL_FALSE : SDL_TRUE);
            return true;
        }
        case SDLK_q:
        {
            _active = false;
            return true;
        }
        }
    }

    if (e.type == (_evtBase + EVT_STATUS_OFFSET))
    {
        _state.deviceStatus = e.user.code;
        return true;
    }

    if (e.type == (_evtBase + EVT_PHONE_OFFSET))
    {
        _state.connected = e.user.code != 0;
        _state.frameRendered = false;
        _state.dirty = true;
        _state.requestFrame = -1;
        _state.flushBuffers = _state.connected;
        return true;
    }

    return false;
}

bool Application::processFrameEvents(Protocol &protocol, Renderer &renderer)
{
    bool result = false;
    SDL_Event e;
    int motionX = -1;
    int motionY = -1;
    int downX = -1;
    int downY = -1;
    int upX = -1;
    int upY = -1;

    while (SDL_PollEvent(&e))
    {
        if (processSystemEvent(e))
            continue;

        switch (e.type)
        {

        case SDL_MOUSEBUTTONDOWN:
        {
            _state.mouseDown = true;
            downX = e.button.x;
            downY = e.button.y;
            break;
        }

        case SDL_MOUSEBUTTONUP:
        {
            _state.mouseDown = false;
            upX = e.button.x;
            upY = e.button.y;
            result = true;
            break;
        }
        case SDL_MOUSEMOTION:
        {
            if (!_state.mouseDown)
                break;
            motionX = e.motion.x;
            motionY = e.motion.y;
            break;
        }
        case SDL_KEYDOWN:
        {
            int key = processKey(e.key.keysym);
            if (key > 0)
            {
                protocol.sendKey(key);
                result = true;
            }
            break;
        }
        case SDL_KEYUP:
        {
            if (e.key.keysym.sym == Settings::keyEnter)
            {
                protocol.sendKey(Settings::keyEnterUp.key);
                result = true;
            }
            break;
        }
        }
    }

    if (_state.frameRendered && (downX >= 0 || upX >= 0 || motionX >= 0))
    {
        if (downX >= 0)
            protocol.sendClick(renderer.xScale * downX / _width, renderer.yScale * downY / _height, true);
        if (motionX >= 0)
            protocol.sendMove(renderer.xScale * motionX / _width, renderer.yScale * motionY / _height);
        if (upX >= 0)
            protocol.sendClick(renderer.xScale * upX / _width, renderer.yScale * upY / _height, false);
    }

    return result;
}

void Application::loop()
{
    // Prepare home screen
    Interface interface(_renderer);
    interface.drawHome(true, PROTOCOL_STATUS_UNKNOWN);

    // Process full screen, do not do this in headless to avoid blinking
    if (Settings::isFullscreen())
    {
        _state.fullscreen = true;
        SDL_SetWindowFullscreen(_window, SDL_WINDOW_FULLSCREEN);
        SDL_SetWindowBordered(_window, SDL_FALSE);
    }

    // Show window, do not do this in headless to avoid blinking
    if (!Settings::isHeadless())
        SDL_ShowWindow(_window);
    interface.drawHome(true, PROTOCOL_STATUS_UNKNOWN);

    VideoBuffer videoBuffer;
    Protocol protocol(Settings::width, Settings::height, Settings::sourceFps, AV_INPUT_BUFFER_PADDING_SIZE);
    Decoder decoder;
    PcmAudio audioMain("Main"), audioAux("Aux");

    decoder.start(&protocol.videoData, &videoBuffer, AV_CODEC_ID_H264);
    audioMain.start(&protocol.audioStreamMain);
    audioAux.start(&protocol.audioStreamAux, &audioMain);
    protocol.start(_evtBase + EVT_STATUS_OFFSET, _evtBase + EVT_PHONE_OFFSET);

    std::cout << "[App] Loop" << std::endl;
    Uint32 frameStart = SDL_GetTicks();
    AVFrame *frame = nullptr;
    uint32_t frameid = 0;
    uint32_t latestFrameid = 0;
    uint32_t frameTargetTime = 1000 / Settings::fps;
    int frameDelay = 0;
    while (_active)
    {
        if (_state.connected && _state.showVideo)
        {
            if (videoBuffer.latest(&frame, &frameid) && frame && (frameid != latestFrameid || _state.dirty))
            {
                if (interface.render(frame))
                {
                    _state.frameRendered = true;
                    if (!_state.dirty && (frameid != latestFrameid + 1))
                        std::cout << "[App] Frame drop " << frameid - latestFrameid - 1 << " on " << frameid << std::endl;
                    latestFrameid = frameid;
                    _state.dirty = false;
                }
                videoBuffer.consume();
            }

            if (_state.requestFrame > 0 && Settings::forceRedraw > 0)
            {
                if (_state.requestFrame++ >= Settings::forceRedraw)
                {
                    protocol.requestKeyframe();
                    _state.requestFrame = -1;
                }
            }
        }

        if (!_state.frameRendered || !_state.showVideo)
        {
            interface.drawHome(_state.dirty, _state.connected ? PROTOCOL_STATUS_CONNECTED : _state.deviceStatus);
            _state.dirty = false;
            SDL_Event e;
            while (SDL_PollEvent(&e))
                processSystemEvent(e);
        }
        else
        {
            if (processFrameEvents(protocol, interface) && Settings::forceRedraw > 0)
                _state.requestFrame = 1;
        }

        if (_state.flushBuffers)
        {
            _state.flushBuffers = false;
            decoder.flush();
            videoBuffer.reset();
        }

        if (_active)
        {
            Uint32 frameEnd = SDL_GetTicks();
            frameDelay = frameTargetTime - (frameEnd - frameStart);
            SDL_Delay(frameDelay > 0 ? frameDelay : 1);
            frameStart += frameTargetTime;
        }
    }

    if (!Settings::isHeadless())
        SDL_HideWindow(_window);
}