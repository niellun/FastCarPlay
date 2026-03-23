#include "application.h"

#include <SDL2/SDL_ttf.h>
#include <cstdio>

#include "struct/video_buffer.h"
#include "common/logger.h"

#include "settings.h"
#include "interface.h"
#include "decoder.h"
#include "pcm_audio.h"

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
    &Settings::keyNavRelease};

static constexpr size_t keyMapSize = sizeof(keyMap) / sizeof(keyMap[0]);

Application::Application(/* args */) : _window(nullptr),
                                       _renderer(nullptr),
                                       _active(true)
{
    log_v("Creating");

    _debug = Settings::debugOverlay;

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

    log_i("SDL screen %dx%d@%d, audio driver %s", _displayMode.w, _displayMode.h, _displayMode.refresh_rate, SDL_GetCurrentAudioDriver());
}

Application::~Application()
{
    log_v("Destroying");
    if (_renderer != nullptr)
        SDL_DestroyRenderer(_renderer);
    if (_window != nullptr)
        SDL_DestroyWindow(_window);
    TTF_Quit();
    SDL_Quit();
    log_d("Finished");
}

void Application::start(const char *title)
{
    log_d("Initialising");

    // Create SDL window centered on screen
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, Settings::fastScale ? "nearest" : "best");

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

    SDL_SetHint(SDL_HINT_RENDER_DRIVER, Settings::renderDriver.value.c_str());
    _renderer = SDL_CreateRenderer(_window, -1, flags);

    if (!_renderer)
        throw std::runtime_error(std::string("SDL can't create renderer > ") + SDL_GetError());

    SDL_RendererInfo rendererInfo{};
    if (SDL_GetRendererInfo(_renderer, &rendererInfo) == 0)
    {
        log_i("Renderer %s (%s, %s)", rendererInfo.name,
              ((rendererInfo.flags & SDL_RENDERER_ACCELERATED) ? "accelerated" : "software"),
              ((rendererInfo.flags & SDL_RENDERER_PRESENTVSYNC) ? "vsync" : "no-vsync"));
    }

    log_v("Starting");
    loop();
    log_v("Stopped");
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
    log_w("Unmapped key %d", key.sym);
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
        case SDLK_d:
        {
            _debug = !_debug;
            return true;
        }
        }
    }

    return false;
}

bool Application::processFrameEvents(AtomicQueue<Message> &queue, Renderer &renderer)
{
    bool result = false;
    SDL_Event e;
    bool motion = false;
    int motionX = 0;
    int motionY = 0;
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
            motion = true;
            break;
        }
        case SDL_KEYDOWN:
        {
            int key = processKey(e.key.keysym);
            if (key > 0)
            {
                queue.pushDiscard(Message::Control(key));
                result = true;
            }
            break;
        }
        case SDL_KEYUP:
        {
            if (e.key.keysym.sym == Settings::keyEnter)
            {
                queue.pushDiscard(Message::Control(Settings::keyEnterUp.key));
                result = true;
            }
            break;
        }
        }
    }

    if (_state.frameRendered && (downX >= 0 || upX >= 0 || motion))
    {
        if (downX >= 0)
            queue.pushDiscard(Message::Click(renderer.xScale * downX / _width, renderer.yScale * downY / _height, true));
        if (motion)
            queue.pushDiscard(Message::Move(renderer.xScale * motionX / _width, renderer.yScale * motionY / _height));
        if (upX >= 0)
            queue.pushDiscard(Message::Click(renderer.xScale * upX / _width, renderer.yScale * upY / _height, false));
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

    Connection protocol;
    Decoder decoder;
    PcmAudio audioMain("main"), audioAux("aux");

    decoder.start(&protocol.videoStream, AV_CODEC_ID_H264);
    audioMain.start(&protocol.audioStreamMain);
    audioAux.start(&protocol.audioStreamAux, &audioMain);
    protocol.start(&_state.deviceStatus);

    log_v("Loop");
    Uint32 frameStart = SDL_GetTicks();
    AVFrame *frame = nullptr;
    uint32_t frameid = 0;
    uint32_t latestFrameid = 0;
    uint32_t frameTargetTime = Settings::fps > 0 ? 1000 / Settings::fps : 1000;
    uint32_t delay = 0;
    uint32_t dropframes = 0;
    int skipEvents = 0;
    int frameTime = 0;
    while (_active)
    {
        bool late = false;

        if (_state.deviceStatus != _state.previousdeviceStatus)
        {
            // On connect/disconnect
            if (_state.previousdeviceStatus == PROTOCOL_STATUS_CONNECTED || _state.deviceStatus == PROTOCOL_STATUS_CONNECTED)
            {
                _state.frameRendered = false;
                _state.dirty = true;
                _state.requestFrame = 0;
            }
            // On connect
            if (_state.deviceStatus == PROTOCOL_STATUS_CONNECTED)
            {
                decoder.flush();
                decoder.buffer.reset();
            }
            _state.previousdeviceStatus = _state.deviceStatus;
        }

        if (_state.deviceStatus == PROTOCOL_STATUS_CONNECTED && _state.showVideo)
        {
            delay = 0;
            while (!_state.dirty && decoder.buffer.latestId() == latestFrameid && ++delay < frameTargetTime)
            {
                SDL_Delay(1);
            }

            if (decoder.buffer.consume(&frame, &frameid))
            {
                bool newFrame = frameid != latestFrameid;
                if (newFrame || _state.dirty)
                {
                    if (interface.render(frame))
                    {
                        _state.frameRendered = true;
                        _state.dirty = false;
                        if (latestFrameid > 0 && frameid - latestFrameid > 1)
                        {
                            dropframes += frameid - latestFrameid - 1;
                            log_d("Frame drop %d on %d total %d", frameid - latestFrameid - 1, frameid, dropframes);
                        }
                        latestFrameid = frameid;
                    }
                }
            }

            if (_state.requestFrame > 0 && Settings::forceRedraw > 0)
            {
                if (++_state.requestFrame % Settings::forceRedraw == 0)
                {
                    log_d("Request screen update");
                    protocol.send(Message::Control(BTN_SCREEN_REFRESH));
                    if (_state.requestFrame >= Settings::forceRedraw * REDRAW_REQUEST)
                        _state.requestFrame = 0;
                }
            }
        }

        if (!_state.frameRendered || !_state.showVideo)
        {
            interface.drawHome(_state.dirty, _state.deviceStatus);
            _state.dirty = false;
            SDL_Event e;
            while (SDL_PollEvent(&e))
                processSystemEvent(e);
        }
        else
        {
            late = decoder.buffer.latestId() - latestFrameid > 1;
            if(!late || ++skipEvents > Settings::eventsSkip)
            {
                if (processFrameEvents(protocol.writeQueue, interface) && Settings::forceRedraw > 0)
                {
                    _state.requestFrame = 1;
                }
                skipEvents = 0;
            }
        }

        if (_debug)
        {
            char debugBuffer[256];
            std::snprintf(debugBuffer, sizeof(debugBuffer),
                          "FRAME: %u / %u [%d] droped %d\n"
                          "TIME: %d delay %d\n"
                          "VIDEO: %u\n"
                          "AUDIO-MAIN: %u\n"
                          "AUDIO-AUX: %u\n"
                          "OUT: %u",
                          latestFrameid,
                          decoder.buffer.latestId(),
                          decoder.buffer.latestId() - latestFrameid, dropframes,
                          frameTime, delay,
                          protocol.videoStream.count(),
                          protocol.audioStreamMain.count(),
                          protocol.audioStreamAux.count(),
                          protocol.writeQueue.count());
            interface.debug(debugBuffer);
        }

        if (_active && !Settings::vsync)
        {
            Uint32 frameEnd = SDL_GetTicks();
            frameTime = frameEnd - frameStart;
            int frameDelay = frameTargetTime - frameTime;
            if (frameDelay <= 0 || decoder.buffer.latestId() - latestFrameid > 1)
            {
                frameStart = frameEnd;
            }
            else
            {
                SDL_Delay(frameDelay);
                frameStart += frameDelay;
            }
        }
    }

    if (!Settings::isHeadless())
        SDL_HideWindow(_window);
}
