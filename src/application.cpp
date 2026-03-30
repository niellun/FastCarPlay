#include "application.h"

#include <SDL2/SDL_ttf.h>
#include <cstdio>
#include <sstream>
#include <chrono>
#include <thread>

#include "struct/video_buffer.h"
#include "common/logger.h"

#include "settings.h"
#include "interface.h"
#include "decoder.h"
#include "pcm_audio.h"
#include "common/functions.h"

static KeySetting<int> *keyMap[] = {
    &Settings::keySiri,
    &Settings::keyNightOn,
    &Settings::keyNightOff,
    &Settings::keyLeft,
    &Settings::keyLeftExtra,
    &Settings::keyRight,
    &Settings::keyRightExtra,
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
                                       _keyListener(nullptr),
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
    if (_keyListener != nullptr)
    {
        delete _keyListener;
        _keyListener = nullptr;
    }
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
#ifndef NDEBUG
        case SDLK_d:
        {
            _debug = !_debug;
            return true;
        }
#endif
        case SDLK_r:
        {
            _state.dirty = true;
            _state.requestFrame = 1;
        }
        }
        bool script = false;
        std::string name = "";
        std::string scriptPath = "";
        if (e.key.keysym.sym == Settings::scriptKey1)
        {
            script = true;
            name = Settings::scriptName1;
            scriptPath = Settings::script1;
        }
        if (e.key.keysym.sym == Settings::scriptKey2)
        {
            script = true;
            name = Settings::scriptName2;
            scriptPath = Settings::script2;
        }
        if (e.key.keysym.sym == Settings::scriptKey3)
        {
            script = true;
            name = Settings::scriptName3;
            scriptPath = Settings::script3;
        }
        if (e.key.keysym.sym == Settings::scriptKey4)
        {
            script = true;
            name = Settings::scriptName4;
            scriptPath = Settings::script4;
        }

        if (script)
        {
            if (name.length() > 0)
            {
                _state.toast = name;
                _state.showToast = 1;
            }

            if (scriptPath.length() > 1)
            {
                execute(scriptPath.c_str());
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
    interface.drawHome(true, PROTOCOL_STATUS_UNKNOWN, "");

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
    interface.drawHome(true, PROTOCOL_STATUS_UNKNOWN, "");

    Connection protocol;
    Decoder decoder;
    PcmAudio audioMain("main"), audioAux("aux");

    if (Settings::keyPipe.value.length() > 2)
        _keyListener = new PipeListener(Settings::keyPipe.value.c_str());

    decoder.start(&protocol.videoStream, AV_CODEC_ID_H264);
    audioMain.start(&protocol.audioStreamMain);
    audioAux.start(&protocol.audioStreamAux, &audioMain);
    protocol.start();

    log_v("Loop");
    std::chrono::steady_clock::time_point frameStart = std::chrono::steady_clock::now();
    int32_t frameTime = 0;
    int32_t frameDelay = 0;
    const int32_t frameTarget = Settings::sourceFps > 0 ? 1000000 / Settings::sourceFps : 1000000;
    AVFrame *frame = nullptr;
    uint32_t frameId = 0;
    uint32_t dropframes = 0;
    int skipEvents = 0;
#ifndef NDEBUG
    Uint32 debugLast = SDL_GetTicks();
    int debugSpeed = 0;
    int debugLastCount = 0;
#endif
    while (_active)
    {
        bool newFrame = false;

        if (_state.showToast > 0)
        {
            if (_state.showToast == 1)
            {
                interface.showToast(_state.toast);
                _state.showToast = SDL_GetTicks();
                _state.dirty = true;
            }

            if (SDL_GetTicks() - _state.showToast >= TOAST_TIME * 1000)
            {
                interface.hideToast();
                _state.showToast = 0;
                _state.dirty = true;
            }
        }

        if (protocol.state() != _state.latestState)
        {
            // On connect/disconnect
            if (protocol.state() == PROTOCOL_STATUS_CONNECTED || _state.latestState == PROTOCOL_STATUS_CONNECTED)
            {
                _state.frameRendered = false;
                _state.dirty = true;
                _state.requestFrame = 0;
            }
            // On connect
            if (protocol.state() == PROTOCOL_STATUS_CONNECTED)
            {
                decoder.flush();
                decoder.buffer.reset();
            }
            _state.latestState = protocol.state();
        }

        if (_state.latestState == PROTOCOL_STATUS_CONNECTED)
        {
            uint32_t latestFrameId = 0;
            if (decoder.buffer.consume(&frame, &latestFrameId))
            {
                newFrame = latestFrameId != frameId;
                if (newFrame || _state.dirty)
                {
                    if (interface.render(frame))
                    {
                        _state.frameRendered = true;
                        _state.dirty = false;
                        if (frameId > 0 && latestFrameId - frameId > 1)
                        {
                            dropframes += latestFrameId - frameId - 1;
                            log_d("Frame drop %d on %d total %d", latestFrameId - frameId - 1, latestFrameId, dropframes);
                        }
                        frameId = latestFrameId;
                    }
                }
            }

            if (_state.requestFrame > 0 && Settings::forceRedraw > 0 && _state.requestFrame++ % Settings::forceRedraw == 0)
            {
                log_d("Request screen update");
                protocol.send(Message::Control(BTN_SCREEN_REFRESH));
                if (_state.requestFrame > Settings::forceRedraw * 2)
                    _state.requestFrame = 0;
            }
        }

        if (!_state.frameRendered)
        {
            interface.drawHome(_state.dirty, _state.latestState, protocol.phoneName());
            _state.dirty = false;
            SDL_Event e;
            while (SDL_PollEvent(&e))
                processSystemEvent(e);
        }
        else
        {
            if (newFrame || ++skipEvents > Settings::eventsSkip)
            {
                if (processFrameEvents(protocol.writeQueue, interface) && Settings::forceRedraw > 0)
                {
                    _state.requestFrame = 1;
                }
                skipEvents = 0;
            }
        }

#ifndef NDEBUG
        if (_debug)
        {
            if (SDL_GetTicks() - debugLast >= 1000)
            {
                debugSpeed = (protocol.transfered() - debugLastCount) / (SDL_GetTicks() - debugLast);
                debugLastCount = protocol.transfered();
                debugLast = SDL_GetTicks();
            }
            char debugBuffer[2048];
            std::snprintf(debugBuffer, sizeof(debugBuffer),
                          "%s\n"
                          "FRAME: %u / %u [%d] dropped: %d render: %dus / %dus\n"
                          "USB: %s ~%dKB/s\n"
                          "BUFF: video [%u] audio[main %u aux %u] out [%u]",
                          status().c_str(),
                          frameId,
                          decoder.buffer.latestId(),
                          decoder.buffer.latestId() - frameId,
                          dropframes,
                          frameTime,
                          frameDelay,
                          protocol.status().c_str(),
                          debugSpeed,
                          protocol.videoStream.count(),
                          protocol.audioStreamMain.count(),
                          protocol.audioStreamAux.count(),
                          protocol.writeQueue.count());
            interface.debug(debugBuffer);
        }
#endif

        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        frameTime = (int32_t)std::chrono::duration_cast<std::chrono::microseconds>(now - frameStart).count();
        frameStart = now;
        if (_active && !Settings::vsync && !_state.dirty)
        {
            frameDelay = (frameTarget - frameTime) * ((decoder.buffer.latestId() == frameId) ? 1.0 : 0.9);
            if (frameDelay > 0)
            {
                std::this_thread::sleep_for(std::chrono::microseconds(frameDelay));
                frameStart += std::chrono::microseconds(frameDelay);
            }
        }
    }

    if (!Settings::isHeadless())
        SDL_HideWindow(_window);
}

const std::string Application::status() const
{
    std::ostringstream out;

    SDL_version compiled{};
    SDL_VERSION(&compiled);
    SDL_version linked{};
    SDL_GetVersion(&linked);

    out << "SDL: v"
        << static_cast<int>(compiled.major) << '.'
        << static_cast<int>(compiled.minor) << '.'
        << static_cast<int>(compiled.patch) << " "
        << SDL_GetCurrentVideoDriver();

    SDL_Window *window = SDL_GetKeyboardFocus();
    SDL_RendererInfo cr{};
    if (window)
    {
        int width = 0;
        int height = 0;
        SDL_GetWindowSize(window, &width, &height);
        out << " " << width << 'x' << height;
        SDL_Renderer *renderer = SDL_GetRenderer(window);
        if (renderer)
        {
            if (SDL_GetRendererInfo(renderer, &cr) == 0)
            {
                out << ((cr.flags & SDL_RENDERER_ACCELERATED) != 0 ? " accelerated" : "")
                    << ((cr.flags & SDL_RENDERER_PRESENTVSYNC) != 0 ? " vsync" : "");
            }
        }
    }
    out << " audio: " << SDL_GetCurrentAudioDriver();

    out << "\nBACKENDS:";
    for (int i = 0; i < SDL_GetNumRenderDrivers(); ++i)
    {
        SDL_RendererInfo info;
        SDL_GetRenderDriverInfo(i, &info);
        out << " ";
        if (cr.name == info.name)
            out << "[" << info.name << "]";
        else
            out << info.name;
    }

    int displayIndex = SDL_GetWindowDisplayIndex(window);
    if (displayIndex >= 0)
    {
        out << "\nSCREEN:";
        SDL_Rect bounds{};
        SDL_DisplayMode mode{};
        SDL_GetDisplayBounds(displayIndex, &bounds);
        if (SDL_GetCurrentDisplayMode(displayIndex, &mode) == 0)
        {
            out << " [" << displayIndex << "] "
                << bounds.w << 'x' << bounds.h
                << '@' << mode.refresh_rate
                << " " << SDL_GetPixelFormatName(mode.format);
        }
    }

    return out.str();
}
