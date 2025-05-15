#include <SDL2/SDL.h> // Include SDL2 library headers for graphics and event handling
#include <SDL2/SDL_ttf.h>
#include <pthread.h> // Include POSIX threads library for multithreading
#include "resource/background.h"
#include "resource/sst_regular.h"

extern "C"
{
#include <libavformat/avformat.h> // FFmpeg library for multimedia container format handling
#include <libavcodec/avcodec.h>   // FFmpeg library for encoding/decoding
#include <libswscale/swscale.h>   // FFmpeg library for image scaling and pixel format conversion
#include <libavutil/imgutils.h>   // FFmpeg utility functions for image handling
}

#include <atomic>             // C++ atomic types for thread-safe variables
#include <mutex>              // C++ mutex for locking resources
#include <condition_variable> // C++ condition variable for thread synchronization
#include <cmath>              // Math functions (not explicitly used here but included)
#include <cstdio>             // Standard C I/O functions
#include <vector>             // C++ dynamic array container
#include <string>             // C++ string type

#include <unistd.h>
#include <iostream>

#define MAX_CIRCLES 32      // Maximum number of circles drawn on the screen
#define CIRCLE_LIFETIME 500 // Circle lifetime in milliseconds
#define MAX_RADIUS 60       // Maximum radius of a circle in pixels

// Structure to represent a circle with position, start time and active state
struct Circle
{
    int x, y;          // Circle center coordinates
    Uint32 start_time; // Time when the circle was created (milliseconds)
    bool active;       // Whether the circle is currently active (visible)
};

// Double-buffered frames to hold decoded video frames in YUV format
static AVFrame *yuv_frames[2] = {nullptr, nullptr};
static int width, height; // Video frame width and height

// Synchronization primitives for thread-safe communication between decode and main thread
static std::mutex buf_mutex;                   // Mutex to protect shared resources
static std::condition_variable buf_cv;         // Condition variable to notify frame availability
static std::atomic<bool> frame_ready(false);   // Atomic flag indicating a new frame is ready for display
static std::atomic<int> decoded_idx(0);        // Atomic index for the currently decoded frame buffer
static std::atomic<bool> decoding_done(false); // Atomic flag indicating decoding is finished
static std::atomic<bool> global_quit(false);   // Atomic flag signaling application shutdown

// Interrupt callback for FFmpeg to check if decoding should stop
static int interrupt_cb(void *) { return global_quit.load(); }

// Function to draw a circle on an SDL renderer using midpoint circle algorithm
void draw_circle(SDL_Renderer *renderer, int cx, int cy, int r)
{
    int x = r, y = 0, err = 0;
    while (x >= y)
    {
        // Draw symmetrical points for each octant of the circle
        SDL_RenderDrawPoint(renderer, cx + x, cy + y);
        SDL_RenderDrawPoint(renderer, cx + y, cy + x);
        SDL_RenderDrawPoint(renderer, cx - y, cy + x);
        SDL_RenderDrawPoint(renderer, cx - x, cy + y);
        SDL_RenderDrawPoint(renderer, cx - x, cy - y);
        SDL_RenderDrawPoint(renderer, cx - y, cy - x);
        SDL_RenderDrawPoint(renderer, cx + y, cy - x);
        SDL_RenderDrawPoint(renderer, cx + x, cy - y);
        // Update error term and coordinates to draw circle outline
        if (err <= 0)
        {
            y++;
            err += 2 * y + 1;
        }
        if (err > 0)
        {
            x--;
            err -= 2 * x + 1;
        }
    }
}

// Helper function to get all hardware-accelerated decoder names for a given codec ID
static std::vector<std::string> get_hw_codec_names(AVCodecID codec_id)
{
    std::vector<std::string> hw_names; // List of hardware decoder names
    void *iter = nullptr;
    const AVCodec *codec = nullptr;

    // Iterate over all registered codecs
    while ((codec = av_codec_iterate(&iter)))
    {
        if (!av_codec_is_decoder(codec)) // Skip if not a decoder
            continue;
        if (codec->id != codec_id) // Skip if codec ID doesn't match
            continue;
        if (!(codec->capabilities & AV_CODEC_CAP_HARDWARE)) // Skip if no hardware acceleration
            continue;
        hw_names.push_back(codec->name); // Add hardware codec name to list
    }
    return hw_names; // Return list of hardware decoder names
}

// Thread function to decode video frames from the input file
int decode_thread(void *arg)
{
    pthread_setname_np(pthread_self(), "decode");                                       // Set thread name to "decode"
    AVFormatContext *fmt_ctx = (AVFormatContext *)arg;                                  // Cast argument to format context pointer
    int vid_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0); // Find video stream index
    AVCodecParameters *ppar = fmt_ctx->streams[vid_idx]->codecpar;                      // Get codec parameters for video stream

    AVCodecContext *dec_ctx = nullptr; // Decoder context pointer
    const AVCodec *codec = nullptr;    // Decoder codec pointer

    // Attempt to use hardware-accelerated decoders first
    std::vector<std::string> hw_codec_names = get_hw_codec_names(ppar->codec_id);
    for (const auto &hw_name : hw_codec_names)
    {
        codec = avcodec_find_decoder_by_name(hw_name.c_str()); // Find hardware decoder by name
        if (!codec)
            continue;
        dec_ctx = avcodec_alloc_context3(codec); // Allocate decoder context
        if (!dec_ctx)
            continue;
        avcodec_parameters_to_context(dec_ctx, ppar);    // Copy codec parameters
        if (avcodec_open2(dec_ctx, codec, nullptr) >= 0) // Try to open hardware decoder
        {
            fprintf(stderr, "Using HW decoder: %s\n", codec->name);
            break;
        }
        avcodec_free_context(&dec_ctx); // Free context if open failed
        dec_ctx = nullptr;
    }

    // If no hardware decoder found, fallback to software decoder
    if (!dec_ctx)
    {
        codec = avcodec_find_decoder(ppar->codec_id);   // Find software decoder
        dec_ctx = avcodec_alloc_context3(codec);        // Allocate decoder context
        avcodec_parameters_to_context(dec_ctx, ppar);   // Copy codec parameters
        if (avcodec_open2(dec_ctx, codec, nullptr) < 0) // Open software decoder
        {
            fprintf(stderr, "Failed to open codec: %s\n", codec->name);
            return -1; // Return error if open fails
        }
        fprintf(stderr, "Using software decoder: %s\n", codec->name);
    }

    AVPacket *pkt = av_packet_alloc();       // Allocate packet to hold encoded data
    AVFrame *frame = av_frame_alloc();       // Allocate frame to hold decoded data
    struct SwsContext *sws = sws_getContext( // Setup scaler context to convert pixel format to YUV420P
        width, height, dec_ctx->pix_fmt,
        width, height, AV_PIX_FMT_YUV420P,
        SWS_BILINEAR, nullptr, nullptr, nullptr);

    // Main decoding loop until quit or no more packets
    while (!global_quit.load() && av_read_frame(fmt_ctx, pkt) >= 0)
    {
        if (pkt->stream_index == vid_idx) // Process only video stream packets
        {
            if (!avcodec_send_packet(dec_ctx, pkt)) // Send packet to decoder
            {
                while (!avcodec_receive_frame(dec_ctx, frame)) // Receive all decoded frames
                {
                    int idx = decoded_idx.load(); // Get current decoded frame index
                    // Convert frame pixel format and scale to YUV420P buffer
                    sws_scale(sws, frame->data, frame->linesize,
                              0, height,
                              yuv_frames[idx]->data, yuv_frames[idx]->linesize);
                    {
                        std::lock_guard<std::mutex> lk(buf_mutex); // Lock mutex
                        frame_ready = true;                        // Mark frame ready for display
                    }
                    buf_cv.notify_one(); // Notify main thread
                    std::unique_lock<std::mutex> lk(buf_mutex);
                    // Wait until frame is consumed or quit is triggered
                    buf_cv.wait(lk, [&]
                                { return !frame_ready || global_quit.load(); });
                    if (global_quit.load())
                        break;             // Exit if quitting
                    decoded_idx = 1 - idx; // Swap buffer index for double buffering
                }
            }
        }
        av_packet_unref(pkt); // Release packet resources
    }
    decoding_done = true;           // Mark decoding finished
    buf_cv.notify_all();            // Notify all waiting threads
    sws_freeContext(sws);           // Free scaling context
    av_frame_free(&frame);          // Free decoded frame memory
    av_packet_free(&pkt);           // Free packet memory
    avcodec_free_context(&dec_ctx); // Free codec context
    return 0;                       // Thread exit with success
}

TTF_Font *LoadEmbeddedFont(int ptsize)
{
    SDL_RWops *font_rw = SDL_RWFromConstMem(sst_regular, sst_regular_len);
    if (!font_rw)
    {
        SDL_Log("Failed to create RWops from font: %s", SDL_GetError());
        return nullptr;
    }

    TTF_Font *font = TTF_OpenFontRW(font_rw, 1, ptsize); // 1 = SDL_ttf will free RWops
    if (!font)
    {
        SDL_Log("Failed to load font from RWops: %s", TTF_GetError());
    }

    return font;
}

void DrawText(SDL_Renderer *renderer, TTF_Font *font, const char *text)
{
    SDL_Color white = {255, 255, 255, 255};

    SDL_Surface *textSurface = TTF_RenderText_Blended(font, text, white);
    if (!textSurface)
    {
        SDL_Log("Failed to create text surface: %s", TTF_GetError());
        return;
    }

    SDL_Texture *textTexture = SDL_CreateTextureFromSurface(renderer, textSurface);
    SDL_FreeSurface(textSurface);

    int textW, textH;
    SDL_QueryTexture(textTexture, nullptr, nullptr, &textW, &textH);

    int win_w, win_h;
    SDL_GetRendererOutputSize(renderer, &win_w, &win_h);

    // Center text
    SDL_Rect dstRect = {
        (win_w - textW) / 2,
        int((win_h - textH) * 0.9),
        textW,
        textH};

    SDL_RenderCopy(renderer, textTexture, nullptr, &dstRect);
    SDL_DestroyTexture(textTexture);
}

SDL_Texture *loadBMPFromMemory(SDL_Renderer *renderer, int *w, int *h)
{
    // Create SDL_RWops from memory buffer (no disk I/O)
    SDL_RWops *rw = SDL_RWFromConstMem(background, background_len);
    if (!rw)
    {
        std::cerr << "SDL_RWFromConstMem failed: " << SDL_GetError() << std::endl;
        return nullptr;
    }

    // Load BMP surface from RWops (SDL takes ownership of rw if flag is 1)
    SDL_Surface *surface = SDL_LoadBMP_RW(rw, 1);
    if (!surface)
    {
        std::cerr << "SDL_LoadBMP_RW failed: " << SDL_GetError() << std::endl;
        return nullptr;
    }

    // Output image dimensions for later centering
    *w = surface->w;
    *h = surface->h;

    // Create hardware-accelerated texture from surface
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);
    if (!texture)
    {
        std::cerr << "SDL_CreateTextureFromSurface failed: " << SDL_GetError() << std::endl;
    }

    return texture;
}

// Main function: program entry point
int main(int argc, char **argv)
{
    int img_w = 0;
    int img_h = 0;

    // Initialize SDL video subsystem
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
    {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
        return 1;
    }

    SDL_DisplayMode displayMode;
    if (SDL_GetCurrentDisplayMode(0, &displayMode) != 0)
    {
        SDL_Log("SDL_GetCurrentDisplayMode failed: %s", SDL_GetError());
        // Handle error or set default size
    }

    // Create SDL window centered on screen, 800x600 size
    SDL_Window *window = SDL_CreateWindow("Fast Carplay",
                                          SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, displayMode.w, displayMode.h, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN);
    if (!window)
    {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    // Create accelerated renderer for the window
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer)
    {
        std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255); // Set draw color to black
    SDL_RenderClear(renderer);                      // Clear renderer to black
    SDL_RenderPresent(renderer);                    // Present initial blank frame

    // init SDL_ttf once
    if (TTF_Init() != 0)
    {
        std::cerr << "TTF_Init failed: " << TTF_GetError() << std::endl;
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // open font from memory (do not close fontRW, TTF_OpenFontRW will handle it if final arg is 1)
    TTF_Font *font = LoadEmbeddedFont(30);
    DrawText(renderer, font, "Loading");

    SDL_RenderPresent(renderer); // Present initial blank frame

    // Load texture from embedded image data
    SDL_Texture *texture = loadBMPFromMemory(renderer, &img_w, &img_h);
    if (!texture)
    {
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Get current window size to center image dynamically
    int win_w, win_h;
    SDL_GetRendererOutputSize(renderer, &win_w, &win_h);

    // Compute destination rectangle to center image
    SDL_Rect dst = {
        (win_w - img_w) / 2, // x: center horizontally
        (win_h - img_h) / 2, // y: center vertically
        img_w,               // width: original image width
        img_h                // height: original image height
    };

    // Copy texture to renderer at centered position
    SDL_RenderClear(renderer);                      // Clear renderer to black
    SDL_RenderCopy(renderer, texture, nullptr, &dst);
    DrawText(renderer, font, "Initialising");

    // Present rendered frame on screen

    SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN);
    SDL_ShowWindow(window);
    SDL_RenderPresent(renderer);
    bool full = true;            // Flag for fullscreen toggle


    sleep(4);

    pthread_setname_np(pthread_self(), "main"); // Set main thread name
    if (argc < 2)                               // Check if filename argument is provided
        return -1;
    const char *file = argv[1]; // Input video file path

    avformat_network_init(); // Initialize network components of FFmpeg
    AVFormatContext *fmt_ctx = nullptr;
    avformat_open_input(&fmt_ctx, file, nullptr, nullptr); // Open video file
    fmt_ctx->interrupt_callback.callback = interrupt_cb;   // Set interrupt callback for graceful exit
    avformat_find_stream_info(fmt_ctx, nullptr);           // Retrieve stream information

    int vid_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0); // Get video stream index
    width = fmt_ctx->streams[vid_idx]->codecpar->width;                                 // Get video width
    height = fmt_ctx->streams[vid_idx]->codecpar->height;                               // Get video height

    // Allocate two YUV420P frames for double buffering and initialize to black
    for (int i = 0; i < 2; ++i)
    {
        yuv_frames[i] = av_frame_alloc(); // Allocate AVFrame
        yuv_frames[i]->format = AV_PIX_FMT_YUV420P;
        yuv_frames[i]->width = width;
        yuv_frames[i]->height = height;
        av_frame_get_buffer(yuv_frames[i], 32); // 32 = alignment
        // av_image_alloc(yuv_frames[i]->data, yuv_frames[i]->linesize, width, height, AV_PIX_FMT_YUV420P, 1);                           // Allocate image buffers for YUV420P
        // memset(yuv_frames[i]->data[0], 0, yuv_frames[i]->linesize[0] * height);         // Set Y plane to black (0)
        // memset(yuv_frames[i]->data[1], 128, yuv_frames[i]->linesize[1] * (height / 2)); // Set U plane to 128 (neutral)
        // memset(yuv_frames[i]->data[2], 128, yuv_frames[i]->linesize[2] * (height / 2)); // Set V plane to 128 (neutral)
    }

    SDL_Texture *tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_IYUV, // Create texture for YUV frames
                                         SDL_TEXTUREACCESS_STREAMING,
                                         width, height);

    // SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255); // Set draw color to black
    // SDL_RenderClear(renderer);                      // Clear renderer to black
    // SDL_RenderPresent(renderer);                    // Present initial blank frame

    SDL_Thread *dt = SDL_CreateThread(decode_thread, "decode", fmt_ctx); // Start decoding thread

    Circle circles[MAX_CIRCLES] = {}; // Initialize array of circles (all inactive)
    // Calculate delay between frames based on video frame rate (in milliseconds)
    Uint32 frame_delay = (Uint32)(1000.0 / av_q2d(fmt_ctx->streams[vid_idx]->avg_frame_rate));
    Uint32 last = SDL_GetTicks(); // Record last frame display time
    bool quit = false;            // Flag to signal quit event


    // Main event and rendering loop
    while (!quit)
    {
        SDL_Event e;
        while (SDL_PollEvent(&e)) // Process all SDL events
        {
            if (e.type == SDL_QUIT) // User requested quit
            {
                quit = true;
                global_quit = true;  // Signal all threads to quit
                buf_cv.notify_all(); // Wake up waiting threads
            }
            if (e.type == SDL_MOUSEBUTTONDOWN) // Mouse button pressed
            {
                // Activate first inactive circle at mouse click position
                for (auto &c : circles)
                    if (!c.active)
                    {
                        c.x = e.button.x;
                        c.y = e.button.y;
                        c.start_time = SDL_GetTicks();
                        c.active = true;
                        break;
                    }
            }
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_f) // 'f' key pressed
            {
                full = !full; // Toggle fullscreen mode
                SDL_SetWindowFullscreen(window, full ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                SDL_SetWindowBordered(window, full ? SDL_FALSE : SDL_TRUE);
            }
        }

        Uint32 now = SDL_GetTicks(); // Current time in milliseconds
        // Update frame if enough time has passed or decoding is done
        if (now - last >= frame_delay || decoding_done.load())
        {
            last = now;
            int disp_idx = 1 - decoded_idx.load(); // Get index of frame to display
            std::unique_lock<std::mutex> lk(buf_mutex);
            // Wait until a frame is ready or decoding is done
            buf_cv.wait(lk, []
                        { return frame_ready.load() || decoding_done.load(); });
            if (frame_ready)
            {
                // Update SDL texture with YUV frame data
                SDL_UpdateYUVTexture(tex, nullptr,
                                     yuv_frames[disp_idx]->data[0], yuv_frames[disp_idx]->linesize[0],
                                     yuv_frames[disp_idx]->data[1], yuv_frames[disp_idx]->linesize[1],
                                     yuv_frames[disp_idx]->data[2], yuv_frames[disp_idx]->linesize[2]);
                frame_ready = false; // Mark frame as consumed
                buf_cv.notify_one(); // Notify decoding thread
            }
            SDL_RenderClear(renderer);                       // Clear renderer
            SDL_RenderCopy(renderer, tex, nullptr, nullptr); // Render video frame

            // Draw all active circles with fading radius and alpha
            for (auto &c : circles)
            {
                if (!c.active)
                    continue;
                Uint32 age = now - c.start_time; // Calculate age of circle
                if (age > CIRCLE_LIFETIME)       // Deactivate circle if expired
                {
                    c.active = false;
                    continue;
                }
                float t = age / (float)CIRCLE_LIFETIME;                    // Normalized lifetime [0..1]
                int r = (int)(MAX_RADIUS * t);                             // Radius grows over lifetime
                Uint8 a = (Uint8)(255 * (1 - t));                          // Alpha fades over lifetime
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND); // Enable blending for transparency
                SDL_SetRenderDrawColor(renderer, 255, 0, 0, a);            // Set draw color red with alpha
                draw_circle(renderer, c.x, c.y, r);                        // Draw the circle
            }
            SDL_RenderPresent(renderer); // Present final rendered frame
        }
        else
        {
            // Delay to limit frame rate to video frame rate
            SDL_Delay((last + frame_delay > now) ? last + frame_delay - now : 0);
        }
    }

    buf_cv.notify_all();         // Notify threads to quit if waiting
    SDL_WaitThread(dt, nullptr); // Wait for decode thread to finish

    // Free allocated YUV frames
    for (int i = 0; i < 2; ++i)
    {
        av_freep(&yuv_frames[i]->data[0]);
        av_frame_free(&yuv_frames[i]);
    }

    // Clean up SDL resources
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    // Close input file and network cleanup
    avformat_close_input(&fmt_ctx);
    avformat_network_deinit();
    return 0; // Exit program successfully
}
