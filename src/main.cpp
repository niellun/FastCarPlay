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

#include "resource/background.h"
#include "resource/font.h"

#include "helper/functions.h"
#include "helper/ufont.h"
#include "helper/uimage.h"

#include "struct/video_buffer.h"

#include "protocol.h"
#include "decoder.h"
#include "pcm_audio.h"
#include "renderer.h"

static const char *title = "Fast Car Play v0.2";
static SDL_Window *window = nullptr;
static SDL_Renderer *renderer = nullptr;
bool active = false;

static SDL_Texture *textTexture = nullptr;
static std::string textureText = "";
static SDL_Texture *imgTexture = nullptr;

static bool mouseDown = false;
static bool fullscreen = false;

std::mutex statusMutex;
std::string statusText;

void onStatus(const char *status)
{
    std::lock_guard<std::mutex> lock(statusMutex);
    statusText = status;
}

void DrawText(UFont &font, std::string text)
{
    if (!textTexture || textureText.compare(text) != 0)
    {
        if (textTexture)
            SDL_DestroyTexture(textTexture);
        textTexture = font.GetText(renderer, text.c_str(), {255, 255, 255, 255});
        textureText = text;
    }

    if (!textTexture)
        return;

    int textW, textH;
    SDL_QueryTexture(textTexture, nullptr, nullptr, &textW, &textH);

    int windowW, windowH;
    SDL_GetRendererOutputSize(renderer, &windowW, &windowH);

    // Center text
    SDL_Rect dstRect = {
        (windowW - textW) / 2,
        (windowH - textH) * 9 / 10,
        textW,
        textH};

    SDL_RenderCopy(renderer, textTexture, nullptr, &dstRect);
}

void DrawImage(UImage &img)
{
    if (!imgTexture)
        imgTexture = img.GetImage(renderer);

    if (!imgTexture)
        return;

    int windowW, windowH;
    SDL_GetRendererOutputSize(renderer, &windowW, &windowH);

    // Compute destination rectangle to center image
    SDL_Rect dst = {
        (windowW - img.Width) / 2,  // x: center horizontally
        (windowH - img.Height) / 2, // y: center vertically
        img.Width,                  // width: original image width
        img.Height                  // height: original image height
    };

    SDL_RenderCopy(renderer, imgTexture, nullptr, &dst);
}

void processKey(Protocol &protocol, SDL_Keysym key)
{
    switch (key.sym)
    {
    case SDLK_f:
        fullscreen = !fullscreen; // Toggle fullscreen mode
        SDL_SetWindowFullscreen(window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
        SDL_SetWindowBordered(window, fullscreen ? SDL_FALSE : SDL_TRUE);
        break;

    case SDLK_LEFT:
        protocol.sendKey(100);
        break;

    case SDLK_RIGHT:
        protocol.sendKey(101);
        break;

    case SDLK_RETURN:
        protocol.sendKey(104);
        protocol.sendKey(105);
        break;

    case SDLK_BACKSPACE:
        protocol.sendKey(106);
        break;
    }
}

void processEvents(Protocol &protocol)
{
    SDL_Event e;
    while (SDL_PollEvent(&e))
    {
        switch (e.type)
        {
        case SDL_QUIT:
            active = false;
            break;

        case SDL_MOUSEBUTTONDOWN:
        {
            mouseDown = true;
            int window_width, window_height;
            SDL_GetWindowSize(window, &window_width, &window_height);
            protocol.sendClick(1.0 * e.button.x / window_width, 1.0 * e.button.y / window_height, true);
            break;
        }

        case SDL_MOUSEBUTTONUP:
        {
            mouseDown = false;
            int window_width, window_height;
            SDL_GetWindowSize(window, &window_width, &window_height);
            protocol.sendClick(1.0 * e.button.x / window_width, 1.0 * e.button.y / window_height, false);
            break;
        }
        case SDL_MOUSEMOTION:
        {
            if (!mouseDown)
                break;
            int window_width, window_height;
            SDL_GetWindowSize(window, &window_width, &window_height);
            protocol.sendMove(1.0 * e.motion.x / window_width, 1.0 * e.motion.y / window_height);
            break;
        }
        case SDL_KEYDOWN:
        {
            processKey(protocol, e.key.keysym);
            break;
        }
        }
    }
}

void application()
{
    fullscreen = Settings::fullscreen;
    if (fullscreen)
    {
        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN);
        SDL_SetWindowBordered(window, SDL_FALSE);
    }
    SDL_ShowWindow(window);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255); // Set draw color to black
    SDL_RenderClear(renderer);                      // Clear renderer to black
    SDL_RenderPresent(renderer);                    // Present initial blank frame

    VideoBuffer videoBuffer;
    Protocol protocol(Settings::sourceWidth, Settings::sourceHeight, Settings::sourceFps, AV_INPUT_BUFFER_PADDING_SIZE);
    Decoder decoder;
    PcmAudio audio0, audio1, audio2;
    decoder.start(&protocol.videoData, &videoBuffer, AV_CODEC_ID_H264);
    audio0.start(&protocol.audioStream0);
    audio1.start(&protocol.audioStream1);
    audio2.start(&protocol.audioStream2);
    protocol.start(onStatus);

    UFont textFont(font, font_len, Settings::fontSize);
    std::string status = "Initialising";
    DrawText(textFont, status);
    SDL_RenderPresent(renderer);

    UImage image(background, background_len);
    SDL_RenderClear(renderer);
    DrawImage(image);
    DrawText(textFont, status);
    SDL_RenderPresent(renderer);

    std::cout << " > Application loop" << std::endl;
    Renderer videoRenderer(renderer);
    bool dirty = true;
    bool connected = false;
    bool videoPrepared = false;
    const int activeDelay = 1000 / Settings::fps;
    const int inactiveDelay = 1000 / 5; // 5FPS
    uint32_t frameDelay = inactiveDelay;
    active = true;
    uint32_t latestid = 0;
    while (active)
    {
        Uint32 frameStart = SDL_GetTicks();
        processEvents(protocol);

        if (connected != protocol.phoneConnected)
        {
            connected = protocol.phoneConnected;
            SDL_RenderClear(renderer);
            DrawImage(image);
            SDL_RenderPresent(renderer);
            dirty = true;
            videoPrepared = false;
            frameDelay = connected ? activeDelay : inactiveDelay;
        }

        if (connected)
        {
            AVFrame *frame = nullptr;
            uint32_t frameid = 0;
            if (videoBuffer.latest(&frame, &frameid) && frameid != latestid && frame)
            {
                if (!videoPrepared)
                    videoPrepared = videoRenderer.prepare(frame, Settings::width, Settings::height, Settings::scaler);
                if (videoPrepared && videoRenderer.render(frame))
                {
                    SDL_RenderClear(renderer);
                    SDL_RenderCopy(renderer, videoRenderer.texture, nullptr, nullptr);
                    SDL_RenderPresent(renderer);
                }
                latestid = frameid;
                videoBuffer.consume();
            }
        }
        else
        {
            {
                std::lock_guard<std::mutex> lock(statusMutex);
                if (status != statusText)
                {
                    status = statusText;
                    dirty = true;
                }
            }
            if (dirty)
            {
                SDL_RenderClear(renderer);
                DrawImage(image);
                DrawText(textFont, status);
                SDL_RenderPresent(renderer);
            }
        }

        Uint32 frameTime = SDL_GetTicks() - frameStart;
        if (frameTime < frameDelay)
        {
            SDL_Delay(frameDelay - frameTime); // Sleep only the remaining time
        }
    }
    std::cout << " > Application stopping" << std::endl;
    SDL_HideWindow(window);
}

int main(int argc, char **argv)
{
    std::cout << title << std::endl;

    if (argc > 2)
    {
        std::cerr << "  Usage: " << argv[0] << " [settings_file]" << std::endl;
        return 1;
    }
    if (argc == 2)
    {
        Settings::load(argv[1]);
    }

    if (!Settings::logging)
        disable_cout();
    else
        Settings::print();

    // Initialize SDL video subsystem
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO) != 0)
    {
        std::cerr << "[Main] SDL initialisation failed: " << SDL_GetError() << std::endl;
        return 1;
    }

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
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (renderer)
    {
        std::cout << " > Application started" << std::endl;
        application();
        std::cout << " > Application finish" << std::endl;
        SDL_DestroyRenderer(renderer);
    }
    else
    {
        std::cerr << "[Main] SDL can't create renderer: " << SDL_GetError() << std::endl;
    }

    if (textTexture)
        SDL_DestroyTexture(textTexture);
    if (imgTexture)
        SDL_DestroyTexture(imgTexture);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
    return 0;
}