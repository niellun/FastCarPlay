#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string>
#include <unistd.h>
#include <iostream>

extern "C"
{
#include <libavformat/avformat.h> // FFmpeg library for multimedia container format handling
#include <libavcodec/avcodec.h>   // FFmpeg library for encoding/decoding
#include <libswscale/swscale.h>   // FFmpeg library for image scaling and pixel format conversion
#include <libavutil/imgutils.h>   // FFmpeg utility functions for image handling
}

#include "helper/functions.h"
#include "helper/protocol_const.h"
#include "struct/video_buffer.h"

#include "protocol.h"
#include "decoder.h"
#include "pcm_audio.h"
#include "interface.h"
#include "pipe_listener.h"

#define FRAME_DELAY_INACTIVE 200

static const char *title = "Fast Car Play v0.6";
static SDL_Window *window = nullptr;
static SDL_Renderer *renderer = nullptr;
Uint32 evtStatus = (Uint32)-1;
Uint32 evtConnected = (Uint32)-1;
bool active = true;

struct RunParams
{
    bool connected;
    bool videoPrepaired;
    bool videoRendered;
    bool dirty;
    bool fullscreen;
    bool mouseDown;
    uint8_t deviceStatus;
    uint32_t frameDelay;
    int activeDelay;
};

void processKey(Protocol &protocol, SDL_Keysym key, RunParams &params)
{
    switch (key.sym)
    {
    case SDLK_f:
        params.fullscreen = !params.fullscreen; // Toggle fullscreen mode
        SDL_SetWindowFullscreen(window, params.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
        SDL_SetWindowBordered(window, params.fullscreen ? SDL_FALSE : SDL_TRUE);
        return;

    case SDLK_q:
        active = false;
        return;

    case SDLK_r:
        params.dirty = true;
        return;

    case SDLK_LEFT:
        protocol.sendKey(BTN_LEFT);
        return;

    case SDLK_RIGHT:
        protocol.sendKey(BTN_RIGHT);
        return;

    case SDLK_RETURN:
        protocol.sendKey(BTN_SELECT_DOWN);
        protocol.sendKey(BTN_SELECT_UP);
        return;

    case SDLK_BACKSPACE:
        protocol.sendKey(BTN_BACK);
        return;

    case 0:
        return;
    }

    if (key.sym == Settings::keyLeft)
    {
        protocol.sendKey(BTN_LEFT);
    }
    else if (key.sym == Settings::keyRight)
    {
        protocol.sendKey(BTN_RIGHT);
    }
    else if (key.sym == Settings::keyEnter)
    {
        protocol.sendKey(BTN_SELECT_DOWN);
        protocol.sendKey(BTN_SELECT_UP);
    }
    else if (key.sym == Settings::keyBack)
    {
        protocol.sendKey(BTN_BACK);
    }
    else if (key.sym == Settings::keyHome)
    {
        protocol.sendKey(BTN_HOME);
    }
    else
    {
        std::cout << "[Key] Unmapped key " << key.sym << std::endl;
    }
}

bool processEvents(Protocol &protocol, RunParams &params, Renderer &renderer)
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
        switch (e.type)
        {
        case SDL_QUIT:
            active = false;
            break;

        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_RESIZED)
            {
                params.dirty = true;
            }
            break;

        case SDL_MOUSEBUTTONDOWN:
        {
            params.mouseDown = true;
            downX = e.button.x;
            downY = e.button.y;
            break;
        }

        case SDL_MOUSEBUTTONUP:
        {
            params.mouseDown = false;
            upX = e.button.x;
            upY = e.button.y;
            break;
        }
        case SDL_MOUSEMOTION:
        {
            if (!params.mouseDown)
                break;
            motionX = e.motion.x;
            motionY = e.motion.y;
            break;
        }
        case SDL_KEYDOWN:
        {
            processKey(protocol, e.key.keysym, params);
            break;
        }
        default:
        {
            if (e.type == evtConnected)
            {
                params.connected = e.user.code != 0;
                params.dirty = true;
                params.videoRendered = false;
                params.frameDelay = params.connected ? params.activeDelay : FRAME_DELAY_INACTIVE;
                params.videoPrepaired = false;
                result = true;
            }
            else if (e.type == evtStatus)
            {
                params.deviceStatus = e.user.code;
            }
        }
        }
    }

    if (params.videoRendered && (downX >= 0 || upX >= 0 || motionX >= 0))
    {
        int window_width, window_height;
        SDL_GetWindowSize(window, &window_width, &window_height);
        if (downX >= 0)
            protocol.sendClick(renderer.xScale * downX / window_width, renderer.yScale * downY / window_height, true);
        if (motionX >= 0)
            protocol.sendMove(renderer.xScale * motionX / window_width, renderer.yScale * motionY / window_height);
        if (upX >= 0)
            protocol.sendClick(renderer.xScale * upX / window_width, renderer.yScale * upY / window_height, false);
    }

    return result;
}

void application()
{
    RunParams p;
    p.activeDelay = 1000 / Settings::fps;
    p.connected = false;
    p.deviceStatus = PROTOCOL_STATUS_INITIALISING;
    p.dirty = false;
    p.frameDelay = FRAME_DELAY_INACTIVE;
    p.videoPrepaired = false;
    p.videoRendered = false;
    p.fullscreen = Settings::fullscreen;
    p.mouseDown = false;

    if (p.fullscreen)
    {
        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN);
        SDL_SetWindowBordered(window, SDL_FALSE);
    }
    SDL_ShowWindow(window);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255); // Set draw color to black
    SDL_RenderClear(renderer);                      // Clear renderer to black
    SDL_RenderPresent(renderer);                    // Present initial blank frame

    VideoBuffer videoBuffer;
    Protocol protocol(Settings::width, Settings::height, Settings::sourceFps, AV_INPUT_BUFFER_PADDING_SIZE);
    Decoder decoder;
    PcmAudio audioMain("Main"), audioAux("Aux");
    decoder.start(&protocol.videoData, &videoBuffer, AV_CODEC_ID_H264);
    audioMain.start(&protocol.audioStreamMain);
    audioAux.start(&protocol.audioStreamAux, &audioMain);
    protocol.start(evtStatus, evtConnected);
    Interface interface(renderer);
    PipeListener pipeListener(protocol, (Settings::keyPipe.value.length() > 2) ? Settings::keyPipe.value.c_str() : nullptr);

    std::cout << "[Main] Loop" << std::endl;
    uint32_t latestid = 0;
    Uint32 frameStart = SDL_GetTicks();
    while (active)
    {
        if (processEvents(protocol, p, interface))
        {
            if (p.connected)
            {
                decoder.flush();
                videoBuffer.reset();
            }
        }

        if (p.connected)
        {
            AVFrame *frame = nullptr;
            uint32_t frameid = 0;
            if (videoBuffer.latest(&frame, &frameid) && (frameid != latestid || p.dirty) && frame)
            {
                if (interface.render(frame))
                {
                    p.videoRendered = true;
                    if (!p.dirty && (frameid != latestid + 1))
                        std::cout << "[Main] Frame drop " << frameid - latestid - 1 << " on " << frameid << std::endl;
                    latestid = frameid;
                    p.dirty = false;
                }
                videoBuffer.consume();
            }
        }

        if (!p.videoRendered)
        {
            interface.drawHome(p.dirty, p.connected ? PROTOCOL_STATUS_CONNECTED : p.deviceStatus);
            p.dirty = false;
        }

        Uint32 frameEnd = SDL_GetTicks();
        Uint32 frameTime = frameEnd - frameStart;
        if (active && frameTime < p.frameDelay)
        {
            SDL_Delay(p.frameDelay - frameTime); // Sleep only the remaining time
            frameStart = frameStart + p.frameDelay;
        }
        else
            frameStart = frameEnd;
    }
    std::cout << "[Main] Stopping" << std::endl;
    SDL_HideWindow(window);
}

bool setAudioDriver()
{
    if (Settings::audioDriver.value.length() < 2)
        return true;

    int num = SDL_GetNumAudioDrivers();
    for (int i = 0; i < num; ++i)
    {
        if (SDL_GetAudioDriver(i) == Settings::audioDriver.value)
        {
            SDL_setenv("SDL_AUDIODRIVER", SDL_GetAudioDriver(i), 1);
            return true;
        }
    }
    return false;
}

int start()
{
    if (!Settings::logging)
        disable_cout();
    else
        Settings::print();

    if (!setAudioDriver())
        std::cerr << "[Main] Not supported audio driver " << Settings::audioDriver.value << std::endl;

    // Initialize SDL video subsystem
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO) != 0)
    {
        std::cerr << "[Main] SDL initialisation failed: " << SDL_GetError() << std::endl;
        return 1;
    }
    std::cout << "[Main] SDL audio driver: " << SDL_GetCurrentAudioDriver() << std::endl;

    if (TTF_Init() != 0)
    {
        std::cerr << "[Main] TTF initialisation failed: " << TTF_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    SDL_DisplayMode displayMode;
    if (SDL_GetCurrentDisplayMode(0, &displayMode) != 0)
    {
        std::cerr << "[Main] SDL get display mode failed: " << SDL_GetError() << std::endl;
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    // Create SDL window centered on screen
    if (Settings::fastScale)
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    else
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best");
    window = SDL_CreateWindow(title,
                              SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED,
                              Settings::fullscreen ? displayMode.w : Settings::width,
                              Settings::fullscreen ? displayMode.h : Settings::height,
                              SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN);

    if (!window)
    {
        std::cerr << "[Main] SDL can't create window: " << SDL_GetError() << std::endl;
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    // Create accelerated renderer for the window
    renderer = SDL_CreateRenderer(window, -1, (Settings::vsync ? (SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : SDL_RENDERER_ACCELERATED));
    if (renderer)
    {
        evtStatus = SDL_RegisterEvents(2);
        if (evtStatus != (Uint32)-1)
        {
            evtConnected = evtStatus + 1;
            std::cout << "[Main] Started" << std::endl;
            application();
            std::cout << "[Main] Finish" << std::endl;
        }
        else
        {
            std::cerr << "[Main] Can't register custom events" << std::endl;
        }

        SDL_DestroyRenderer(renderer);
    }
    else
    {
        std::cerr << "[Main] SDL can't create renderer: " << SDL_GetError() << std::endl;
    }

    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
    return 0;
}

int main(int argc, char **argv)
{
    std::cout << title << std::endl;
    if (argc > 2)
    {
        std::cerr << "  Usage: " << argv[0] << " [settings_file]" << std::endl;
        return 0;
    }
    try
    {
        if (argc == 2)
            Settings::load(argv[1]);
        return start();
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << std::endl;
        return 1;
    }
}