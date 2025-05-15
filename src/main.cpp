// src/main.cpp

#include <iostream>
#include <vector>
#include <SDL2/SDL.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libavutil/channel_layout.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

#define SDL_AUDIO_BUFFER_SIZE 1024

int audio_decode_frame(AVCodecContext *aCtx, uint8_t *out_buf, int buf_size, AVPacket *pkt, AVFrame *frame) {
    if (avcodec_send_packet(aCtx, pkt) < 0) return 0;
    if (avcodec_receive_frame(aCtx, frame) < 0) return 0;

    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    int64_t in_ch_layout = aCtx->channel_layout;
    #pragma GCC diagnostic pop

    SwrContext *swr = swr_alloc();
    av_opt_set_int(swr, "in_channel_layout",  in_ch_layout,           0);
    av_opt_set_int(swr, "in_sample_rate",     aCtx->sample_rate,      0);
    av_opt_set_sample_fmt(swr, "in_sample_fmt", aCtx->sample_fmt,       0);
    av_opt_set_int(swr, "out_channel_layout", AV_CH_LAYOUT_STEREO,    0);
    av_opt_set_int(swr, "out_sample_rate",    aCtx->sample_rate,      0);
    av_opt_set_sample_fmt(swr, "out_sample_fmt", AV_SAMPLE_FMT_S16,    0);

    if (swr_init(swr) < 0) {
        swr_free(&swr);
        return 0;
    }

    int converted = swr_convert(
        swr,
        &out_buf,
        buf_size / 2,
        (const uint8_t**)frame->data,
        frame->nb_samples
    ) * 2;
    swr_free(&swr);
    return converted;
}

AVCodecContext* open_video_decoder(AVFormatContext* fmtCtx, int vidStream) {
    // prepare codec context
    AVCodecContext *vCtx = avcodec_alloc_context3(nullptr);
    avcodec_parameters_to_context(vCtx, fmtCtx->streams[vidStream]->codecpar);

    // list of hw accel names to try
    std::vector<const char*> hw_decoders = {
        "h264_v4l2m2m",
        "h264_mmal",
        "h264_omx",
        nullptr
    };

    int ret;
    const AVCodec *dec = nullptr;
    for (auto name : hw_decoders) {
        if (name) {
            dec = avcodec_find_decoder_by_name(name);
            if (!dec) continue;
        } else {
            // fallback to default software decoder for this codec id
            dec = avcodec_find_decoder(vCtx->codec_id);
        }

        ret = avcodec_open2(vCtx, dec, nullptr);
        if (ret == 0) {
            std::cout << "Opened video decoder: "
                      << (name ? name : dec->name)
                      << std::endl;
            return vCtx;
        }
    }

    // if we get here, nothing opened cleanly
    avcodec_free_context(&vCtx);
    return nullptr;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <video_file>\n";
        return -1;
    }

    av_log_set_level(AV_LOG_QUIET);
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER);

    // open media
    AVFormatContext *fmtCtx = nullptr;
    if (avformat_open_input(&fmtCtx, argv[1], nullptr, nullptr) < 0) {
        std::cerr << "Could not open input file\n";
        return -1;
    }
    if (avformat_find_stream_info(fmtCtx, nullptr) < 0) {
        std::cerr << "Could not find streams\n";
        return -1;
    }

    // find streams
    int vidStream = -1, audStream = -1;
    for (unsigned i = 0; i < fmtCtx->nb_streams; i++) {
        AVMediaType t = fmtCtx->streams[i]->codecpar->codec_type;
        if (t == AVMEDIA_TYPE_VIDEO && vidStream < 0) vidStream = i;
        if (t == AVMEDIA_TYPE_AUDIO && audStream < 0) audStream = i;
    }
    if (vidStream < 0 || audStream < 0) {
        std::cerr << "Missing video or audio stream\n";
        return -1;
    }

    // video decoder (with fallback)
    AVCodecContext *vCtx = open_video_decoder(fmtCtx, vidStream);
    if (!vCtx) {
        std::cerr << "Failed to open any video decoder\n";
        return -1;
    }

    // audio decoder
    AVCodecContext *aCtx = avcodec_alloc_context3(nullptr);
    avcodec_parameters_to_context(aCtx, fmtCtx->streams[audStream]->codecpar);
    const AVCodec *aDec = avcodec_find_decoder(aCtx->codec_id);
    if (!aDec || avcodec_open2(aCtx, aDec, nullptr) < 0) {
        std::cerr << "Failed to open audio decoder\n";
        return -1;
    }

    // SDL window / renderer / texture
    SDL_Window   *win      = SDL_CreateWindow(
        "SDL2 FFmpeg Player",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        vCtx->width, vCtx->height, 0
    );
    SDL_Renderer *renderer = SDL_CreateRenderer(win, -1, 0);
    SDL_Texture  *texture  = SDL_CreateTexture(
        renderer, SDL_PIXELFORMAT_YV12, SDL_TEXTUREACCESS_STREAMING,
        vCtx->width, vCtx->height
    );

    // SDL audio
    SDL_AudioSpec want = {};
    want.freq     = aCtx->sample_rate;
    want.format   = AUDIO_S16SYS;
    want.channels = 2;
    want.samples  = SDL_AUDIO_BUFFER_SIZE;
    want.callback = nullptr;
    if (SDL_OpenAudio(&want, nullptr) < 0) {
        std::cerr << "SDL_OpenAudio error: " << SDL_GetError() << "\n";
        return -1;
    }
    SDL_PauseAudio(0);

    // frames, packet, and scaler
    AVFrame *vFrame = av_frame_alloc();
    AVFrame *aFrame = av_frame_alloc();
    AVPacket pkt;
    SwsContext *sws = sws_getContext(
        vCtx->width, vCtx->height, vCtx->pix_fmt,
        vCtx->width, vCtx->height, AV_PIX_FMT_YUV420P,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );

    // main loop
    while (av_read_frame(fmtCtx, &pkt) >= 0) {
        if (pkt.stream_index == vidStream) {
            if (!avcodec_send_packet(vCtx, &pkt) &&
                !avcodec_receive_frame(vCtx, vFrame))
            {
                SDL_UpdateYUVTexture(
                    texture, nullptr,
                    vFrame->data[0], vFrame->linesize[0],
                    vFrame->data[1], vFrame->linesize[1],
                    vFrame->data[2], vFrame->linesize[2]
                );
                SDL_RenderClear(renderer);
                SDL_RenderCopy(renderer, texture, nullptr, nullptr);
                SDL_RenderPresent(renderer);
            }
        }
        else if (pkt.stream_index == audStream) {
            uint8_t buf[192000];
            int len = audio_decode_frame(aCtx, buf, sizeof(buf), &pkt, aFrame);
            if (len > 0) SDL_QueueAudio(0, buf, len);
        }
        av_packet_unref(&pkt);
        SDL_Delay(10);
    }

    // cleanup
    sws_freeContext(sws);
    av_frame_free(&vFrame);
    av_frame_free(&aFrame);
    avcodec_free_context(&vCtx);
    avcodec_free_context(&aCtx);
    avformat_close_input(&fmtCtx);
    SDL_Quit();
    return 0;
}
