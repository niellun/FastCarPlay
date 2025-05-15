#include <SDL2/SDL.h>
#include <pthread.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

#include <vector>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <cmath>

#define MAX_CIRCLES 32
#define CIRCLE_LIFETIME 500   // ms, faster animation
#define MAX_RADIUS 60         // maximum radius

struct Circle {
    int x, y;
    Uint32 start_time;
    bool active;
};

// Shared queues and synchronization (only hold 1 extra frame)
static std::vector<AVFrame*> frame_queue;
static std::mutex queue_mutex;
static std::condition_variable queue_cv;
static std::atomic<bool> decoding_done(false);
static std::atomic<bool> global_quit(false);

// FFmpeg interrupt callback
static int interrupt_cb(void* ctx) {
    return global_quit.load() ? 1 : 0;
}

// Draw circle outline using midpoint circle algorithm
void draw_circle(SDL_Renderer* ren, int cx, int cy, int r) {
    int x = r;
    int y = 0;
    int err = 0;
    while (x >= y) {
        SDL_RenderDrawPoint(ren, cx + x, cy + y);
        SDL_RenderDrawPoint(ren, cx + y, cy + x);
        SDL_RenderDrawPoint(ren, cx - y, cy + x);
        SDL_RenderDrawPoint(ren, cx - x, cy + y);
        SDL_RenderDrawPoint(ren, cx - x, cy - y);
        SDL_RenderDrawPoint(ren, cx - y, cy - x);
        SDL_RenderDrawPoint(ren, cx + y, cy - x);
        SDL_RenderDrawPoint(ren, cx + x, cy - y);
        if (err <= 0) {
            y++;
            err += 2*y + 1;
        }
        if (err > 0) {
            x--;
            err -= 2*x + 1;
        }
    }
}

// Decode thread: keeps only 1 decoded frame queued, uses hardware decoder if available
int decode_thread(void* arg) {
    pthread_setname_np(pthread_self(), "decode");
    AVFormatContext* fmt_ctx = (AVFormatContext*)arg;
    int vid_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (vid_idx < 0) return -1;

    AVCodecParameters* ppar = fmt_ctx->streams[vid_idx]->codecpar;
    // Try hardware-accelerated decoders in order
    const char* hw_list[] = {"h264_omx", "h264_mmal", "h264_v4l2m2m", nullptr};
    const AVCodec* decoder = nullptr;
    for (int i = 0; hw_list[i]; ++i) {
        decoder = avcodec_find_decoder_by_name(hw_list[i]);
        if (decoder) break;
    }
    if (!decoder) {
        decoder = avcodec_find_decoder(ppar->codec_id);
    }

    AVCodecContext* dec_ctx = avcodec_alloc_context3(decoder);
    avcodec_parameters_to_context(dec_ctx, ppar);
    dec_ctx->thread_count = 1;  // single-threaded decode
    if (avcodec_open2(dec_ctx, decoder, nullptr) < 0) {
        fprintf(stderr, "Failed to open codec\n");
        return -1;
    }

    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    AVFrame* yuv = av_frame_alloc();

    SwsContext* sws = sws_getContext(
        dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt,
        dec_ctx->width, dec_ctx->height, AV_PIX_FMT_YUV420P,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );
    av_image_alloc(yuv->data, yuv->linesize,
                   dec_ctx->width, dec_ctx->height,
                   AV_PIX_FMT_YUV420P, 1);

    while (!global_quit.load()) {
        if (av_read_frame(fmt_ctx, pkt) < 0) break;
        if (pkt->stream_index == vid_idx) {
            if (avcodec_send_packet(dec_ctx, pkt) == 0) {
                while (avcodec_receive_frame(dec_ctx, frame) == 0) {
                    // Convert to YUV420P for SDL
                    sws_scale(sws, frame->data, frame->linesize,
                              0, dec_ctx->height,
                              yuv->data, yuv->linesize);
                    AVFrame* copy = av_frame_alloc();
                    av_image_alloc(copy->data, copy->linesize,
                                   dec_ctx->width, dec_ctx->height,
                                   AV_PIX_FMT_YUV420P, 1);
                    for (int i = 0; i < 3; ++i) {
                        int h = (i == 0 ? dec_ctx->height : dec_ctx->height/2);
                        memcpy(copy->data[i], yuv->data[i], copy->linesize[i] * h);
                    }
                    {
                        std::unique_lock<std::mutex> lk(queue_mutex);
                        queue_cv.wait(lk, []{
                            return frame_queue.size() < 1 || global_quit.load();
                        });
                        if (global_quit.load()) {
                            av_frame_free(&copy);
                            break;
                        }
                        frame_queue.push_back(copy);
                    }
                    queue_cv.notify_one();
                }
            }
        }
        av_packet_unref(pkt);
    }

    decoding_done = true;
    queue_cv.notify_all();

    sws_freeContext(sws);
    av_freep(&yuv->data[0]); av_frame_free(&yuv);
    av_frame_free(&frame); av_packet_free(&pkt);
    avcodec_free_context(&dec_ctx);
    return 0;
}

int main(int argc, char* argv[]) {
    pthread_setname_np(pthread_self(), "main");
    if (argc < 2) return -1;
    const char* filename = argv[1];

    avformat_network_init();
    AVFormatContext* fmt_ctx = nullptr;
    if (avformat_open_input(&fmt_ctx, filename, nullptr, nullptr) < 0) return -1;
    fmt_ctx->interrupt_callback.callback = interrupt_cb;
    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) return -1;

    int vid_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    AVCodecParameters* ppar = fmt_ctx->streams[vid_idx]->codecpar;

    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);
    SDL_Window* win = SDL_CreateWindow("Player", SDL_WINDOWPOS_CENTERED,
                                       SDL_WINDOWPOS_CENTERED,
                                       ppar->width, ppar->height,
                                       SDL_WINDOW_RESIZABLE);
    SDL_Renderer* renderer = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    SDL_Texture* texture = SDL_CreateTexture(renderer,
                                             SDL_PIXELFORMAT_IYUV,
                                             SDL_TEXTUREACCESS_STREAMING,
                                             ppar->width, ppar->height);

    bool is_fullscreen = false;
    SDL_Thread* dt = SDL_CreateThread(decode_thread, "decode", fmt_ctx);

    Circle circles[MAX_CIRCLES] = {};
    Uint32 frame_delay = (Uint32)(1000.0 / av_q2d(fmt_ctx->streams[vid_idx]->avg_frame_rate));
    Uint32 last_time = SDL_GetTicks();
    bool quit = false;

    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                quit = true;
                global_quit = true;
                queue_cv.notify_all();
            }
            if (e.type == SDL_MOUSEBUTTONDOWN) {
                for (auto &c : circles) if (!c.active) {
                    c.x = e.button.x; c.y = e.button.y;
                    c.start_time = SDL_GetTicks(); c.active = true;
                    break;
                }
            }
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_f) {
                is_fullscreen = !is_fullscreen;
                if (is_fullscreen) {
                    SDL_SetWindowFullscreen(win, SDL_WINDOW_FULLSCREEN_DESKTOP);
                    SDL_SetWindowBordered(win, SDL_FALSE);
                } else {
                    SDL_SetWindowFullscreen(win, 0);
                    SDL_SetWindowBordered(win, SDL_TRUE);
                }
            }
        }

        Uint32 now = SDL_GetTicks();
        if (now - last_time >= frame_delay) {
            last_time = now;
            AVFrame* f = nullptr;
            {
                std::unique_lock<std::mutex> lk(queue_mutex);
                queue_cv.wait(lk, []{
                    return !frame_queue.empty() || decoding_done.load();
                });
                if (!frame_queue.empty()) {
                    f = frame_queue.front();
                    frame_queue.erase(frame_queue.begin());
                }
            }
            queue_cv.notify_one();

            if (f) {
                SDL_UpdateYUVTexture(texture, nullptr,
                                     f->data[0], f->linesize[0],
                                     f->data[1], f->linesize[1],
                                     f->data[2], f->linesize[2]);
                SDL_RenderClear(renderer);
                SDL_RenderCopy(renderer, texture, nullptr, nullptr);

                // draw outlined red circles fading out
                for (auto &c : circles) {
                    if (!c.active) continue;
                    Uint32 age = now - c.start_time;
                    if (age > CIRCLE_LIFETIME) { c.active = false; continue; }
                    float t = age / (float)CIRCLE_LIFETIME;
                    int r = (int)(MAX_RADIUS * t);
                    Uint8 a = (Uint8)(255 * (1 - t));
                    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                    SDL_SetRenderDrawColor(renderer, 255, 0, 0, a);
                    draw_circle(renderer, c.x, c.y, r);
                }

                SDL_RenderPresent(renderer);
                av_freep(&f->data[0]); av_frame_free(&f);
            }
        } else {
            Uint32 sleep_ms = (last_time + frame_delay > now) ? (last_time + frame_delay - now) : 0;
            SDL_Delay(sleep_ms);
        }
    }

    queue_cv.notify_all();
    SDL_WaitThread(dt, nullptr);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(win);
    SDL_Quit();

    avformat_close_input(&fmt_ctx);
    avformat_network_deinit();
    return 0;
}
