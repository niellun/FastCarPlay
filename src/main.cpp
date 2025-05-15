#include <SDL2/SDL.h>
#include <pthread.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

#include <atomic>
#include <mutex>
#include <condition_variable>
#include <cmath>

#define MAX_CIRCLES 32
#define CIRCLE_LIFETIME 500   // ms
#define MAX_RADIUS 60         // px

struct Circle { int x, y; Uint32 start_time; bool active; };

// Double-buffered frames
static AVFrame *yuv_frames[2] = { nullptr, nullptr };
static int width, height;

// Sync primitives
static std::mutex buf_mutex;
static std::condition_variable buf_cv;
static std::atomic<bool> frame_ready(false);
static std::atomic<int> decoded_idx(0);
static std::atomic<bool> decoding_done(false);
static std::atomic<bool> global_quit(false);

static int interrupt_cb(void*) { return global_quit.load(); }

void draw_circle(SDL_Renderer* ren, int cx, int cy, int r) {
    int x = r, y = 0, err = 0;
    while (x >= y) {
        SDL_RenderDrawPoint(ren, cx + x, cy + y);
        SDL_RenderDrawPoint(ren, cx + y, cy + x);
        SDL_RenderDrawPoint(ren, cx - y, cy + x);
        SDL_RenderDrawPoint(ren, cx - x, cy + y);
        SDL_RenderDrawPoint(ren, cx - x, cy - y);
        SDL_RenderDrawPoint(ren, cx - y, cy - x);
        SDL_RenderDrawPoint(ren, cx + y, cy - x);
        SDL_RenderDrawPoint(ren, cx + x, cy - y);
        if (err <= 0) { y++; err += 2*y + 1; }
        if (err > 0)  { x--; err -= 2*x + 1; }
    }
}

int decode_thread(void* arg) {
    pthread_setname_np(pthread_self(), "decode");
    AVFormatContext *fmt_ctx = (AVFormatContext*)arg;
    int vid_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);

    AVCodecParameters *ppar = fmt_ctx->streams[vid_idx]->codecpar;
    const char* hw_list[] = {"h264_omx","h264_mmal","h264_v4l2m2m", nullptr};
    const AVCodec* dec = nullptr;
    for (int i = 0; hw_list[i]; ++i) if ((dec = avcodec_find_decoder_by_name(hw_list[i]))) break;
    if (!dec) dec = avcodec_find_decoder(ppar->codec_id);

    AVCodecContext *dec_ctx = avcodec_alloc_context3(dec);
    avcodec_parameters_to_context(dec_ctx, ppar);
    dec_ctx->thread_count = 1;
    avcodec_open2(dec_ctx, dec, nullptr);

    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    struct SwsContext *sws = sws_getContext(
        width, height, dec_ctx->pix_fmt,
        width, height, AV_PIX_FMT_YUV420P,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );

    while (!global_quit.load() && av_read_frame(fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index == vid_idx) {
            if (!avcodec_send_packet(dec_ctx, pkt)) {
                while (!avcodec_receive_frame(dec_ctx, frame)) {
                    int idx = decoded_idx.load();
                    sws_scale(sws, frame->data, frame->linesize,
                              0, height,
                              yuv_frames[idx]->data, yuv_frames[idx]->linesize);
                    {
                        std::lock_guard<std::mutex> lk(buf_mutex);
                        frame_ready = true;
                    }
                    buf_cv.notify_one();
                    std::unique_lock<std::mutex> lk(buf_mutex);
                    buf_cv.wait(lk, [&]{ return !frame_ready || global_quit.load(); });
                    if (global_quit.load()) break;
                    decoded_idx = 1 - idx;
                }
            }
        }
        av_packet_unref(pkt);
    }
    decoding_done = true;
    buf_cv.notify_all();
    sws_freeContext(sws);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&dec_ctx);
    return 0;
}

int main(int argc, char** argv) {
    pthread_setname_np(pthread_self(), "main");
    if (argc < 2) return -1;
    const char* file = argv[1];

    avformat_network_init();
    AVFormatContext *fmt_ctx = nullptr;
    avformat_open_input(&fmt_ctx, file, nullptr, nullptr);
    fmt_ctx->interrupt_callback.callback = interrupt_cb;
    avformat_find_stream_info(fmt_ctx, nullptr);

    int vid_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    width = fmt_ctx->streams[vid_idx]->codecpar->width;
    height = fmt_ctx->streams[vid_idx]->codecpar->height;

    for (int i = 0; i < 2; ++i) {
        yuv_frames[i] = av_frame_alloc();
        av_image_alloc(yuv_frames[i]->data, yuv_frames[i]->linesize,
                       width, height, AV_PIX_FMT_YUV420P, 1);
    }

    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);
    SDL_Window *win = SDL_CreateWindow("Player", SDL_WINDOWPOS_CENTERED,
                                       SDL_WINDOWPOS_CENTERED,
                                       width, height, SDL_WINDOW_RESIZABLE);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    SDL_Texture  *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_IYUV,
                                          SDL_TEXTUREACCESS_STREAMING,
                                          width, height);

    SDL_Thread *dt = SDL_CreateThread(decode_thread, "decode", fmt_ctx);
    Circle circles[MAX_CIRCLES] = {};
    Uint32 frame_delay = (Uint32)(1000.0 / av_q2d(fmt_ctx->streams[vid_idx]->avg_frame_rate));
    Uint32 last = SDL_GetTicks(); bool quit = false;
    bool full = false;

    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                quit = true;
                global_quit = true;
                buf_cv.notify_all();
            }
            if (e.type == SDL_MOUSEBUTTONDOWN) {
                for (auto &c : circles) if (!c.active) {
                    c.x = e.button.x;
                    c.y = e.button.y;
                    c.start_time = SDL_GetTicks();
                    c.active = true;
                    break;
                }
            }
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_f) {
                full = !full;
                SDL_SetWindowFullscreen(win, full ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                SDL_SetWindowBordered(win, full ? SDL_FALSE : SDL_TRUE);
            }
        }

        Uint32 now = SDL_GetTicks();
        if (now - last >= frame_delay || decoding_done.load()) {
            last = now;
            int disp_idx = 1 - decoded_idx.load();
            std::unique_lock<std::mutex> lk(buf_mutex);
            buf_cv.wait(lk, []{ return frame_ready.load() || decoding_done.load(); });
            if (frame_ready) {
                SDL_UpdateYUVTexture(tex, nullptr,
                                     yuv_frames[disp_idx]->data[0], yuv_frames[disp_idx]->linesize[0],
                                     yuv_frames[disp_idx]->data[1], yuv_frames[disp_idx]->linesize[1],
                                     yuv_frames[disp_idx]->data[2], yuv_frames[disp_idx]->linesize[2]);
                frame_ready = false;
                buf_cv.notify_one();
            }
            SDL_RenderClear(ren);
            SDL_RenderCopy(ren, tex, nullptr, nullptr);
            for (auto &c : circles) {
                if (!c.active) continue;
                Uint32 age = now - c.start_time;
                if (age > CIRCLE_LIFETIME) { c.active = false; continue; }
                float t = age / (float)CIRCLE_LIFETIME;
                int r = (int)(MAX_RADIUS * t);
                Uint8 a = (Uint8)(255 * (1 - t));
                SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(ren, 255, 0, 0, a);
                draw_circle(ren, c.x, c.y, r);
            }
            SDL_RenderPresent(ren);
        } else {
            SDL_Delay((last + frame_delay > now) ? last + frame_delay - now : 0);
        }
    }

    buf_cv.notify_all();
    SDL_WaitThread(dt, nullptr);
    for (int i = 0; i < 2; ++i) {
        av_freep(&yuv_frames[i]->data[0]);
        av_frame_free(&yuv_frames[i]);
    }
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();

    avformat_close_input(&fmt_ctx);
    avformat_network_deinit();
    return 0;
}
