#include "renderer.h"
#include <iostream>
#include "settings.h"
#include "helper/functions.h"
#include <SDL2/SDL_ttf.h>

RendererText::RendererText(const void *font_data, int data_size, int ptsize)
    : width(0),
      height(0),
      _font(nullptr),
      _texture(nullptr),
      _text(" "),
      _color({0, 0, 0, 0})
{
    if (ptsize < 1)
        return;

    SDL_RWops *font_rw = SDL_RWFromConstMem(font_data, data_size);
    if (!font_rw)
    {
        std::cerr << "[UX] SDL can't open font: " << SDL_GetError() << std::endl;
        return;
    }

    _font = TTF_OpenFontRW(font_rw, 1, ptsize);
    if (!_font)
    {
        std::cerr << "[UX] SDL can't load font: " << TTF_GetError() << std::endl;
    }
};

RendererText::~RendererText()
{
    if (_texture)
    {
        SDL_DestroyTexture(_texture);
        _texture = nullptr;
    }

    if (_font)
    {
        TTF_CloseFont(_font);
        _font = nullptr;
    }
};

bool RendererText::prepare(SDL_Renderer *renderer, std::string text, SDL_Color color)
{
    if (!_texture || _text.compare(text) != 0 || !sameColor(_color, color))
    {
        if (_texture)
            SDL_DestroyTexture(_texture);
        _texture = getText(renderer, text.c_str(), color);
        _text = text;
        _color = color;
        SDL_QueryTexture(_texture, nullptr, nullptr, &width, &height);
    }
    return _texture;
}

SDL_Rect RendererText::draw(SDL_Renderer *renderer, int x, int y)
{
    if (!_texture)
        return {0, 0, 0, 0};

    SDL_Rect dstRect = {x, y, (int)(width * Settings::aspectCorrection), height};
    SDL_RenderCopy(renderer, _texture, nullptr, &dstRect);
    return dstRect;
}

SDL_Texture *RendererText::getText(SDL_Renderer *renderer, const char *text, SDL_Color color)
{
    if (!_font)
        return nullptr;

    SDL_Surface *textSurface = TTF_RenderText_Blended(_font, text, color);
    if (!textSurface)
    {
        std::cerr << "[UX] Failed to create text surface: " << TTF_GetError() << std::endl;
        return nullptr;
    }

    SDL_Texture *textTexture = SDL_CreateTextureFromSurface(renderer, textSurface);
    SDL_FreeSurface(textSurface);
    if (!textTexture)
    {
        std::cerr << "[UX] Failed to create text texture: " << TTF_GetError() << std::endl;
        return nullptr;
    }

    return textTexture;
}

RendererImage::RendererImage(const void *img_data, int img_size)
    : width(0), height(0), _surface(nullptr), _aspect(0)
{
    SDL_RWops *img_rw = SDL_RWFromConstMem(img_data, img_size);
    if (!img_rw)
    {
        std::cerr << "[UX] SDL can't open image: " << SDL_GetError() << std::endl;
        return;
    }

    _surface = SDL_LoadBMP_RW(img_rw, 1);
    if (!_surface)
    {
        std::cerr << "[UX] Failed to create image surface: " << SDL_GetError() << std::endl;
        return;
    }

    width = _surface->w;
    height = _surface->h;
};

RendererImage::~RendererImage()
{
    if (_surface)
    {
        SDL_FreeSurface(_surface);
        _surface = nullptr;
    }
};

SDL_Rect RendererImage::draw(SDL_Renderer *renderer, int w, int h)
{
    if (!_texture)
    {
        _texture = SDL_CreateTextureFromSurface(renderer, _surface);
        if (!_texture)
            return {0, 0, 0, 0};
        SDL_GetRendererOutputSize(renderer, &width, &height);
        _aspect = 1.0 * height / width;
    }

    float scale = 1.0 * h / height;
    int imgw = width * scale * Settings::aspectCorrection;

    SDL_Rect dst = {w - imgw, 0, imgw, h};
    SDL_RenderCopy(renderer, _texture, nullptr, &dst);
    return dst;
}

const Renderer::FormatMapping Renderer::_mapping[] = {
    {AV_PIX_FMT_RGB24, SDL_PIXELFORMAT_RGB24, &Renderer::rgb, &Renderer::crgb, "RGB24", 3},
    {AV_PIX_FMT_YUV420P, SDL_PIXELFORMAT_IYUV, &Renderer::yuv, &Renderer::cyuv, "YUV420P", 0},
    {AV_PIX_FMT_YUVJ420P, SDL_PIXELFORMAT_IYUV, &Renderer::yuv, &Renderer::cyuv, "YUVJ420P", 0},
    {AV_PIX_FMT_NV12, SDL_PIXELFORMAT_NV12, &Renderer::nv, &Renderer::cnv, "NV12", 0}};

Renderer::Renderer(SDL_Renderer *renderer)
    : _renderer(renderer),
      _texture(nullptr),
      _textureWidth(0),
      _textureHeight(0),
      _cropX(0),
      _cropY(0),
      _crop(false),
      _bytesPerPixel(0),
      _render(nullptr),
      _sws(nullptr),
      _frame(nullptr)
{
}

Renderer::~Renderer()
{
    clear();
}

bool Renderer::render(AVFrame *frame)
{
    if (_render == nullptr || _texture == nullptr)
        return false;
    (this->*_render)(frame);
    SDL_RenderClear(_renderer);
    SDL_RenderCopy(_renderer, _texture, nullptr, nullptr);
    SDL_RenderPresent(_renderer);
    return true;
}

bool Renderer::prepareTexture(uint32_t format, int width, int height)
{
    if (_texture)
    {
        if (_textureWidth == width && _textureHeight == height)
            return true;
        SDL_DestroyTexture(_texture);
        _texture = nullptr;
    }

    _texture = SDL_CreateTexture(_renderer, format,
                                 SDL_TEXTUREACCESS_STREAMING,
                                 width, height);
    if (!_texture)
    {
        std::cerr << "[UX] SDL can't create video texture: " << SDL_GetError() << std::endl;
        return false;
    }
    return true;
}

void Renderer::clear()
{
    if (_texture)
    {
        SDL_DestroyTexture(_texture);
        _texture = nullptr;
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

bool Renderer::prepare(AVFrame *frame, int targetWidth, int targetHeight, uint32_t scaler, bool android, float *cropX, float *cropY)
{
    clear();
    std::cout << "[UX] Prepare renderer " << targetWidth << "x" << targetHeight << " for source " << frame->width << "x" << frame->height << " " << (android ? "android auto" : "carplay") << std::endl;
    if (targetWidth == 0 || targetHeight == 0)
        return false;

    bool scaled = frame->height * targetWidth == targetHeight * frame->width;
    int width = frame->width;
    int height = frame->height;

    if (android && !scaled)
    {
        float scale = (float)frame->width / targetWidth;
        float scale2 = (float)frame->height / targetHeight;
        if (scale > scale2)
            scale = scale2;
        width = targetWidth * scale;
        height = targetHeight * scale;
    }

    bool cropW = android && width != frame->width;
    bool cropH = android && height != frame->height;
    _cropX = cropW ? (frame->width - width) / 2 : 0;
    _cropY = cropH ? (frame->height - height) / 2 : 0;
    *cropX = cropW ? (float)width / frame->width : 1;
    *cropY = cropH ? (float)height / frame->height : 1;

    if (scaled || cropW || cropH)
    {
        AVPixelFormat fmt = static_cast<AVPixelFormat>(frame->format);
        for (const FormatMapping &mapping : _mapping)
        {
            if (mapping.avFormat == fmt)
            {
                if (prepareTexture(mapping.sdlFormat, width, height))
                {
                    std::cout << "[UX] Direct rendering " << mapping.name << std::endl;
                    _render = (cropW || cropH) ? mapping.functionCrop : mapping.function;
                    _bytesPerPixel = mapping.bpp;
                    return true;
                }
            }
        }
    }
    else
    {
        std::cout << "[UX] Scaling required from " << frame->width << "x" << frame->height << " to " << targetWidth << "x" << targetHeight << std::endl;
    }

    if (!prepareTexture(SDL_PIXELFORMAT_IYUV, width, height))
        return false;

    _sws = sws_getContext(frame->width, frame->height, (AVPixelFormat)frame->format,
                          width, height, AV_PIX_FMT_YUV420P,
                          scaler, nullptr, nullptr, nullptr);
    if (!_sws)
    {
        std::cerr << "[UX] Can't create sws context" << std::endl;
        return false;
    }

    _frame = av_frame_alloc();
    if (!_frame)
    {
        std::cerr << "[UX] Can't allocate AVFrame" << std::endl;
        return false;
    }
    _frame->format = AV_PIX_FMT_YUV420P;
    _frame->width = width;
    _frame->height = height;
    // Allocate data buffer with 32 byte allingment
    int avRes = av_frame_get_buffer(_frame, 32);
    if (avRes != 0)
    {
        std::cerr << "[UX] Can't allocate AVFrame buffer: " << avErrorText(avRes) << std::endl;
        return false;
    }

    std::cout << "[UX] Scaling rendering source format " << frame->format << std::endl;
    _render = &Renderer::scale;
    return true;
}

void Renderer::rgb(AVFrame *frame)
{
    SDL_UpdateTexture(
        _texture,
        nullptr,
        frame->data[0], frame->linesize[0]);
}

void Renderer::crgb(AVFrame *frame)
{
    uint8_t *rgb_data = frame->data[0] + _cropY * frame->linesize[0] + _cropX * _bytesPerPixel;
    SDL_UpdateTexture(
        _texture,
        nullptr,
        rgb_data, frame->linesize[0]);
}

void Renderer::nv(AVFrame *frame)
{
    SDL_UpdateNVTexture(
        _texture,
        nullptr,
        frame->data[0], frame->linesize[0],
        frame->data[1], frame->linesize[1]);
}

void Renderer::cnv(AVFrame *frame)
{
    uint8_t *y_plane = frame->data[0] + _cropY * frame->linesize[0] + _cropX;

    // UV plane (subsampled by 2)
    uint8_t *uv_plane = frame->data[1] + (_cropY / 2) * frame->linesize[1] + 2 * (_cropX / 2);

    SDL_UpdateNVTexture(
        _texture,
        nullptr,
        y_plane, frame->linesize[0],
        uv_plane, frame->linesize[1]);
}

void Renderer::yuv(AVFrame *frame)
{
    SDL_UpdateYUVTexture(
        _texture,
        nullptr,
        frame->data[0], frame->linesize[0],
        frame->data[1], frame->linesize[1],
        frame->data[2], frame->linesize[2]);
}

void Renderer::cyuv(AVFrame *frame)
{

    int crop_x_chroma = _cropX / 2;
    int crop_y_chroma = _cropY / 2;
    uint8_t *y_plane = frame->data[0] + _cropY * frame->linesize[0] + _cropX;
    uint8_t *u_plane = frame->data[1] + crop_y_chroma * frame->linesize[1] + crop_x_chroma;
    uint8_t *v_plane = frame->data[2] + crop_y_chroma * frame->linesize[2] + crop_x_chroma;
    SDL_UpdateYUVTexture(
        _texture,
        nullptr,
        y_plane, frame->linesize[0],
        u_plane, frame->linesize[1],
        v_plane, frame->linesize[2]);
}

void Renderer::scale(AVFrame *frame)
{
    // Scale frame to output format
    sws_scale(_sws,
              frame->data, frame->linesize,
              0, _textureHeight,
              _frame->data,
              _frame->linesize);

    yuv(_frame);
}

void Renderer::cscale(AVFrame *frame)
{
    // Scale frame to output format
    sws_scale(_sws,
              frame->data, frame->linesize,
              0, _textureHeight,
              _frame->data,
              _frame->linesize);

    cyuv(_frame);
}