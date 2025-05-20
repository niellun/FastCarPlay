#include "renderer.h"
#include "helper/error.h"
#include <iostream>

const Renderer::FormatMapping Renderer::_mapping[] = {
    {AV_PIX_FMT_RGB24, SDL_PIXELFORMAT_RGB24, &Renderer::rgb, "RGB24"},
    {AV_PIX_FMT_YUV420P, SDL_PIXELFORMAT_IYUV, &Renderer::yuv, "YUV420P"},
    {AV_PIX_FMT_YUVJ420P, SDL_PIXELFORMAT_IYUV, &Renderer::yuv, "YUVJ420P"},    
    {AV_PIX_FMT_NV12, SDL_PIXELFORMAT_NV12, &Renderer::nv, "NV12"}};

Renderer::Renderer(SDL_Renderer *renderer)
    : texture(nullptr),
      textureWidth(0),
      textureHeight(0),
      _renderer(renderer),
      _render(nullptr),
      _sws(nullptr),
      _swsWidth(0),
      _swsHeight(0),
      _frame(nullptr)
{
}

Renderer::~Renderer()
{
    if (texture)
    {
        SDL_DestroyTexture(texture);
        texture = nullptr;
    }
    if (_sws)
    {
        sws_freeContext(_sws);
        _sws = nullptr;
    }
    if (_frame)
    {
        av_frame_free(&_frame);
        _frame = nullptr;
    }
}

bool Renderer::render(AVFrame *frame)
{
    if (_render == nullptr)
        return false;
    (this->*_render)(frame);
    return true;
}

bool Renderer::prepareTexture(uint32_t format, int width, int height)
{
    if (texture)
    {
        if (textureWidth == width && textureHeight == height)
            return true;
        SDL_DestroyTexture(texture);
        texture = nullptr;
    }

    texture = SDL_CreateTexture(_renderer, format,
                                SDL_TEXTUREACCESS_STREAMING,
                                width, height);
    if (!texture)
    {
        std::cerr << "[Render] SDL can't create video texture: " << SDL_GetError() << std::endl;
        return false;
    }
    return true;
}

bool Renderer::prepare(AVFrame *frame, int targetWidth, int targetHeight, uint32_t scaler)
{
    if (frame->width == targetWidth && frame->height == targetHeight)
    {
        AVPixelFormat fmt = static_cast<AVPixelFormat>(frame->format);
        for (const FormatMapping &mapping : _mapping)
        {
            if (mapping.avFormat == fmt)
            {
                if (prepareTexture(mapping.sdlFormat, targetWidth, targetHeight))
                {
                    std::cout << "[Render] Direct rendering " << mapping.name << std::endl;
                    _render = mapping.function;
                    return true;
                }
            }
        }
    }
    else
    {
        std::cout << "[Render] Scaling required from " << frame->width << "x" << frame->height << " to " << targetWidth << "x" << targetHeight << std::endl;
    }

    if (!prepareTexture(SDL_PIXELFORMAT_IYUV, targetWidth, targetHeight))
        return false;

    if (!_sws || _swsWidth != frame->width || _swsHeight != frame->height)
    {
        if (_sws)
        {
            sws_freeContext(_sws);
            _sws = nullptr;
        }

        _sws = sws_getContext(frame->width, frame->height, (AVPixelFormat)frame->format,
                              targetWidth, targetHeight, AV_PIX_FMT_YUV420P,
                              scaler, nullptr, nullptr, nullptr);
        if (!_sws)
        {
            std::cerr << "[Render] Can't create sws context" << std::endl;
            return false;
        }
        _swsWidth = frame->width;
        _swsHeight = frame->height;
    }

    if (!_frame)
    {
        _frame = av_frame_alloc();
        if (!_frame)
        {
            std::cerr << "[Render] Can't allocate AVFrame" << std::endl;
            return false;
        }
        _frame->format = AV_PIX_FMT_YUV420P;
        _frame->width = targetWidth;
        _frame->height = targetHeight;
        // Allocate data buffer with 32 byte allingment
        int avRes = av_frame_get_buffer(_frame, 32);
        if (avRes != 0)
        {
            std::cerr << "[Render] Can't allocate AVFrame buffer: " << Error::avErrorText(avRes) << std::endl;
            return false;
        }
    }

    std::cout << "[Render] Scaling rendering source format " << frame->format << std::endl;
    _render = &Renderer::scale;
    return true;
}

void Renderer::rgb(AVFrame *frame)
{
    SDL_UpdateTexture(
        texture,
        nullptr,
        frame->data[0], frame->linesize[0]);
}

void Renderer::nv(AVFrame *frame)
{
    SDL_UpdateNVTexture(
        texture,
        nullptr,
        frame->data[0], frame->linesize[0],
        frame->data[1], frame->linesize[1]);
}
void Renderer::yuv(AVFrame *frame)
{
    SDL_UpdateYUVTexture(
        texture,
        nullptr,
        frame->data[0], frame->linesize[0],
        frame->data[1], frame->linesize[1],
        frame->data[2], frame->linesize[2]);
}

void Renderer::scale(AVFrame *frame)
{
    // Scale frame to output format
    sws_scale(_sws,
              frame->data, frame->linesize,
              0, _swsHeight,
              _frame->data,
              _frame->linesize);

    // Update SDL texture with YUV frame data
    SDL_UpdateYUVTexture(texture, nullptr,
                         _frame->data[0], _frame->linesize[0],
                         _frame->data[1], _frame->linesize[1],
                         _frame->data[2], _frame->linesize[2]);
}
