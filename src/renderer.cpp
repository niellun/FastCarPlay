#include "renderer.h"
#include <cstring>
#include "settings.h"
#include "common/functions.h"
#include "common/logger.h"
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
        log_e("SDL can't open font: %s", SDL_GetError());
        return;
    }

    _font = TTF_OpenFontRW(font_rw, 1, ptsize);
    if (!_font)
    {
        log_e("SDL can't load font: %s", TTF_GetError());
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
        log_e("Failed to create text surface: %s", TTF_GetError());
        return nullptr;
    }

    SDL_Texture *textTexture = SDL_CreateTextureFromSurface(renderer, textSurface);
    SDL_FreeSurface(textSurface);
    if (!textTexture)
    {
        log_e("Failed to create text texture: %s", TTF_GetError());
        return nullptr;
    }

    return textTexture;
}

RendererImage::RendererImage(const void *img_data, int img_size)
    : width(0), height(0), _surface(nullptr)
{
    SDL_RWops *img_rw = SDL_RWFromConstMem(img_data, img_size);
    if (!img_rw)
    {
        log_e("SDL can't open image: %s", SDL_GetError());
        return;
    }

    _surface = SDL_LoadBMP_RW(img_rw, 1);
    if (!_surface)
    {
        log_e("Failed to create image surface: %s", SDL_GetError());
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
    }

    float scale = 1.0 * h / height;
    int imgw = width * scale * Settings::aspectCorrection;

    SDL_Rect dst = {w - imgw, 0, imgw, h};
    SDL_RenderCopy(renderer, _texture, nullptr, &dst);
    return dst;
}

Renderer::Renderer(SDL_Renderer *renderer)
    : xScale(0),
      yScale(0),
      _renderer(renderer),
      _texture(nullptr),
      _textureWidth(0),
      _textureHeight(0),
      _sourceRect({0, 0, 0, 0}),
      _render(nullptr),
      _sws(nullptr),
      _frame(nullptr)
{
    if (Settings::alternativeRendering)
    {
        _mapping[1].function = &Renderer::yuvAlternative;
        _mapping[2].function = &Renderer::yuvAlternative;
        _mapping[3].function = &Renderer::nvAlternative;
    }
}

Renderer::~Renderer()
{
    clear();
}

bool Renderer::render(AVFrame *frame)
{
    if (_render == nullptr || frame->width != _textureWidth || frame->height != _textureHeight)
    {
        clear();
        if (!prepare(frame, Settings::width, Settings::height))
            return false;
    }
    (this->*_render)(frame);
    SDL_RenderCopy(_renderer, _texture, &_sourceRect, nullptr);
    SDL_RenderPresent(_renderer);
    return true;
}

bool Renderer::prepareTexture(uint32_t format, int width, int height)
{
    _texture = SDL_CreateTexture(_renderer, format,
                                 SDL_TEXTUREACCESS_STREAMING,
                                 width, height);
    if (!_texture)
    {
        log_e("SDL can't create video texture: %s", SDL_GetError());
        return false;
    }

    _textureWidth = width;
    _textureHeight = height;
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

bool Renderer::prepare(AVFrame *frame, int targetWidth, int targetHeight)
{
    if (targetWidth == 0 || targetHeight == 0)
        return false;

    float scale = (float)frame->width / targetWidth;
    float scale2 = (float)frame->height / targetHeight;
    if (scale > scale2)
        scale = scale2;
    int width = targetWidth * scale;
    int height = targetHeight * scale;

    _sourceRect = {(frame->width - width) / 2, (frame->height - height) / 2, width, height};
    xScale = (float)width / frame->width;
    yScale = (float)height / frame->height;

    log_i("Prepare renderer %dx%d for source %dx%d target %dx%d", width, height, frame->width, frame->height, targetWidth, targetHeight);

    AVPixelFormat fmt = static_cast<AVPixelFormat>(frame->format);
    for (const FormatMapping &mapping : _mapping)
    {
        if (mapping.avFormat == fmt)
        {
            if (prepareTexture(mapping.sdlFormat, frame->width, frame->height))
            {
                log_i("Direct rendering %s", mapping.name.c_str());
                _render = mapping.function;
                return true;
            }
        }
    }

    if (!prepareTexture(SDL_PIXELFORMAT_IYUV, frame->width, frame->height))
        return false;

    int swsFlags = Settings::fastScale ? SWS_FAST_BILINEAR : SWS_BILINEAR;
    _sws = sws_getContext(frame->width, frame->height, (AVPixelFormat)frame->format,
                          frame->width, frame->height, AV_PIX_FMT_YUV420P,
                          swsFlags, nullptr, nullptr, nullptr);
    if (!_sws)
    {
        log_e("Can't create sws context");
        return false;
    }

    _frame = av_frame_alloc();
    if (!_frame)
    {
        log_e("Can't allocate AVFrame");
        return false;
    }
    _frame->format = AV_PIX_FMT_YUV420P;
    _frame->width = frame->width;
    _frame->height = frame->height;
    // Allocate data buffer with 32 byte allingment
    int avRes = av_frame_get_buffer(_frame, 32);
    if (avRes != 0)
    {
        log_e("Can't allocate AVFrame buffer: %s", avErrorText(avRes).c_str());
        return false;
    }

    log_i("Scaling rendering source format %d", frame->format);
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

void Renderer::nv(AVFrame *frame)
{
    SDL_UpdateNVTexture(
        _texture,
        nullptr,
        frame->data[0], frame->linesize[0],
        frame->data[1], frame->linesize[1]);
}

void Renderer::nvAlternative(AVFrame *frame)
{
    uint8_t *pixels = nullptr;
    int pitch = 0;
    if (SDL_LockTexture(_texture, nullptr, (void **)&pixels, &pitch) != 0)
        return;

    // Y plane
    for (int i = 0; i < frame->height; i++)
        memcpy(pixels + i * pitch, frame->data[0] + i * frame->linesize[0], frame->width);

    // UV interleaved plane (half height, full width)
    uint8_t *uv = pixels + pitch * frame->height;
    for (int i = 0; i < frame->height / 2; i++)
        memcpy(uv + i * pitch, frame->data[1] + i * frame->linesize[1], frame->width);

    SDL_UnlockTexture(_texture);
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

void Renderer::yuvAlternative(AVFrame *frame)
{
    uint8_t *pixels = nullptr;
    int pitch = 0;
    if (SDL_LockTexture(_texture, nullptr, (void **)&pixels, &pitch) != 0)
        return;

    // Y plane
    for (int i = 0; i < frame->height; i++)
        memcpy(pixels + i * pitch, frame->data[0] + i * frame->linesize[0], frame->width);

    // U plane
    uint8_t *u = pixels + pitch * frame->height;
    for (int i = 0; i < frame->height / 2; i++)
        memcpy(u + i * (pitch / 2), frame->data[1] + i * frame->linesize[1], frame->width / 2);

    // V plane
    uint8_t *v = u + (pitch / 2) * (frame->height / 2);
    for (int i = 0; i < frame->height / 2; i++)
        memcpy(v + i * (pitch / 2), frame->data[2] + i * frame->linesize[2], frame->width / 2);

    SDL_UnlockTexture(_texture);
}

void Renderer::scale(AVFrame *frame)
{
    // Scale frame to output format
    sws_scale(_sws,
              frame->data, frame->linesize,
              0, _frame->height,
              _frame->data,
              _frame->linesize);

    if (Settings::alternativeRendering)
        yuvAlternative(_frame);
    else
        yuv(_frame);
}
